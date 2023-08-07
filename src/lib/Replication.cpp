#include "Replication.h"

#include "nsblast/logging.h"
#include "proto_util.h"

using namespace std;
using namespace std::string_literals;


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

GrpcPrimary::on_trxid_fn_t Replication::addAgent(GrpcPrimary::SyncClient &client, uint64_t fromTrxId)
{
    lock_guard lock{mutex_};

    auto agent = make_shared<FollowerAgent>(*this, client, fromTrxId);
    follower_agents_[client.uuid()] = agent;

    return [agent](uint64_t trxId) {
        agent->onTrxId(trxId);
    };
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

Replication::FollowerAgent::FollowerAgent(Replication& parent, GrpcPrimary::SyncClient &client, uint64_t fromTrxId)
    : currentTrx{fromTrxId}, client_{client.weak_from_this()}, parent_{parent}
{

}

void Replication::FollowerAgent::iterateDb()
{
    auto trx = parent_.server().db().dbTransaction();

    const ResourceIf::RealKey key{currentTrx, ResourceIf::RealKey::Class::TRXID};

    auto fn = [this](ResourceIf::TransactionIf::key_t key, span_t value) {
        auto update = make_shared<grpc::nsblast::pb::SyncUpdate>();
        update->set_isinsync(false); // We are iterating, so not in sync (streaming).
        auto mtrx = update->mutable_trx();
        if (!mtrx->ParseFromArray(value.data(), value.size())) [[unlikely]] {
            LOG_ERROR << "Replication::Agent::iterateDb - Failed to deserialize the transaction "
                      << "for " << key;
        } else if (auto client = client_.lock()) {
            // enqueue will return false if the queue is full
            const auto trx_id = mtrx->id();
            const auto result =  client->enqueue(std::move(update));
            if (result) {
                currentTrx = trx_id;
                return true;
            }
        }
        return false;
    };

    trx->iterateFromPrevT(key, ResourceIf::Category::TRXLOG, fn);
}

void Replication::FollowerAgent::onTrxId(uint64_t trxId)
{
    lock_guard lock{mutex_};
    lastConfirmedTrx = trxId;

    if (lastConfirmedTrx == currentTrx) {
        if (state_ == State::ITERATING_DB) {
            // Try to switch to streaming
            parent_.applyIf(trxId, [this] {
                LOG_TRACE << "Replication::Agent::onTrxId - Agent "
                          << uuid() << " is switching to streaming updates.";
                state_ = State::STREAMING;
            });
        }
    }
}

boost::uuids::uuid Replication::FollowerAgent::uuid() const noexcept
{
    if (auto client = client_.lock()) {
        return client->uuid();
    }

    return {};
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
