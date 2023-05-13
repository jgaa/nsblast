
#include <memory>

#include "AuthMgr.h"
#include "nsblast/logging.h"
#include "nsblast/errors.h"

using namespace std;

namespace nsblast::lib {

namespace {


void validate(const pb::Tenant& tenant) {
    // TODO: Validate
}

void validate(const pb::User& user) {
    // TODO: Validate
}

void validate(const pb::Zone& zone) {
    // TODO: Validate
}

template <typename T, ResourceIf::Category cat = ResourceIf::Category::ACCOUNT>
void upsert(trx_t& trx, const ResourceIf::RealKey& key, const T& value, bool isNew) {
    validate(value);

    string raw;
    value.SerializeToString(&raw);
    trx.write(key, raw, isNew, cat);
}

} // anon ns

optional<pb::Tenant> AuthMgr::getTenant(std::string_view tenantId) const
{
    auto trx = server_.resource().transaction();

    ResourceIf::RealKey key{tenantId, ResourceIf::RealKey::Class::TENANT};
    std::string value;
    if (!trx->read(key, value, ResourceIf::Category::ACCOUNT, false)) {
        return {};
    }

    optional<nsblast::pb::Tenant> tenant;
    tenant.emplace();

    if (tenant->ParseFromArray(value.data(), value.size())) {
        return tenant;
    }

    LOG_ERROR << "AuthMgr::get Failed to deserialize from " << key;
    throw InternalErrorException{"Failed to deserialize Tenant"};
}

string AuthMgr::createTenant(pb::Tenant &tenant)
{
    auto trx = server_.resource().transaction();

    string id;
    if (tenant.has_id()) {
        id = toLower(tenant.id());
    } else {
        id = newUuidStr();
    }

    tenant.set_id(id);
    if (!tenant.has_active()) {
        tenant.set_active(true);
    }

    ResourceIf::RealKey key{id, ResourceIf::RealKey::Class::TENANT};
    if (trx->keyExists(key, ResourceIf::Category::ACCOUNT)) {
        LOG_INFO << "AuthMgr::createTenant - Tenant already exist: " << key;
        throw AlreadyExistException{"Tenant already exist"};
    }

    LOG_INFO << "Creating tenant " << id;
    upsert(*trx, key, tenant, true);
    trx->commit();
    return id;
}

void AuthMgr::upsertTenant(std::string_view tenantId,
                           const pb::Tenant &tenant, bool merge)
{
    assert(!tenantId.empty());
    auto trx = server_.resource().transaction();
    if (tenant.has_id()) {
        if (tenant.id() != tenantId) {
            throw ConstraintException{"id is immutable"};
        }
    }

    auto id = toLower(tenant.id());
    ResourceIf::RealKey key{id, ResourceIf::RealKey::Class::TENANT};

    if (merge) {
        auto existing = getTenant(id);
        if (existing) {
            existing->MergeFrom(tenant);
            upsert(*trx, key, *existing, false);
            goto commit;
        }
    }

    upsert(*trx, key, tenant, false);

commit:
    trx->commit();
}

void AuthMgr::deleteTenant(std::string_view tenantId)
{
    auto trx = server_.resource().transaction();
    ResourceIf::RealKey key{toLower(tenantId), ResourceIf::RealKey::Class::TENANT};

    if (!trx->keyExists(key, ResourceIf::Category::ACCOUNT)) {
        throw NotFoundException{"Tenant not found"};
    }

    // TODO:
    // enumerate zones, roles, users
    // delete zones, roles, users

    trx->remove(key, true, ResourceIf::Category::ACCOUNT);

    LOG_INFO << "Deleting tenant " << tenantId;
    trx->commit();
}

void AuthMgr::addZone(trx_t &trx, std::string_view fqdn, std::string_view tenant)
{
    assert(fqdn == toLower(fqdn));
    ResourceIf::RealKey key_zone{fqdn, ResourceIf::RealKey::Class::ZONE};
    ResourceIf::RealKey key_tzone{tenant, fqdn, ResourceIf::RealKey::Class::TZONE};

    pb::Zone zone;
    zone.set_status(pb::ACTIVE);

    auto id = newUuidStr();
    LOG_INFO << "Creating new Zone " << fqdn << " for tenant " << tenant
             << " with uuid " << id;
    zone.set_id(id);
    zone.set_tenantid(string{tenant});
    upsert(trx, key_zone, zone, true);

    // Add index so we can find it by tenantId
    trx.write(key_tzone, fqdn, true, ResourceIf::Category::ACCOUNT);
}

} // ns
