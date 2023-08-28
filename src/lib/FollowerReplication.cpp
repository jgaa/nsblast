
#include <sstream>
#include <boost/lexical_cast.hpp>

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

    parent_.server().grpcFollow().createSyncClient([this]() {
        lock_guard lock{mutex_};
        return current_trxid_;
    }, [this](const grpc::nsblast::pb::SyncUpdate& update){
        LOG_TRACE << "FollowerReplication::Agent--update called with update. sync="
            << update.isinsync()
            << ", trx #" << update.trx().id();

        try {
            onTrx(update.trx());
            const auto id =  update.trx().id();

            auto was_in_sync = parent_.is_in_sync_;
            parent_.is_in_sync_ = update.isinsync();

            if (parent_.is_in_sync_ != was_in_sync) {
                LOG_INFO << "Changed replication state to "
                         << (parent_.is_in_sync_ ? "IN_SYNC" : "NOT_IN_SYNC");
            }

            {
                lock_guard lock{mutex_};
                current_trxid_ = id;
            }
        } catch(const exception& ex) {
            LOG_ERROR_N << "Failed to apply transaction #"
                      << update.trx().id()
                      << ": " << ex.what();
        }
    });
}

void FollowerReplication::Agent::onTrx(const pb::Transaction &value)
{
    const auto trxid = value.id();

    LOG_TRACE_N << "Applying transaction #" << trxid;

    auto trx = parent_.server().db().dbTransaction();
    trx->disableTrxlog();

    // Re-compose each of the parts of the original transaction
    for(const auto& part : value.parts()) {
        const ResourceIf::RealKey key{ResourceIf::RealKey::Binary{part.key()}};
        try {
            auto cat = ResourceIf::toCatecory(part.columnfamilyix());
            trx->write(key, part.value(), false, cat);
        } catch (const exception& ex) {
            LOG_WARN_N << "Failed to write " << key << " of transaction "
                       << value.uuid() << " replid: #"  << trxid
                       << ": " << ex.what();
        }
    }

    // Also write the transaction-log entry
    string val;
    value.SerializeToString(&val);
    const ResourceIf::RealKey key{trxid, ResourceIf::RealKey::Class::TRXID};
    trx->write(key, val, false, ResourceIf::Category::TRXLOG);
    trx->commit();
}


} // ns
