
#include <memory>

#include <boost/algorithm/string.hpp>

#include "AuthMgr.h"
#include "nsblast/logging.h"
#include "nsblast/errors.h"
#include "proto_util.h"

using namespace std;

namespace nsblast::lib {

namespace {

auto getSeed() {
    return getRandomStr(6);
}

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


bool AuthMgr::has_auth_ = true;

AuthMgr::AuthMgr(Server &server)
    : server_{server}, keys_{server.config().auth_cache_lru_size} {
    has_auth_ = server.config().enable_auth;
}

yahat::Auth AuthMgr::authorize(const yahat::AuthReq &ar)
{
    auto hash = sha256(ar.auth_header, false);
    if (auto existing = keys_.get(hash)) {
        LOG_TRACE << "Request " <<  ar.req.uuid << " proceeded with session-key " << Base64Encode(hash);
        return existing->getAuth();
    }

    if (ar.auth_header.empty()) {
        LOG_TRACE << "Request " <<  ar.req.uuid << " provided no Authorization header.";
        return {};
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

    processUsers(tenant);

    LOG_INFO << "Creating tenant " << id;
    upsert(*trx, key, tenant, true);
    upsertUserIndexes(*trx, tenant);
    trx->commit();
    return id;
}

bool AuthMgr::upsertTenant(std::string_view tenantId,
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

    auto existing = getTenant(id);
    if (merge) {
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

    auto was_new = !existing.has_value();
    if (!was_new) {
        resetTokensForTenant(tenantId);
    }

    return was_new;
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
    resetTokensForTenant(tenantId);
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
    auth->set_seed(getSeed());
    string passwd = getRandomStr(42);
    if (auto p = getenv("NSBLAST_ADMIN_PASSWORD")) {
        if (*p) {
            passwd = p;
            LOG_INFO << "Setting admin password to value in envvar NSBLAST_ADMIN_PASSWORD";
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

void AuthMgr::resetTokensForTenant(std::string_view tenantId)
{
    // For now, just delete all the tokens.
    LOG_INFO << "Resetting auth-keys after change in tenant " << tenantId;
    keys_.clear();
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

    auto loginName = toLower(user_pass.substr(0, pos));
    auto pass = user_pass.substr(pos + 1);

    //LOG_TRACE << "AuthMgr::authorize: User='" << loginName << "', pass='" << pass << "'.";

    const ResourceIf::RealKey key{loginName, ResourceIf::RealKey::Class::USER};

    std::string tenantId;
    auto trx = server_.resource().transaction();
    if (trx->read(key, tenantId, ResourceIf::Category::ACCOUNT, false)) {
        const ResourceIf::RealKey tkey{tenantId, ResourceIf::RealKey::Class::TENANT};
        if (auto tenant = get<pb::Tenant>(*trx, tkey)) {
            for(const auto& user: tenant->users()) {
                const auto lcName = toLower(user.loginname());
                if (lcName == loginName) {
                    if (!user.has_auth()) {
                        LOG_DEBUG << " AuthMgr::basicAuth No Auth data for login for user " << loginName
                                  << " at tenant " << tenant->id()
                                  << " for request " << ar.req.uuid;
                        return {};
                    }
                    auto pwhash = createHash(user.auth().seed(), string{pass});
                    if (pwhash != user.auth().hash()) {
                        LOG_DEBUG << " AuthMgr::basicAuth Invalid password for login from user " << loginName
                                  << " at tenant " << tenant->id()
                                  << " for request " << ar.req.uuid;
                        return {};
                    }

                    vector<string_view> role_names;
                    for(const auto& name : user.roles()) {
                        role_names.push_back(name);
                    }
                    auto session = make_shared<Session>(*this, *tenant, loginName, role_names);
                    keys_.emplace(hash, session);
                    LOG_DEBUG << " AuthMgr::basicAuth Added session key "
                              << Base64Encode(hash) << " for user " << loginName
                              << " at tenant " << tenant->id()
                              << " for request " << ar.req.uuid;

                    return session->getAuth();
                }
            }
        }
    }

    LOG_DEBUG << " AuthMgr::basicAuth User " << toPrintable(loginName)
              << " not found for request " << ar.req.uuid;
    return {};
}

void AuthMgr::processUsers(pb::Tenant &tenant)
{
    for(auto it = tenant.mutable_users()->begin(); it != tenant.mutable_users()->end(); ++it) {

        // Make sure the users have id's
        if (!it->has_id()) {
            *it->mutable_id() = newUuidStr();
        }

        if (!it->has_auth()) {
            throw ConstraintException("Missing auth section in user "s + it->loginname());
        }

        // Set seed if not set, create hash if password is set.
        auto& auth = *it->mutable_auth();

        if (!auth.has_seed()) {
            auth.set_seed(getSeed());
        }

        if (auth.has_password()) {
            auth.set_hash(createHash(auth.seed(), auth.password()));
            auth.clear_password();
        }

        if (!auth.has_hash()) {
            if (!auth.has_password()) {
                throw ConstraintException("Must have password or hash in user "s + it->loginname());
            }
        }
    }
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

bool Session::isAllowed(pb::Permission perm, bool throwOnFailure) const {
    if (!AuthMgr::hasAuth()) {
        return true;
    }
    const auto bit = detail::getBit(perm);
    auto result = (non_zone_perms_ & bit) == bit;
    if (!result && throwOnFailure) {
        auto pname = pb::Permission_Name(perm);
        throw DeniedException{"Access denied for "s + pname + "."};
    }
    return result;
}

string_view Session::tenant() const {
    return tenant_;
}

bool Session::isAllowed(pb::Permission perm, std::string_view lowercaseFqdn, bool throwOnFailure) const
{
    if (!AuthMgr::hasAuth()) {
        return true;
    }
    auto perms = non_zone_perms_;

    for(const auto& role : roles_) {
        if (!role.appliesToAll()) {
            if (role.matchesFqdn(lowercaseFqdn)) {
                perms |= role.permissions_;
            }
        }
    }

    const auto bit = detail::getBit(perm);
    const auto result = (perms & bit) == bit;
    if (!result && throwOnFailure) {
        auto pname = pb::Permission_Name(perm);
        throw DeniedException{"Access denied for "s + pname + ": " + string{lowercaseFqdn}};
    }
    return result;
}

yahat::Auth Session::getAuth() noexcept
{
    yahat::Auth a;
    a.access = isAllowed(pb::Permission::USE_API);
    a.extra = shared_from_this();
    a.account = tenant();
    return a;
}

void Session::init(const pb::Tenant& tenant)
{
    auto root = PB_GET(tenant, root, "");

    for (auto& role : roles_) {
        if (role.appliesToAll()) {
            non_zone_perms_ |= role.permissions_;
        }

        if (role.filters_) {
            auto& filter = *role.filters_;
            string pattern;
            pattern.reserve(root.size() + filter.fqdn_.size() + filter.regex_.size() + 16);
            if (filter.fqdn_.empty()) {
                if (!filter.recursive_) {
                    LOG_DEBUG << "Filter in role " << role.name_
                              << " is invalid (fqdn is empty, and the filter is non-recuirsive)";
                    continue;
                }
            } else {
                static const regex is_valid_hostname{R"(^[_a-zA-Z0-9.\-]*$)"};
                if (!std::regex_match(filter.fqdn_, is_valid_hostname)) {
                    LOG_DEBUG << "Filter in role " << role.name_
                              << " is invalid (fqdn contains invalid characters): "
                              << " pattern: " << pattern;
                    continue;
                }

                pattern = filter.fqdn_;
                assert(!pattern.empty());
                if (!pattern.ends_with('.')) {
                    pattern += ".";
                }
            }
            pattern += root;
            if (pattern.find('\\') != string::npos) {
                LOG_DEBUG << "Filter in role " << role.name_ << " is invalid (contains backslash): "
                          << " pattern: " << pattern;
                continue;
            }
            boost::replace_all(pattern, ".", "\\.");
            if (!filter.regex_.empty()) {
                // TODO: Validate assumtion:
                //   - The user cannot affect the second part of the pattern with the value
                //     of the regex because the paranthesis would become unbalance and
                //     the regex compilation would fail ann hence invalidate the filter.
                pattern = "^("s + filter.regex_ + ")(" + pattern + ")$";
            } else {
                if (filter.recursive_) {
                    pattern = "^(.*\\.)*(" + pattern + ")$";
                } else {
                    pattern = "^(" + pattern + ")$";
                }
            }

            LOG_TRACE << "Session::init Assigning filter regex " << pattern
                      << " to role " << role.name_
                      << " for tenant " << tenant.id();
            try {
                role.filters_->match_.emplace(toLower(pattern));
            } catch(const std::exception& ex) {
                LOG_INFO << "Session::init Discarding role " << role.name_
                         << " for tenant " << tenant.id()
                         << " with regex " << pattern
                         << ": " << ex.what();
            }
        }
    }
}


bool detail::Role::matchesFqdn(std::string_view fqdn) const noexcept
{
    if (filters_ && filters_->match_) {
        return regex_match(fqdn.begin(), fqdn.end(), *filters_->match_);
    }

    return false;
}


} // ns
