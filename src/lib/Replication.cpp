#include "Replication.h"

#include "nsblast/logging.h"
#include "proto_util.h"

using namespace std;
using namespace std::string_literals;

ostream& operator <<(ostream& out, const nsblast::lib::Replication::FollowerAgent& agent) {
    return out << "Replication::FollowerAgent{" << agent.uuid() << '}';
}

ostream &
operator<<(ostream &out,
           const nsblast::lib::Replication::FollowerAgent::State &state) {
    array<string_view, 3> names = {"ITERATING_DB", "STREAMING", "DONE"};

    return out << names.at(static_cast<size_t>(state));
}

namespace nsblast::lib {

void Replication::start()
{
    if (server().role() == Server::Role::CLUSTER_FOLLOWER) {
        // We must sync up...
        primary_agent_ = make_shared<PrimaryAgent>(*this);
        primary_agent_->init();
    }
}

Replication::Replication(Server &server)
    : server_{server}
{
    startTimer();
}

GrpcPrimary::ReplicationInterface *Replication::addAgent(
    const std::shared_ptr<GrpcPrimary::SyncClientInterface>& client)
{
    lock_guard lock{mutex_};

    auto agent = make_shared<FollowerAgent>(*this, client);
    follower_agents_[client->uuid()] = agent;

    return agent.get();
}

void Replication::onTransaction(transaction_t && transaction)
{
    auto update = make_shared<grpc::nsblast::pb::SyncUpdate>();
    update->mutable_trx()->Swap(transaction.get());
    transaction.reset();

    lock_guard lock{mutex_};
    const auto prev_trxid = last_trxid_;
    last_trxid_ = update->trx().id();

    if (prev_trxid >= last_trxid_) {
        LOG_ERROR << "New transaction has old trxid! prev_id=" << prev_trxid
                  << ", new_id=" << last_trxid_
                  << "I'm not replicating this transaction.";

        // Roll back change in id and return
        last_trxid_ = prev_trxid;
        return;
    }

    LOG_TRACE << "Replicating transacion #" << last_trxid_
              << " to " << follower_agents_.size()
              << " follower agents.";

    // Notify replication agents
    for(auto& [_, agent] : follower_agents_) {
        agent->onTransaction(prev_trxid, update);
    }
}

void Replication::startTimer()
{
    timer_.expires_from_now(boost::posix_time::seconds{server_.config().cluster_replication_housekeeping_timer_});
    timer_.async_wait([this](const auto ec) {
        if (ec) {
            if (ec == boost::asio::error::operation_aborted) {
                LOG_TRACE << "Replication housekeeping timer aborted.";
                return;
            }
            LOG_WARN << "Replication housekeeping timer unexpected error: " << ec;
        } else {
            try {
                housekeeping();
            } catch (const exception& ex) {
                LOG_ERROR << "Replication housekeeping timer: exception from housekeeping(): "
                          << ex.what();
            }
        }

        startTimer();
    });
}

void Replication::housekeeping()
{
    std::lock_guard lock{mutex_};

    LOG_TRACE << "Replication::housekeeping() - "
              "Deleting zombie agents where the RPC request/stream is done with.";
    erase_if(follower_agents_, [](auto& item) {
        return item.second->expired();
    });

}

Replication::FollowerAgent::FollowerAgent(Replication& parent,
                                          const std::shared_ptr<GrpcPrimary::SyncClientInterface>& client)
    : uuid_{client->uuid()}
    , client_{client}, parent_{parent}
{

}

void Replication::FollowerAgent::iterateDb()
{
    auto trx = parent_.server().db().dbTransaction();

    const ResourceIf::RealKey key{last_enqueued_trxid_, ResourceIf::RealKey::Class::TRXID};
    bool queue_was_filled = false;

    auto fn = [this, queue_was_filled](ResourceIf::TransactionIf::key_t key, span_t value) mutable {
        auto update = make_shared<grpc::nsblast::pb::SyncUpdate>();
        update->set_isinsync(false); // We are iterating, so not in sync (streaming).
        auto mtrx = update->mutable_trx();
        if (!mtrx->ParseFromArray(value.data(), value.size())) [[unlikely]] {
            LOG_ERROR << *this << " iterateDb - Failed to deserialize the transaction "
                      << "for " << key;
        } else if (auto client = client_.lock()) {
            // enqueue will return false if the queue is full
            const auto trx_id = mtrx->id();
            const auto result =  client->enqueue(std::move(update));
            if (result) {
                last_enqueued_trxid_ = trx_id;
                return true;
            } else {
                queue_was_filled = true;
            }
        }
        return false;
    };

    trx->iterateFromPrevT(key, ResourceIf::Category::TRXLOG, fn);

    if (!queue_was_filled) {
        LOG_TRACE << *this
                  << " The queue was not filled while iterating the stored transactions."
                     " Switching replication to streaming mode";

        setState(State::STREAMING);
    }
}

void Replication::FollowerAgent::setState(State state, bool doLock)
{
    State old = {};
    {
        optional<lock_guard<mutex>> lock;
        if (doLock) {
            lock.emplace(mutex_);
        }

        if (state_ == State::DONE) [[unlikely]] {
            LOG_DEBUG << *this << " setState: Rejecting state change to "
                      << state
                      << " because the state is alreadt DONE";
            return;
        }

        old = state_;
        state_ = state;

        if (state == State::DONE || state == State::STREAMING) {

            // Notify callers that we are idle.
            // This is primarily for unit-tests to avoid abritrary timouts
            // to wait for the agent to catch up.
            while(!test_promises_.empty()) {
                test_promises_.front().set_value();
                test_promises_.pop();
            }
        }
    }
    LOG_DEBUG << *this << " setState Changed state from "
              << old << " to " << state;
}

void Replication::FollowerAgent::onTrxId(uint64_t trxId)
{
    lock_guard lock{mutex_};
    last_confirmed_trx_ = trxId;
    syncLater();
}

void Replication::FollowerAgent::onQueueIsEmpty()
{
    lock_guard lock{mutex_};
    syncLater();
}

void Replication::FollowerAgent::onDone()
{
    LOG_DEBUG << *this << " onDone: The replication-session has ended.";
    setState(State::DONE);

    lock_guard lock{mutex_};
    client_.reset();
}

void Replication::FollowerAgent::syncLater()
{
    assert(!mutex_.try_lock() && "The lock must me held");

    if (state_ == State::ITERATING_DB && !is_syncing_) {
        is_syncing_ = true;

        // We must return immediately, so schedule a follow-up
        // on a worker-thread.
        parent_.server().ctx().post([w=weak_from_this()]{
            if (auto self = w.lock()) {
                try {
                    self->iterateDb();

                    lock_guard lock{self->mutex_};
                    self->is_syncing_ = false;
                } catch (const exception& ex) {
                    LOG_ERROR << "Replication::FollowerAgent::sync "
                              << " caught exception from iterateDb() on "
                              << self->uuid()
                              << ": " << ex.what();
                }
            }
        });
    }
}

boost::uuids::uuid Replication::FollowerAgent::uuid() const noexcept
{
    if (auto client = client_.lock()) {
        return client->uuid();
    }

    return {};
}

void Replication::FollowerAgent::onTransaction(uint64_t prevTrxId,
                                               const GrpcPrimary::update_t& update)
{
    lock_guard lock{mutex_};

    // Check inside the lock so we don't risk a state-change while we are here.
    if (!isStreaming()) {
        return;
    }

    if (prevTrxId == last_enqueued_trxid_) {
        if (auto client = client_.lock()) {
            if (!client->enqueue(update)) {
                setState(State::ITERATING_DB, false);
            }
            last_enqueued_trxid_ = update->trx().id();
        } else {
            assert(false && "Client object removed while the agent is still in streaming mode!");
        }
    } else {
        LOG_TRACE << *this << " Out of sync in streaming.";
        setState(State::ITERATING_DB, false);
        syncLater();
    }
}

Replication::PrimaryAgent::PrimaryAgent(Replication &parent)
    : parent_{parent}
{
}

void Replication::PrimaryAgent::init()
{
    current_trxid_ = parent_.server().db().getLastCommittedTransactionId();
    due_ = chrono::steady_clock::now();

    parent_.server().grpcFollow().createSyncClient([this]() -> optional<uint64_t> {
        lock_guard lock{mutex_};
        if (due_ && due_ <= chrono::steady_clock::now()) {
            due_.reset();
            LOG_TRACE << "Replication::PrimaryAgent: - due is replying with trx-id #" << current_trxid_;
            return current_trxid_;
        }
        return {};
    }, [this](const grpc::nsblast::pb::SyncUpdate& update){
        try {
            onTrx(update.trx());
            const auto id =  update.trx().id();
            {
                lock_guard lock{mutex_};
                current_trxid_ = id;
                if (!due_) {
                    due_ = chrono::steady_clock::now() + chrono::milliseconds(
                               parent_.server().config().cluster_followers_update_delay_);
                }
            }
        } catch(const exception& ex) {
            LOG_ERROR << "Replication::PrimaryAgent - Failed to apply transaction #"
                      << update.trx().id()
                      << ": " << ex.what();
        }
    });
}

void Replication::PrimaryAgent::onTrx(const pb::Transaction &value)
{
    const auto trxid = value.id();

    LOG_TRACE << "Replication::PrimaryAgent::onTrx - Applying transaction #" << trxid;

    const ResourceIf::RealKey key{trxid, ResourceIf::RealKey::Class::TRXID};
    auto trx = parent_.server().db().dbTransaction();
    trx->disableTrxlog();

    // Re-compose each of the parts of the original transaction
    for(const auto& part : value.parts()) {
        const ResourceIf::RealKey key{ResourceIf::RealKey::Binary{part.key()}};
        auto cat = static_cast<ResourceIf::Category>(part.columnfamilyix());
        trx->write(key, part.value(), false, cat);
    }

    // Also write the transaction-log entry
    string val;
    value.SerializeToString(&val);
    trx->write(key, val, false, ResourceIf::Category::TRXLOG);
    trx->commit();
}


} // ns
