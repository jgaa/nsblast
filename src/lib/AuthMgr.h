#pragma once

#include <set>
#include <boost/unordered/unordered_flat_map.hpp>
#include <chrono>
#include <atomic>
#include <list>

#include "nsblast/nsblast.h"
#include "nsblast/ResourceIf.h"
#include "proto/nsblast.pb.h"
#include "nsblast/Server.h"
#include "nsblast/errors.h"


namespace nsblast::lib {

class AuthMgr;

namespace detail {

template <typename dataT, typename keyT = std::string, typename shadowKeyT = std::string_view>
class Lru {
public:
    struct Item;
    using lru_iterator_t = typename std::list<Item *>::iterator;
    struct Item {
        lru_iterator_t lruIt_;
        dataT data_;
        keyT key_;
    };

    Lru(size_t capacity) : capacity_{capacity} {}

    void emplace(keyT && key, Item && item) {
        auto instance = std::make_unique<Item>({}, std::move(item), std::move(key));
        std::lock_guard lock{mutex_};
        instance->lruIt_ = lru_.push_front(instance.get());
        auto [_, inserted] = items_.insert_or_assign(instance->key_, std::move(instance));
        if (inserted) {
            makeSpace();
        }
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
            return true;
        }

        return false;
    }

    size_t size() {
        std::lock_guard lock{mutex_};
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

class ZoneFilter {
    std::string fqdn_;
    std::string regex;
    bool recursive_ = false;
    uint32_t rrTypes_ = {};
};

class Role {
    std::string name_;
    perms_t permissions_;
    std::optional<ZoneFilter> filters_;
};


} // ns detail

/*! Session object that follows an individual request.
 */
class Session {
public:

    Session(AuthMgr& mgr, const std::string tenant, detail::perms_t perms)
        : perms_{perms}, mgr_{mgr}, tenant_{tenant} {};

    // Check if a non-zone permission is granted
    bool isAllowed(pb::Permission perm, bool throwOnFailure = false) const {
        const auto bit = detail::getBit(perm);
        auto result = (perms_ & bit) == bit;
        if (!result && throwOnFailure) {
            throw DeniedException{};
        }
        return result;
    }

    std::string_view tenant() const {
        return tenant_;
    }

    // Check if a zone-permission is granted
    bool isAllowed(pb::Permission perm, std::string_view lowercaseFqdn, bool throwOnFailure = false) const;

private:
    std::optional<pb::ZoneFilter> filter_;
    const detail::perms_t perms_ = 0;
    AuthMgr& mgr_;
    const std::string tenant_;
};

class AuthMgr {
public:
    AuthMgr(Server& server)
        : server_{server}, keys_{server.config().auth_cache_lru_size} {}

    yahat::Auth authorize(const yahat::AuthReq& ar);

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
    void upsertTenant(std::string_view tenantId, const pb::Tenant& tenant, bool merge);

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

    static std::string createHash(const std::string& seed, const std::string& passwd);

private:
    yahat::Auth basicAuth(std::string hash, std::string_view authString, const yahat::AuthReq &ar);
    void upsertUserIndexes(trx_t& trx, const pb::Tenant& tenant);
    void deleteUserIndexes(trx_t& trx, const pb::Tenant& tenant);

    Server& server_;
    detail::Lru<std::shared_ptr<Session>> keys_;
};

} // ns
