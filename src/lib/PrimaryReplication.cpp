#include "PrimaryReplication.h"

#include "nsblast/logging.h"
#include "proto_util.h"

using namespace std;
using namespace std::string_literals;

ostream& operator <<(ostream& out, const nsblast::lib::PrimaryReplication::Agent& agent) {
    return out << "PrimaryReplication::FollowerAgent{" << agent.uuid() << '}';
}

ostream &
operator<<(ostream &out,
           const nsblast::lib::PrimaryReplication::Agent::State &state) {
    array<string_view, 3> names = {"ITERATING_DB", "STREAMING", "DONE"};

    return out << names.at(static_cast<size_t>(state));
}

namespace nsblast::lib {

PrimaryReplication::PrimaryReplication(Server &server)
    : server_{server}, waiter_{server.ctx(),
              [](uint64_t current, uint64_t constraint) {
                  return current <= constraint;
              }}
{
}

void PrimaryReplication::start()
{
    startTimer();
}

void PrimaryReplication::checkAgents()
{
    if (auto trxid = getMinTrxIdForAllAgents()) {
        waiter_.onChange(minTrxIdForAllStreamingAgents_);
    }
}

GrpcPrimary::ReplicationInterface *PrimaryReplication::addAgent(
    const std::shared_ptr<GrpcPrimary::SyncClientInterface>& client)
{
    lock_guard lock{mutex_};

    auto agent = make_shared<Agent>(*this, client);
    follower_agents_[client->uuid()] = agent;

    return agent.get();
}

void PrimaryReplication::onTransaction(transaction_t && transaction)
{
    auto update = make_shared<grpc::nsblast::pb::SyncUpdate>();
    update->set_isinsync(true); // Only streaming client gets this
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

    // Notify PrimaryReplication agents
    for(auto& [_, agent] : follower_agents_) {
        agent->onTransaction(prev_trxid, update);
    }
}

void PrimaryReplication::startTimer()
{
    timer_.expires_from_now(boost::posix_time::seconds{server_.config().cluster_replication_housekeeping_timer_});
    timer_.async_wait([this](const auto ec) {
        if (ec) {
            if (ec == boost::asio::error::operation_aborted) {
                LOG_TRACE << "PrimaryReplication housekeeping timer aborted.";
                return;
            }
            LOG_WARN << "PrimaryReplication housekeeping timer unexpected error: " << ec;
        } else {
            try {
                housekeeping();
            } catch (const exception& ex) {
                LOG_ERROR << "PrimaryReplication housekeeping timer: exception from housekeeping(): "
                          << ex.what();
            }
        }

        startTimer();
    });
}

void PrimaryReplication::housekeeping()
{
    std::lock_guard lock{mutex_};

    LOG_TRACE << "PrimaryReplication::housekeeping() - "
              "Deleting zombie agents where the RPC request/stream is done with.";
    erase_if(follower_agents_, [](auto& item) {
        return item.second->expired();
    });

}

uint64_t PrimaryReplication::getMinTrxIdForAllAgents()
{
    lock_guard lock{mutex_};
    uint64_t lowest = std::numeric_limits<uint64_t>::max(); // Start high
    for(const auto& [_, agent] : follower_agents_) {
        if (agent->isStreaming()) {
            lowest = std::min(agent->lastConfirmedTrx(), lowest);
        }
    }

    if (lowest == std::numeric_limits<uint64_t>::max()) {
        return 0; // No streaming agents.
    }

    if (lowest > minTrxIdForAllStreamingAgents_) {
        LOG_TRACE << "PrimaryReplication::PrimaryReplication: minTrxIdForAllStreamingAgents_ changing from "
                  << minTrxIdForAllStreamingAgents_ << " to " << lowest;
        minTrxIdForAllStreamingAgents_ = lowest;
        return minTrxIdForAllStreamingAgents_;
    }

    return 0; // No changes
}

PrimaryReplication::Agent::Agent(PrimaryReplication& parent,
                                          const std::shared_ptr<GrpcPrimary::SyncClientInterface>& client)
    : uuid_{client->uuid()}
    , client_{client}, parent_{parent}
{

}

void PrimaryReplication::Agent::iterateDb()
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
                     " Switching PrimaryReplication to streaming mode";

        setState(State::STREAMING);
    }
}

void PrimaryReplication::Agent::setState(State state, bool doLock)
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

void PrimaryReplication::Agent::onTrxId(uint64_t trxId)
{
    {
        lock_guard lock{mutex_};
        last_confirmed_trx_ = trxId;
        syncLater();
    }

    parent_.checkAgents();
}

void PrimaryReplication::Agent::onQueueIsEmpty()
{
    lock_guard lock{mutex_};
    syncLater();
}

void PrimaryReplication::Agent::onDone()
{
    LOG_DEBUG << *this << " onDone: The PrimaryReplication-session has ended.";
    setState(State::DONE);

    lock_guard lock{mutex_};
    client_.reset();
}

void PrimaryReplication::Agent::syncLater()
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
                    LOG_ERROR << "PrimaryReplication::FollowerAgent::sync "
                              << " caught exception from iterateDb() on "
                              << self->uuid()
                              << ": " << ex.what();
                }
            }
        });
    }
}

boost::uuids::uuid PrimaryReplication::Agent::uuid() const noexcept
{
    if (auto client = client_.lock()) {
        return client->uuid();
    }

    return {};
}

void PrimaryReplication::Agent::onTransaction(uint64_t prevTrxId,
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

} // ns
