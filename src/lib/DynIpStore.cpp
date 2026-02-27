#include "DynIpStore.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <format>
#include <optional>
#include <string>

#include "nsblast/errors.h"
#include "nsblast/logging.h"
#include "nsblast/util.h"

using namespace std;

namespace nsblast::lib {

namespace {

string nowUtc() {
    const auto now = std::chrono::system_clock::now();
    const auto tt = std::chrono::system_clock::to_time_t(now);
    char buffer[32] = {};
    const auto* tm = std::gmtime(&tt);
    if (tm) {
        std::strftime(buffer, sizeof(buffer), "%FT%TZ", tm);
        return string{buffer};
    }
    return {};
}

string makeHostKey(string_view root, string_view host) {
    return std::format("{}/{}", root, host);
}

bool startsWith(string_view value, string_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

template <typename T>
optional<T> readProto(ResourceIf::TransactionIf& trx, const ResourceIf::RealKey& key) {
    string raw;
    if (!trx.read(key, raw, ResourceIf::Category::ACCOUNT, false)) {
        return {};
    }

    T obj;
    if (!obj.ParseFromArray(raw.data(), static_cast<int>(raw.size()))) {
        throw InternalErrorException{"Failed to parse protobuf payload from DynIP store"};
    }
    return obj;
}

template <typename T>
void writeProto(ResourceIf::TransactionIf& trx,
                const ResourceIf::RealKey& key,
                const T& obj,
                bool isNew) {
    string raw;
    if (!obj.SerializeToString(&raw)) {
        throw InternalErrorException{"Failed to serialize DynIP protobuf payload"};
    }
    trx.write(key, raw, isNew, ResourceIf::Category::ACCOUNT);
}

} // namespace

bool DynIpStore::isValidLabel(string_view label) {
    if (label.empty() || label.size() > 63) {
        return false;
    }

    if (label.front() == '-' || label.back() == '-') {
        return false;
    }

    return ranges::all_of(label, [](char c) {
        return (c >= 'a' && c <= 'z')
               || (c >= '0' && c <= '9')
               || c == '-';
    });
}

string DynIpStore::normalizeLabel(string_view label) {
    auto normalized = string{label};
    trim(normalized);
    normalized = toLower(normalized);
    if (!isValidLabel(normalized)) {
        throw ConstraintException{"Invalid DNS label"};
    }
    return normalized;
}

string DynIpStore::makeRootFqdn(string_view root, string_view realm) {
    auto normalizedRealm = string{realm};
    trim(normalizedRealm);
    normalizedRealm = toLower(normalizedRealm);
    return std::format("{}.{}", root, normalizedRealm);
}

string DynIpStore::makeHostFqdn(string_view host, string_view root, string_view realm) {
    auto normalizedRealm = string{realm};
    trim(normalizedRealm);
    normalizedRealm = toLower(normalizedRealm);
    return std::format("{}.{}.{}", host, root, normalizedRealm);
}

pb::DynipRoot DynIpStore::createRoot(string_view tenantId,
                                     string_view root,
                                     string_view realm,
                                     uint32_t hostLimit) {
    const auto rootLower = normalizeLabel(root);
    const auto fqdn = makeRootFqdn(rootLower, realm);
    if (!validateFqdn(fqdn)) {
        throw ConstraintException{"Invalid DynIP root FQDN"};
    }

    auto trx = resource_.transaction();
    const ResourceIf::RealKey rootKey{rootLower, ResourceIf::RealKey::Class::DYNIP_ROOT};
    const ResourceIf::RealKey tenantIxKey{toLower(string{tenantId}), rootLower, ResourceIf::RealKey::Class::DYNIP_TROOT};
    if (trx->keyExists(rootKey, ResourceIf::Category::ACCOUNT)) {
        throw AlreadyExistException{"DynIP root already exists"};
    }

    pb::DynipRoot dynRoot;
    dynRoot.set_tenant_id(string{tenantId});
    dynRoot.set_root(rootLower);
    dynRoot.set_fqdn(fqdn);
    dynRoot.set_host_limit(hostLimit);
    dynRoot.set_created_at(nowUtc());
    dynRoot.set_updated_at(dynRoot.created_at());

    writeProto(*trx, rootKey, dynRoot, true);
    trx->write(tenantIxKey, {}, true, ResourceIf::Category::ACCOUNT);
    trx->commit();

    return dynRoot;
}

vector<pb::DynipRoot> DynIpStore::listRoots(string_view tenantId) {
    vector<pb::DynipRoot> roots;
    auto trx = resource_.transaction();
    const auto tenantPrefix = std::format("{}/", toLower(string{tenantId}));
    const ResourceIf::RealKey from{tenantPrefix, ResourceIf::RealKey::Class::DYNIP_TROOT};

    trx->iterate(from, [&](const ResourceIf::RealKey& key, span_t) {
        const auto raw = key.dataAsString();
        if (!startsWith(raw, tenantPrefix)) {
            return false;
        }

        const auto root = raw.substr(tenantPrefix.size());
        if (const auto dynRoot = readProto<pb::DynipRoot>(
                *trx, ResourceIf::RealKey{root, ResourceIf::RealKey::Class::DYNIP_ROOT})) {
            roots.push_back(*dynRoot);
        }
        return true;
    }, ResourceIf::Category::ACCOUNT);

    return roots;
}

bool DynIpStore::deleteRoot(string_view tenantId, string_view root) {
    const auto rootLower = normalizeLabel(root);
    auto trx = resource_.transaction();

    const ResourceIf::RealKey rootKey{rootLower, ResourceIf::RealKey::Class::DYNIP_ROOT};
    auto dynRoot = readProto<pb::DynipRoot>(*trx, rootKey);
    if (!dynRoot) {
        return false;
    }
    if (!compareCaseInsensitive(dynRoot->tenant_id(), tenantId, true)) {
        throw ConstraintException{"DynIP root belongs to another tenant"};
    }

    const auto hostPrefix = std::format("{}/", rootLower);
    const ResourceIf::RealKey fromHost{hostPrefix, ResourceIf::RealKey::Class::DYNIP_HOST};
    vector<ResourceIf::RealKey> hostKeys;
    vector<ResourceIf::RealKey> tokenKeys;

    trx->iterate(fromHost, [&](const ResourceIf::RealKey& key, span_t value) {
        const auto raw = key.dataAsString();
        if (!startsWith(raw, hostPrefix)) {
            return false;
        }

        pb::DynipHost hostObj;
        if (!hostObj.ParseFromArray(value.data(), static_cast<int>(value.size()))) {
            throw InternalErrorException{"Failed to parse DynIP host entry"};
        }
        hostKeys.emplace_back(key);
        tokenKeys.emplace_back(hostObj.token_hash(), ResourceIf::RealKey::Class::DYNIP_TOKEN);
        return true;
    }, ResourceIf::Category::ACCOUNT);

    for (const auto& key : tokenKeys) {
        trx->remove(key, false, ResourceIf::Category::ACCOUNT);
    }
    for (const auto& key : hostKeys) {
        trx->remove(key, false, ResourceIf::Category::ACCOUNT);
    }

    trx->remove(ResourceIf::RealKey{toLower(string{tenantId}), rootLower, ResourceIf::RealKey::Class::DYNIP_TROOT},
                false, ResourceIf::Category::ACCOUNT);
    trx->remove(rootKey, false, ResourceIf::Category::ACCOUNT);
    trx->commit();
    return true;
}

DynIpStore::CreatedHost DynIpStore::createHost(string_view tenantId,
                                               string_view root,
                                               string_view host,
                                               string_view realm,
                                               uint32_t ttl,
                                               uint32_t maxHostsPerRoot) {
    const auto rootLower = normalizeLabel(root);
    const auto hostLower = normalizeLabel(host);
    const auto fqdn = makeHostFqdn(hostLower, rootLower, realm);
    if (!validateFqdn(fqdn)) {
        throw ConstraintException{"Invalid DynIP host FQDN"};
    }

    auto trx = resource_.transaction();

    auto dynRoot = readProto<pb::DynipRoot>(*trx, ResourceIf::RealKey{rootLower, ResourceIf::RealKey::Class::DYNIP_ROOT});
    if (!dynRoot) {
        throw NotFoundException{"DynIP root not found"};
    }
    if (!compareCaseInsensitive(dynRoot->tenant_id(), tenantId, true)) {
        throw ConstraintException{"DynIP root belongs to another tenant"};
    }

    const auto hostPrefix = std::format("{}/", rootLower);
    const ResourceIf::RealKey fromHost{hostPrefix, ResourceIf::RealKey::Class::DYNIP_HOST};
    uint32_t currentHostCount = 0;
    trx->iterate(fromHost, [&](const ResourceIf::RealKey& key, span_t) {
        const auto raw = key.dataAsString();
        if (!startsWith(raw, hostPrefix)) {
            return false;
        }
        ++currentHostCount;
        return true;
    }, ResourceIf::Category::ACCOUNT);

    const auto limit = dynRoot->host_limit() > 0 ? dynRoot->host_limit() : maxHostsPerRoot;
    if (currentHostCount >= limit) {
        throw ConstraintException{"DynIP host limit reached for root"};
    }

    const auto hostKeyData = makeHostKey(rootLower, hostLower);
    const ResourceIf::RealKey hostKey{hostKeyData, ResourceIf::RealKey::Class::DYNIP_HOST};
    if (trx->keyExists(hostKey, ResourceIf::Category::ACCOUNT)) {
        throw AlreadyExistException{"DynIP host already exists"};
    }

    CreatedHost created;
    created.token = getRandomStr(48);
    const auto tokenHash = sha256(created.token);

    created.host.set_tenant_id(string{tenantId});
    created.host.set_root(rootLower);
    created.host.set_host(hostLower);
    created.host.set_fqdn(fqdn);
    created.host.set_token_hash(tokenHash);
    created.host.set_ttl(ttl);
    created.host.set_disabled(false);
    created.host.set_update_count(0);

    pb::DynipTokenIndex tokenIx;
    tokenIx.set_token_hash(tokenHash);
    tokenIx.set_tenant_id(string{tenantId});
    tokenIx.set_root(rootLower);
    tokenIx.set_host(hostLower);
    tokenIx.set_fqdn(fqdn);
    tokenIx.set_ttl(ttl);
    tokenIx.set_disabled(false);

    writeProto(*trx, hostKey, created.host, true);
    writeProto(*trx, ResourceIf::RealKey{tokenHash, ResourceIf::RealKey::Class::DYNIP_TOKEN}, tokenIx, true);
    trx->commit();

    return created;
}

vector<pb::DynipHost> DynIpStore::listHosts(string_view tenantId, string_view root) {
    const auto rootLower = normalizeLabel(root);
    auto trx = resource_.transaction();

    auto dynRoot = readProto<pb::DynipRoot>(*trx, ResourceIf::RealKey{rootLower, ResourceIf::RealKey::Class::DYNIP_ROOT});
    if (!dynRoot) {
        throw NotFoundException{"DynIP root not found"};
    }
    if (!compareCaseInsensitive(dynRoot->tenant_id(), tenantId, true)) {
        throw ConstraintException{"DynIP root belongs to another tenant"};
    }

    vector<pb::DynipHost> hosts;
    const auto hostPrefix = std::format("{}/", rootLower);
    const ResourceIf::RealKey from{hostPrefix, ResourceIf::RealKey::Class::DYNIP_HOST};
    trx->iterate(from, [&](const ResourceIf::RealKey& key, span_t value) {
        const auto raw = key.dataAsString();
        if (!startsWith(raw, hostPrefix)) {
            return false;
        }
        pb::DynipHost h;
        if (!h.ParseFromArray(value.data(), static_cast<int>(value.size()))) {
            throw InternalErrorException{"Failed to parse DynIP host"};
        }
        hosts.push_back(std::move(h));
        return true;
    }, ResourceIf::Category::ACCOUNT);

    return hosts;
}

bool DynIpStore::deleteHost(string_view tenantId, string_view root, string_view host) {
    const auto rootLower = normalizeLabel(root);
    const auto hostLower = normalizeLabel(host);
    auto trx = resource_.transaction();

    auto dynRoot = readProto<pb::DynipRoot>(*trx, ResourceIf::RealKey{rootLower, ResourceIf::RealKey::Class::DYNIP_ROOT});
    if (!dynRoot) {
        return false;
    }
    if (!compareCaseInsensitive(dynRoot->tenant_id(), tenantId, true)) {
        throw ConstraintException{"DynIP root belongs to another tenant"};
    }

    const auto hostKeyData = makeHostKey(rootLower, hostLower);
    const ResourceIf::RealKey hostKey{hostKeyData, ResourceIf::RealKey::Class::DYNIP_HOST};
    auto dynHost = readProto<pb::DynipHost>(*trx, hostKey);
    if (!dynHost) {
        return false;
    }

    trx->remove(ResourceIf::RealKey{dynHost->token_hash(), ResourceIf::RealKey::Class::DYNIP_TOKEN},
                false, ResourceIf::Category::ACCOUNT);
    trx->remove(hostKey, false, ResourceIf::Category::ACCOUNT);
    trx->commit();
    return true;
}

optional<pb::DynipHost> DynIpStore::lookupHost(string_view root, string_view host) {
    auto trx = resource_.transaction();
    return readProto<pb::DynipHost>(*trx,
                                    ResourceIf::RealKey{makeHostKey(normalizeLabel(root), normalizeLabel(host)),
                                                        ResourceIf::RealKey::Class::DYNIP_HOST});
}

optional<pb::DynipTokenIndex> DynIpStore::lookupByToken(string_view token) {
    auto trx = resource_.transaction();
    const auto tokenHash = sha256(token);
    return readProto<pb::DynipTokenIndex>(*trx,
                                          ResourceIf::RealKey{tokenHash, ResourceIf::RealKey::Class::DYNIP_TOKEN});
}

} // namespace nsblast::lib
