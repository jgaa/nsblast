#pragma once

#include <set>
#include <boost/unordered/unordered_flat_map.hpp>
#include <chrono>
#include <atomic>
#include <list>
#include <regex>

#include "nsblast/nsblast.h"
#include "nsblast/ResourceIf.h"
#include "proto/nsblast.pb.h"
#include "nsblast/Server.h"
#include "nsblast/errors.h"
#include "proto_util.h"


namespace nsblast::lib {

class AuthMgr;

namespace detail {

template <typename dataT, typename keyT = std::string, typename shadowKeyT = std::string_view>
class Lru {
public:
    struct Item;
    using lru_iterator_t = typename std::list<Item *>::iterator;
    struct Item {
        Item(keyT && key, dataT && data)
            : data_{std::move(data)}, key_{std::move(key)} {}

        lru_iterator_t lruIt_;
        dataT data_;
        keyT key_;
    };

    Lru(size_t capacity) : capacity_{capacity} {}

    void emplace(keyT key, dataT data) {
        auto instance = std::make_unique<Item>(std::move(key), std::move(data));
        std::lock_guard lock{mutex_};
        instance->lruIt_ = lru_.insert(lru_.begin(), instance.get());
        auto [_, inserted] = items_.insert_or_assign(instance->key_, std::move(instance));
        if (inserted) {
            makeSpace();
        }
        assert(items_.size() == lru_.size());
    }

    dataT get(const shadowKeyT& key, bool throwOnNotFound = false) {
        {
            std::lock_guard lock{mutex_};
            assert(items_.size() == lru_.size());
            if (auto it = items_.find(key) ; it != items_.end()) {
                auto& item = *it->second;

                lru_.erase(item.lruIt_);
                item.lruIt_ = lru_.insert(lru_.begin(), it->second.get());
                return item.data_;
            }
            assert(items_.size() == lru_.size());
        }

        if (throwOnNotFound) {
            throw NotFoundException{"Unknown key"};
        }

        return {};
    }

    bool erase(const keyT& key) {
        std::lock_guard lock{mutex_};

        if (auto it = items_.find(key) ; it != items_.end()) {
            lru_.erase(it->second->lruIt_);
            items_.erase(it);
            assert(items_.size() == lru_.size());
            return true;
        }

        return false;
    }

    void clear() {
        std::lock_guard lock{mutex_};
        items_.clear();
        lru_.clear();
        assert(items_.size() == lru_.size());
    }

    size_t size() {
        std::lock_guard lock{mutex_};
        assert(items_.size() == lru_.size());
        assert(items_.size() == lru_.size());
        return items_.size();
    }

private:
    // Assume we hold the mutex
    void makeSpace() {
        assert(items_.size() == lru_.size());
        if (items_.size() > capacity_) {
            assert(!lru_.empty());
            items_.erase(lru_.back()->key_);
            lru_.pop_back();
        }
        assert(items_.size() == lru_.size());
    }

    mutable std::mutex mutex_;
    boost::unordered_flat_map<shadowKeyT, std::shared_ptr<Item>> items_;
    std::list<Item *> lru_;
    const size_t capacity_;
};

// Here individual permissions are represented as bits in an integer
using perms_t = uint64_t;

template <typename retT = perms_t, typename eT>
static retT getBit(const eT perm) noexcept {
    const auto bitshift =  static_cast<uint8_t>(perm);
    assert(bitshift < sizeof(retT) * 8);
    return (1 << static_cast<uint8_t>(bitshift));
}

template <typename T, typename retT = detail::perms_t>
static detail::perms_t getPerms(const T& perms) noexcept {
    retT perm = {};
    for(const auto p : perms) {
        perm |= getBit<retT>(p);
    }
    return perm;
}

struct ZoneFilter {
    ZoneFilter() = default;

    ZoneFilter(const std::string& fqdn,
               bool recursive,
               const std::string& regex)
        : fqdn_{fqdn}, recursive_{recursive}, regex_{regex}
    {}


    std::string fqdn_;
    bool recursive_ = false;
    std::string regex_;
    std::optional<std::regex> match_;
};

struct Role {

    Role() = default;

    Role(std::string name)
        : name_{std::move(name)} {}

    bool appliesToAll() const noexcept {
        return !filters_
               || (filters_->fqdn_.empty()
                   && filters_->regex_.empty()
               );
    }

    bool matchesFqdn(std::string_view fqdn) const noexcept;

    std::string name_;
    perms_t permissions_ = 0;
    std::optional<ZoneFilter> filters_;
};


} // ns detail

/*! Session object that follows an individual request.
 */
class Session : public std::enable_shared_from_this<Session> {
public:

    template <typename T>
    Session(AuthMgr& mgr, const pb::Tenant& tenant, const std::string& who, const T& roles)
        : mgr_{mgr}, tenant_{tenant.id()}, who_{who} {
        populate(tenant, roles);
        init(tenant);
    };

    Session(AuthMgr& mgr)
        : mgr_{mgr}, tenant_{"nsblast"}, who_{"somebody"} {};

    // Check if a non-zone permission is granted
    bool isAllowed(pb::Permission perm, bool throwOnFailure = false) const;

    std::string_view tenant() const;

    // Check if a zone-permission is granted
    bool isAllowed(pb::Permission perm, std::string_view lowercaseFqdn, bool throwOnFailure = false) const;

    yahat::Auth getAuth() noexcept;

    const std::string& who() const noexcept {
        return who_;
    }

private:
    void init(const pb::Tenant& tenant);
    template <typename T>
    void populate(const pb::Tenant& tenant, const T& roles) {

        auto exists_in = [&roles](const auto& name) {
            for(const auto& rn : roles) {
                if (compareCaseInsensitive(rn, name)) {
                    return true;
                }
            }
            return false;
        };

        for(const auto& r : tenant.roles()) {
            const auto& name = toLower(r.name());
            if (exists_in(r.name())) {
                auto& nr = roles_.emplace_back(toLower(r.name()));
                nr.permissions_ = detail::getPerms(r.permissions());
                if (r.has_filter()) {
                    nr.filters_.emplace(PB_GET(r.filter(), fqdn, ""),
                                        PB_GET(r.filter(), recursive, true),
                                        PB_GET(r.filter(), regex, ""));
                }
            }
        }
    }

    detail::perms_t non_zone_perms_ = 0;
    std::vector<detail::Role> roles_;
    AuthMgr& mgr_;
    const std::string tenant_;
    const std::string who_;
};

class AuthMgr {
public:
    AuthMgr(Server& server);

    yahat::Auth authorize(const yahat::AuthReq& ar);

    /*! Internal method to log in. Primarily ment for unit tests */
    yahat::Auth login(std::string_view name, std::string_view password);

    // Methods to support the REST api

    /*! Get an existing tenant
     *
     *  \param tenantId ID of the tenant
     *
     *  \returns optional with Tenant object if the tenant was found.
     *  \throws std::runtime_error if there was an internal error.
     */
    std::optional<pb::Tenant> getTenant(std::string_view tenantId) const;

    std::string createTenant(pb::Tenant& tenant);

    // Returns true if a tenant was created
    bool upsertTenant(std::string_view tenantId, const pb::Tenant& tenant,
                      bool merge);

    /*! Delete an existing tenant
     *
     *  This method will enumerate all resources owned by the tenant,
     *  like zones, users, roles, and delete them all. It will try to
     *  delete everything in one atomic transaction to the database.
     *
     * \param tenantId ID of the tenant
     * \throws Exception if there was an internal error.
     */
    void deleteTenant(std::string_view tenantId);

    void addZone(trx_t& trx, std::string_view fqdn, std::string_view tenant);
    void deleteZone(trx_t& trx, std::string_view fqdn, std::string_view tenant);


    /*! Bootstrap authentication
     *
     *  - Create nsblast tenant
     *  - Create admin user
     *
     *  The admin user can take the password from an environment-variable
     *      NSBLAST_ADMIN_PASSWORD. If the variable is unset, a random
     *      password is generated and written to a file: {dbpath}/password.txt
     */
    void bootstrap();

    static std::string createHash(std::string_view seed, std::string_view passwd);

    static bool hasAuth() noexcept {
        return has_auth_;
    }

    void resetTokensForTenant(std::string_view tenantId);

private:
    yahat::Auth basicAuth(std::string hash, std::string_view authString,
                          const boost::uuids::uuid reqUuid);
    void processUsers(pb::Tenant& tenant, const std::optional<pb::Tenant>& existingTenant);
    void upsertUserIndexes(trx_t& trx, const pb::Tenant& tenant,
                           const std::optional<pb::Tenant>& existingTenant);
    void deleteUserIndexes(trx_t& trx, const pb::Tenant& tenant);

    Server& server_;
    detail::Lru<std::shared_ptr<Session>> keys_;
    static bool has_auth_;
    static constexpr std::string_view admin_id_{"d98e539e-fc78-11ed-9f34-bbfe306147e3"};
};

} // ns
