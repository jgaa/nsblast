
#include <memory>

#include <boost/algorithm/string.hpp>

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

template <typename T, ResourceIf::Category cat = ResourceIf::Category::ACCOUNT>
optional<T> get(trx_t& trx, const ResourceIf::RealKey& key) {

    std::string raw;
    if (!trx.read(key, raw, cat, false)) {
        return {};
    }

    optional<T> value;
    value.emplace();
    if (value->ParseFromArray(raw.data(), raw.size())) {
        return value;
    }

    LOG_ERROR << "AuthMgr::get Failed to deserialize serialized data for key " << key;
    throw InternalErrorException{"Failed to deserialize "s + typeid(value).name()} ;
}

} // anon ns


yahat::Auth AuthMgr::authorize(const yahat::AuthReq &ar)
{
    auto hash = sha256(ar.auth_header, false);
    if (auto existing = keys_.get(hash)) {
        yahat::Auth a;
        a.access = existing->isAllowed(pb::Permission::USE_API);
        a.extra = existing;
        a.account = existing->tenant();
        return a;
    }

    static constexpr string_view basic = "basic ";
    if (compareCaseInsensitive(basic, ar.auth_header, false)) {
        return basicAuth(hash, ar.auth_header.substr(basic.size()), ar);
    }

    LOG_DEBUG << "AuthMgr::authorize: Unrecognized authentication method" << ar.auth_header.substr(0, 10);
    return {};
}

optional<pb::Tenant> AuthMgr::getTenant(std::string_view tenantId) const
{
    auto trx = server_.resource().transaction();

    ResourceIf::RealKey key{tenantId, ResourceIf::RealKey::Class::TENANT};

    return get<pb::Tenant>(*trx, key);
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
    upsertUserIndexes(*trx, tenant);
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
    upsertUserIndexes(*trx, tenant);
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

    auto tenant = getTenant(tenantId);
    if (tenant) {
        deleteUserIndexes(*trx, *tenant);
    }

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

void AuthMgr::deleteZone(trx_t &trx, std::string_view fqdn, std::string_view tenant)
{
    const ResourceIf::RealKey key_zone{fqdn, ResourceIf::RealKey::Class::ZONE};
    const ResourceIf::RealKey key_tzone{tenant, fqdn, ResourceIf::RealKey::Class::TZONE};

    auto zone = get<pb::Zone>(trx, key_zone);

    if (!zone) {
        LOG_WARN << "AuthMgr::deleteZone: Failed to lookup zone " << key_zone;
        return;
    }

    LOG_INFO << "Deleting Zone " << fqdn << " for tenant " << tenant
             << " with uuid " << zone->id();

    trx.remove(key_zone, false, ResourceIf::Category::ACCOUNT);
    trx.remove(key_tzone, false, ResourceIf::Category::ACCOUNT);
}

void AuthMgr::bootstrap()
{
    pb::Tenant tenant;

    tenant.set_id("nsblast");
    tenant.set_active(true);
    tenant.set_root(""); // all

    auto role = tenant.add_roles();

    {
        auto filter = role->mutable_filter();
        filter->set_fqdn(""); // all
        filter->set_recursive(true);
    }

    role->set_name("Administrator");

    for(int i = pb::Permission_MIN; i <= pb::Permission_MAX; ++i) {
        if (pb::Permission_IsValid(i)) {
            tenant.add_allowedpermissions(static_cast<pb::Permission>(i));
            role->add_permissions(static_cast<pb::Permission>(i));
        }
    }

    auto user = tenant.add_users();
    user->set_id("admin-id");
    user->set_loginname("admin");
    user->set_active(true);
    *user->add_roles() = role->name();

    auto auth = user->mutable_auth();
    auth->set_seed(getRandomStr(6));
    string passwd = getRandomStr(42);
    if (auto p = getenv("NSBLAST_ADMIN_PASSWORD")) {
        if (*p) {
            passwd = p;
        }
    } else {
        filesystem::path path = server_.config().db_path;
        path /= "password.txt";
        ofstream out{path, ios_base::trunc | ios_base::out};
        out << passwd;
        LOG_INFO << "admin-password written to: " << path;
    }

    auth->set_hash(createHash(auth->seed(), passwd));

    createTenant(tenant);
}

string AuthMgr::createHash(const std::string &seed, const std::string &passwd)
{
    auto combined = seed + passwd;
    return sha256(combined);
}

yahat::Auth AuthMgr::basicAuth(std::string hash,
                               std::string_view authString,
                               const yahat::AuthReq &ar)
{
    trim(authString);
    if (authString.empty()) {
        return {};
    }

    auto blob = base64Decode(authString);
    string_view user_pass{blob.data(), blob.size()};
    auto pos = user_pass.find(':');
    if (pos == string_view::npos) {
        return {};
    }

    auto user = user_pass.substr(0, pos);
    auto pass = user_pass.substr(pos + 1);

    LOG_TRACE << "AuthMgr::authorize: User='" << user << "', pass='" << pass << "'.";

    return {};
}

void AuthMgr::upsertUserIndexes(trx_t &trx, const pb::Tenant& tenant)
{
    for(auto& user: tenant.users()) {
        const ResourceIf::RealKey key{toLower(user.loginname()), ResourceIf::RealKey::Class::USER};

        string existing;
        if (trx.read(key, existing, ResourceIf::Category::ACCOUNT, false)) {
            if (toLower(existing) != toLower(tenant.id())) {
                LOG_WARN << "AuthMgr::updateUserIndexes: Rejecting user "
                         << key << " for tenant " << tenant.id()
                         << " because the loginName is already used by tenant "
                         << existing;
                throw AlreadyExistException{"LoginName "s + user.loginname() + " is already in use"};
            }
        }
        trx.write(key, toLower(tenant.id()), false, ResourceIf::Category::ACCOUNT);
    }
}

void AuthMgr::deleteUserIndexes(trx_t &trx, const pb::Tenant &tenant)
{
    for(auto& user: tenant.users()) {
        const ResourceIf::RealKey key{toLower(user.loginname()), ResourceIf::RealKey::Class::USER};

        string existing;
        if (trx.read(key, existing, ResourceIf::Category::ACCOUNT, false)) {
            if (toLower(existing) != toLower(tenant.id())) {
                LOG_WARN << "AuthMgr::updateUserIndexes: Not deleting key "
                         << key << " for tenant " << tenant.id()
                         << " because the loginName is used by tenant "
                         << existing;
                continue;
            }
        }

        trx.remove(key, false, ResourceIf::Category::ACCOUNT);
    }
}

bool Session::isAllowed(pb::Permission perm, std::string_view lowercaseFqdn, bool throwOnFailure) const
{
    return isAllowed(perm, throwOnFailure);
}

} // ns
