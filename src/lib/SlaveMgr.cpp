#include "SlaveMgr.h"
#include "Slave.h"

#include "nsblast/logging.h"
#include "nsblast/util.h"
#include "proto_util.h"

using namespace std;
using namespace std::string_literals;


namespace nsblast::lib {

SlaveMgr::SlaveMgr(const Config &config, ResourceIf &resource, boost::asio::io_context &ctx)
    : config_{config}, db_{resource}, ctx_{ctx}
{

}

void SlaveMgr::getZone(string_view fqdn, pb::Zone &zone)
{
    auto trx = db_.transaction();

    string buffer;
    trx->read(fqdn, buffer, ResourceIf::Category::ZONE);
    zone.ParseFromString(buffer);
}

void SlaveMgr::addZone(string_view fqdn, const pb::Zone& zone)
{
    string r;
    zone.SerializeToString(&r);

    auto trx = db_.transaction();
    trx->write(fqdn, r, true, ResourceIf::Category::ZONE);
    trx->commit();
    reload(fqdn);
}

void SlaveMgr::replaceZone(string_view fqdn, const pb::Zone& zone)
{
    string r;
    zone.SerializeToString(&r);

    auto trx = db_.transaction();
    trx->write(fqdn, r, false, ResourceIf::Category::ZONE);
    trx->commit();
    reload(fqdn);
}

void SlaveMgr::mergeZone(string_view fqdn, const pb::Zone& zone)
{
    assert(false); // Not implemnted
}

void SlaveMgr::deleteZone(string_view fqdn)
{
    auto trx = db_.transaction();
    trx->remove(fqdn, false, ResourceIf::Category::ZONE);
    trx->commit();
    reload(fqdn);
}

void SlaveMgr::init()
{
    // TODO: Read all active zones we will replicate and
    //       create Slave objects and set timers.

    auto trx = db_.transaction();
    trx->iterate("", [this](ResourceIf::TransactionIf::key_t key, span_t value) {

        pb::Zone z;
        if (z.ParseFromArray(value.data(), value.size())) {
            if (PB_GET(z, active, true)) {
                reload({key.data(), key.size()}, z);
            }
        } else {
            LOG_ERROR << "SlaveMgr::init Failed to deserialize Zone: " << toPrintable(key);
        }

        return true;
    }
    , ResourceIf::Category::ZONE);
}

void SlaveMgr::reload(string_view fqdn)
{
    pb::Zone zone;
    getZone(fqdn, zone);
    reload(fqdn, zone);
}

void SlaveMgr::reload(string_view fqdn, pb::Zone &zone)
{
    const string key{fqdn};
    string info_message;

    {
        const bool active = PB_GET(zone, active, true);
        lock_guard<mutex> lock{mutex_};
        if (auto it = zones_.find(key); it != zones_.end()) {
            // Lazily abort any on-going update
            it->second->done();
            if (active) {
                LOG_DEBUG << "Realoading configuration for master-zone " << fqdn;
            } else {
                zones_.erase(it);
                info_message = "Deactivating (no longer synching from) master-zone "s + key;
            }
        }

        if (active) {
            auto slave = make_shared<Slave>(*this, key, zone);
            slave->start();
            zones_[key] = slave;
        }
    }

    if (!info_message.empty()) {
        LOG_INFO << info_message;
    }
}


};