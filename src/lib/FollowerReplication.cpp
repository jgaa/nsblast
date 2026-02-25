
#include <sstream>
#include <boost/lexical_cast.hpp>

#include "FollowerReplication.h"
#include "RocksDbResource.h"
#include "Metrics.h"

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
    if (parent_.server().config().cluster_force_full_resync) {
        LOG_WARN_N << "FollowerReplication::Agent::init - Forcing full resync from trx-id 0.";
        current_trxid_ = 0;
    } else {
        current_trxid_ = parent_.server().db().getLastCommittedTransactionId();
    }

    if (parent_.server().haveMetrics() && parent_.server().isReplicationFollower()) {
        parent_.server().metrics().cluster_replication_in_sync().set(0);
    }

    parent_.server().grpcFollow().createSyncClient([this]() {
        lock_guard lock{mutex_};
        return current_trxid_;
    }, [this](const grpc::nsblast::pb::SyncUpdate& update){
        LOG_TRACE << "FollowerReplication::Agent--update called with update. sync="
            << update.isinsync()
            << ", has trx=" << update.has_trx();

        try {
            if (update.has_trx()) {
                onTrx(update.trx());
                const auto id = update.trx().id();

                lock_guard lock{mutex_};
                current_trxid_ = id;
            }

            auto was_in_sync = parent_.is_in_sync_;
            parent_.is_in_sync_ = update.isinsync();
            if (parent_.server().haveMetrics() && parent_.server().isReplicationFollower()) {
                parent_.server().metrics().cluster_replication_in_sync().set(parent_.is_in_sync_ ? 1 : 0);
            }

            if (parent_.is_in_sync_ != was_in_sync) {
                LOG_INFO << "Changed replication state to "
                         << (parent_.is_in_sync_ ? "IN_SYNC" : "NOT_IN_SYNC");
            }
        } catch(const exception& ex) {
            LOG_ERROR_N << "Failed to apply transaction #"
                      << (update.has_trx() ? std::to_string(update.trx().id()) : "n/a")
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
        string_view op = "write";
        try {
            auto cat = ResourceIf::toCatecory(part.columnfamilyix());
            if (part.has_value()) {
                trx->write(key, part.value(), false, cat);
            } else {
                op = "remove";
                trx->remove(key, false, cat);
            }
        } catch (const exception& ex) {
            LOG_WARN_N << "Failed to " << op << ' ' << key << " of transaction "
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
