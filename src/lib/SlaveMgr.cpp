#include "SlaveMgr.h"

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

void SlaveMgr::addZone(string_view fqdn, const pb::Zone zone)
{
    string r;
    zone.SerializeToString(&r);

    auto trx = db_.transaction();
    trx->write(fqdn, r, true, ResourceIf::Category::ZONE);
    trx->commit();
    reload(fqdn);
}

void SlaveMgr::replaceZone(string_view fqdn, const pb::Zone zone)
{
    string r;
    zone.SerializeToString(&r);

    auto trx = db_.transaction();
    trx->write(fqdn, r, false, ResourceIf::Category::ZONE);
    trx->commit();
    reload(fqdn);
}

void SlaveMgr::mergeZone(string_view fqdn, const pb::Zone zone)
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
}

void SlaveMgr::reload(string_view fqdn)
{
    // Load/reload a single zone to capture changes.
}


};
