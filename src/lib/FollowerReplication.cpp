#include "FollowerReplication.h"
#include "RocksDbResource.h"

#include "nsblast/logging.h"
#include "proto_util.h"

using namespace std;
using namespace std::string_literals;

namespace nsblast::lib {

FollowerReplication::FollowerReplication(Server &server)
    : server_{server}
{

}

void FollowerReplication::start()
{
    primary_agent_ = make_shared<Agent>(*this);
    primary_agent_->init();
}


FollowerReplication::Agent::Agent(FollowerReplication &parent)
    : parent_{parent}
{

}

void FollowerReplication::Agent::init()
{
    current_trxid_ = parent_.server().db().getLastCommittedTransactionId();
    due_ = chrono::steady_clock::now();

    parent_.server().grpcFollow().createSyncClient([this]() -> optional<uint64_t> {
        lock_guard lock{mutex_};
        if (due_ && due_ <= chrono::steady_clock::now()) {
            due_.reset();
            LOG_TRACE << "PrimaryReplication::PrimaryAgent: - due is replying with trx-id #" << current_trxid_;
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
            LOG_ERROR << "PrimaryReplication::PrimaryAgent - Failed to apply transaction #"
                      << update.trx().id()
                      << ": " << ex.what();
        }
    });
}

void FollowerReplication::Agent::onTrx(const pb::Transaction &value)
{
    const auto trxid = value.id();

    LOG_TRACE << "PrimaryReplication::PrimaryAgent::onTrx - Applying transaction #" << trxid;

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
