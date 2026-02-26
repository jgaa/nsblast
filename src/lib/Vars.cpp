#include <algorithm>
#include <charconv>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "nsblast/Vars.h"

#include "RocksDbResource.h"
#include "nsblast/ResourceIf.h"
#include "nsblast/Server.h"
#include "nsblast/errors.h"
#include "nsblast/logging.h"
#include "nsblast/util.h"

using namespace std;

namespace nsblast::lib {

namespace {

constexpr string_view kSnapshotKey{"vars/snapshot"};
constexpr uint32_t kSchemaVersion = 1;
constexpr uint32_t kTtlMax = 2147483647;

constexpr auto keyClass() {
    return ResourceIf::RealKey::Class::META;
}

constexpr auto category() {
    return ResourceIf::Category::ACCOUNT;
}

string_view trimView(string_view in) {
    constexpr auto ws = " \t\n\r\f\v";
    const auto begin = in.find_first_not_of(ws);
    if (begin == string_view::npos) {
        return {};
    }
    const auto end = in.find_last_not_of(ws);
    return in.substr(begin, end - begin + 1);
}

string normalize(string_view input) {
    return toLower(trimView(input));
}

} // namespace

const vector<Vars::Def>& Vars::defs() {
    static const vector<Def> all = {
        {"schema_version", ValueType::UINT32, true, false, "Schema version of VarsSnapshot."},
        {"pv_bootstrapped", ValueType::BOOL, true, false, "Permanent variables bootstrap marker."},
        {"cluster_role", ValueType::STRING, true, true, "Cluster role for this instance."},

        {"dynip_enabled", ValueType::BOOL, false, false, "Enable DynIP feature."},
        {"dynip_realm", ValueType::STRING, false, false, "DynIP realm/FQDN."},
        {"dynip_max_hosts_per_root", ValueType::UINT32, false, false, "Max DynIP hosts under a root."},
        {"dynip_default_ttl", ValueType::UINT32, false, false, "Default DynIP TTL."},
        {"dynip_min_ttl", ValueType::UINT32, false, false, "Minimum DynIP TTL."},
        {"dynip_max_ttl", ValueType::UINT32, false, false, "Maximum DynIP TTL."},
        {"dynip_allow_txt", ValueType::BOOL, false, false, "Allow DynIP TXT records."},

        {"dynip_enable_get", ValueType::BOOL, false, false, "Enable legacy GET /nic/update."},
        {"dynip_enable_post_json", ValueType::BOOL, false, false, "Enable JSON POST /nic/update."},
        {"dynip_allow_partial_multi_host", ValueType::BOOL, false, false, "Allow partial multi-host DynIP updates."},
        {"dynip_max_hosts_per_request", ValueType::UINT32, false, false, "Maximum hostnames per DynIP request."},
        {"dynip_require_user_agent", ValueType::BOOL, false, false, "Require User-Agent in DynIP requests."},
        {"dynip_allow_private_ips", ValueType::BOOL, false, false, "Allow private IP updates through DynIP."}
    };
    return all;
}

Vars::Vars(Server& server)
    : server_{server}
    , defaults_{defaults(server.config())}
    , snapshot_{make_shared<pb::VarsSnapshot>(defaults_)} {
}

pb::VarsSnapshot Vars::defaults(const Config& config) {
    pb::VarsSnapshot snap;
    snap.set_schema_version(kSchemaVersion);
    snap.set_pv_bootstrapped(false);

    snap.set_dynip_enabled(false);
    snap.set_dynip_realm("");
    snap.set_dynip_max_hosts_per_root(5);
    snap.set_dynip_default_ttl(300);
    snap.set_dynip_min_ttl(60);
    snap.set_dynip_max_ttl(3600);
    snap.set_dynip_allow_txt(false);

    snap.set_dynip_enable_get(config.dynip_enable_get);
    snap.set_dynip_enable_post_json(config.dynip_enable_post_json);
    snap.set_dynip_allow_partial_multi_host(config.dynip_allow_partial_multi_host);
    snap.set_dynip_max_hosts_per_request(static_cast<uint32_t>(config.dynip_max_hosts_per_request));
    snap.set_dynip_require_user_agent(config.dynip_require_user_agent);
    snap.set_dynip_allow_private_ips(config.dynip_allow_private_ips);

    return snap;
}

void Vars::validate(const pb::VarsSnapshot& snap) {
    if (snap.schema_version() == 0) {
        throw Error{2, "schema_version must be greater than zero"};
    }

    if (snap.dynip_min_ttl() == 0 || snap.dynip_default_ttl() == 0 || snap.dynip_max_ttl() == 0) {
        throw Error{2, "DynIP TTL values must be greater than zero"};
    }

    if (snap.dynip_min_ttl() > snap.dynip_default_ttl()
        || snap.dynip_default_ttl() > snap.dynip_max_ttl()) {
        throw Error{2, "dynip_min_ttl <= dynip_default_ttl <= dynip_max_ttl is required"};
    }

    if (snap.dynip_min_ttl() > kTtlMax
        || snap.dynip_default_ttl() > kTtlMax
        || snap.dynip_max_ttl() > kTtlMax) {
        throw Error{2, "DynIP TTL values exceed DNS bounds"};
    }

    if (snap.dynip_max_hosts_per_root() == 0) {
        throw Error{2, "dynip_max_hosts_per_root must be greater than zero"};
    }

    if (snap.dynip_max_hosts_per_request() == 0) {
        throw Error{2, "dynip_max_hosts_per_request must be greater than zero"};
    }

    if (snap.has_cluster_role()) {
        const auto role = normalize(snap.cluster_role());
        if (role != "none" && role != "primary" && role != "follower") {
            throw Error{2, "cluster_role must be one of: none, primary, follower"};
        }
    }

    if (snap.dynip_enabled()) {
        if (trimView(snap.dynip_realm()).empty()) {
            throw Error{2, "dynip_realm must be set when dynip_enabled=true"};
        }
        if (!validateFqdn(snap.dynip_realm())) {
            throw Error{2, "dynip_realm must be a valid FQDN"};
        }
    }
}

void Vars::validateRequired(const pb::VarsSnapshot& snap) {
    if (!snap.has_cluster_role() || trimView(snap.cluster_role()).empty()) {
        LOG_ERROR << "Missing required variable: cluster_role";
        throw Error{4, "Missing required variable: cluster_role"};
    }

    const auto role = normalize(snap.cluster_role());
    if (role != "primary" && role != "follower" && role != "none") {
        LOG_ERROR << "Invalid cluster_role: " << snap.cluster_role()
                  << " (valid: primary, follower, none)";
        throw Error{2, "Invalid cluster_role (valid: primary, follower, none)"};
    }
}

optional<pb::VarsSnapshot> Vars::readSnapshot() const {
    auto trx = server_.resource().transaction();
    ResourceIf::RealKey key{kSnapshotKey, keyClass()};
    string raw;
    if (!trx->read(key, raw, category(), false)) {
        return {};
    }

    pb::VarsSnapshot snap;
    if (!snap.ParseFromArray(raw.data(), static_cast<int>(raw.size()))) {
        throw Error{5, "vars/snapshot is corrupt"};
    }

    return snap;
}

void Vars::persistSnapshot(const pb::VarsSnapshot& snap) {
    auto trx = server_.resource().transaction();
    ResourceIf::RealKey key{kSnapshotKey, keyClass()};
    string raw;
    if (!snap.SerializeToString(&raw)) {
        throw Error{5, "Failed to serialize VarsSnapshot"};
    }

    trx->write(key, raw, false, category());
    trx->commit();
}

void Vars::setSnapshot(pb::VarsSnapshot snap) {
    auto ptr = make_shared<pb::VarsSnapshot>(std::move(snap));
    std::atomic_store_explicit(&snapshot_, std::const_pointer_cast<const pb::VarsSnapshot>(ptr), std::memory_order_release);
}

void Vars::loadOrCreateForUpgrade() {
    if (auto existing = readSnapshot()) {
        validate(*existing);
        setSnapshot(std::move(*existing));
        return;
    }

    auto created = defaults_;
    created.set_pv_bootstrapped(true);
    validate(created);
    persistSnapshot(created);
    setSnapshot(std::move(created));

    LOG_INFO << "Initialized vars/snapshot with defaults for an existing database";
}

void Vars::bootstrap(string_view clusterRole,
                     const vector<string>& overrides) {
    if (clusterRole.empty()) {
        throw Error{2, "bootstrap requires --cluster-role"};
    }

    if (readSnapshot()) {
        throw Error{5, "vars/snapshot already exists; bootstrap is only allowed for new databases"};
    }

    pb::VarsSnapshot snap = defaults_;
    snap.set_pv_bootstrapped(true);
    snap.set_cluster_role(normalize(clusterRole));

    for (const auto& assign : overrides) {
        const auto [name, value] = splitAssignment(assign);
        const auto* def = [&]() -> const Def* {
            const auto& all = defs();
            auto it = find_if(all.begin(), all.end(), [&name](const auto& v) { return v.name == name; });
            return it == all.end() ? nullptr : &*it;
        }();
        if (!def) {
            throw Error{3, format("Unknown variable '{}'", name)};
        }
        apply(snap, name, parseStringValue(value, def->type), false, defaults_, false, false);
    }

    validate(snap);
    validateRequired(snap);
    persistSnapshot(snap);
    setSnapshot(std::move(snap));
}

void Vars::ensureRequiredForRuntime() const {
    const auto snap = snapshot();
    validateRequired(*snap);
}

shared_ptr<const pb::VarsSnapshot> Vars::snapshot() const {
    return std::atomic_load_explicit(&snapshot_, std::memory_order_acquire);
}

pair<string, string> Vars::splitAssignment(string_view assignment) {
    const auto pos = assignment.find('=');
    if (pos == string_view::npos || pos == 0 || pos + 1 >= assignment.size()) {
        throw Error{2, "Expected assignment on the form name=value"};
    }
    return {toLower(assignment.substr(0, pos)), string{assignment.substr(pos + 1)}};
}

boost::json::value Vars::parseStringValue(string_view raw, ValueType type) {
    switch (type) {
    case ValueType::BOOL: {
        const auto folded = toLower(trimView(raw));
        if (folded == "true" || folded == "1" || folded == "yes" || folded == "on") {
            return true;
        }
        if (folded == "false" || folded == "0" || folded == "no" || folded == "off") {
            return false;
        }
        throw Error{2, format("Invalid bool value '{}'", raw)};
    }
    case ValueType::UINT32: {
        uint32_t value = 0;
        const auto txt = trimView(raw);
        const auto [ptr, ec] = std::from_chars(txt.data(), txt.data() + txt.size(), value);
        if (ec != std::errc{} || ptr != txt.data() + txt.size()) {
            throw Error{2, format("Invalid integer value '{}'", raw)};
        }
        return static_cast<int64_t>(value);
    }
    case ValueType::STRING:
        return boost::json::value{string{raw}};
    }

    throw Error{2, "Unknown variable type"};
}

bool Vars::hasExplicitValue(const pb::VarsSnapshot& snap, string_view name) {
    if (name == "cluster_role") {
        return snap.has_cluster_role();
    }
    return true;
}

bool Vars::isNonMutableSet(const pb::VarsSnapshot& snap, string_view name) {
    if (name == "cluster_role") {
        return snap.has_cluster_role() && !trimView(snap.cluster_role()).empty();
    }
    return hasExplicitValue(snap, name);
}

void Vars::apply(pb::VarsSnapshot& snap,
                 string_view name,
                 const boost::json::value& value,
                 bool isUnset,
                 const pb::VarsSnapshot& defaultValues,
                 bool force,
                 bool allowForce) {
    const auto lower = toLower(name);

    const auto* def = [&]() -> const Def* {
        const auto& all = defs();
        auto it = find_if(all.begin(), all.end(), [&lower](const auto& v) { return v.name == lower; });
        return it == all.end() ? nullptr : &*it;
    }();

    if (!def) {
        throw Error{3, format("Unknown variable '{}'", name)};
    }

    if (def->non_mutable && isNonMutableSet(snap, lower)) {
        if (!(force && allowForce)) {
            throw Error{2, format("Variable '{}' is non-mutable", name)};
        }
    }

    const auto set_bool = [&value](bool* dst) {
        if (!value.is_bool()) {
            throw Error{2, "Expected boolean value"};
        }
        *dst = value.as_bool();
    };

    const auto set_u32 = [&value](uint32_t* dst) {
        if (!value.is_int64()) {
            throw Error{2, "Expected integer value"};
        }
        const auto v = value.as_int64();
        if (v < 0 || v > numeric_limits<uint32_t>::max()) {
            throw Error{2, "Integer value is out of range"};
        }
        *dst = static_cast<uint32_t>(v);
    };

    const auto set_string = [&value](string* dst) {
        if (!value.is_string()) {
            throw Error{2, "Expected string value"};
        }
        *dst = string{value.as_string()};
    };

    if (lower == "schema_version") {
        auto tmp = defaultValues.schema_version();
        if (!isUnset) {
            set_u32(&tmp);
        }
        snap.set_schema_version(tmp);
        return;
    }
    if (lower == "pv_bootstrapped") {
        auto tmp = defaultValues.pv_bootstrapped();
        if (!isUnset) {
            set_bool(&tmp);
        }
        snap.set_pv_bootstrapped(tmp);
        return;
    }
    if (lower == "cluster_role") {
        if (isUnset) {
            snap.clear_cluster_role();
            return;
        }
        string tmp;
        set_string(&tmp);
        snap.set_cluster_role(normalize(tmp));
        return;
    }

    if (lower == "dynip_enabled") {
        auto tmp = defaultValues.dynip_enabled();
        if (!isUnset) set_bool(&tmp);
        snap.set_dynip_enabled(tmp);
        return;
    }
    if (lower == "dynip_realm") {
        auto tmp = defaultValues.dynip_realm();
        if (!isUnset) set_string(&tmp);
        snap.set_dynip_realm(tmp);
        return;
    }
    if (lower == "dynip_max_hosts_per_root") {
        auto tmp = defaultValues.dynip_max_hosts_per_root();
        if (!isUnset) set_u32(&tmp);
        snap.set_dynip_max_hosts_per_root(tmp);
        return;
    }
    if (lower == "dynip_default_ttl") {
        auto tmp = defaultValues.dynip_default_ttl();
        if (!isUnset) set_u32(&tmp);
        snap.set_dynip_default_ttl(tmp);
        return;
    }
    if (lower == "dynip_min_ttl") {
        auto tmp = defaultValues.dynip_min_ttl();
        if (!isUnset) set_u32(&tmp);
        snap.set_dynip_min_ttl(tmp);
        return;
    }
    if (lower == "dynip_max_ttl") {
        auto tmp = defaultValues.dynip_max_ttl();
        if (!isUnset) set_u32(&tmp);
        snap.set_dynip_max_ttl(tmp);
        return;
    }
    if (lower == "dynip_allow_txt") {
        auto tmp = defaultValues.dynip_allow_txt();
        if (!isUnset) set_bool(&tmp);
        snap.set_dynip_allow_txt(tmp);
        return;
    }
    if (lower == "dynip_enable_get") {
        auto tmp = defaultValues.dynip_enable_get();
        if (!isUnset) set_bool(&tmp);
        snap.set_dynip_enable_get(tmp);
        return;
    }
    if (lower == "dynip_enable_post_json") {
        auto tmp = defaultValues.dynip_enable_post_json();
        if (!isUnset) set_bool(&tmp);
        snap.set_dynip_enable_post_json(tmp);
        return;
    }
    if (lower == "dynip_allow_partial_multi_host") {
        auto tmp = defaultValues.dynip_allow_partial_multi_host();
        if (!isUnset) set_bool(&tmp);
        snap.set_dynip_allow_partial_multi_host(tmp);
        return;
    }
    if (lower == "dynip_max_hosts_per_request") {
        auto tmp = defaultValues.dynip_max_hosts_per_request();
        if (!isUnset) set_u32(&tmp);
        snap.set_dynip_max_hosts_per_request(tmp);
        return;
    }
    if (lower == "dynip_require_user_agent") {
        auto tmp = defaultValues.dynip_require_user_agent();
        if (!isUnset) set_bool(&tmp);
        snap.set_dynip_require_user_agent(tmp);
        return;
    }
    if (lower == "dynip_allow_private_ips") {
        auto tmp = defaultValues.dynip_allow_private_ips();
        if (!isUnset) set_bool(&tmp);
        snap.set_dynip_allow_private_ips(tmp);
        return;
    }

    throw Error{3, format("Unknown variable '{}'", name)};
}

boost::json::value Vars::getValue(const pb::VarsSnapshot& snap, string_view name) {
    const auto lower = toLower(name);
    if (lower == "schema_version") return static_cast<int64_t>(snap.schema_version());
    if (lower == "pv_bootstrapped") return snap.pv_bootstrapped();
    if (lower == "cluster_role") return snap.has_cluster_role() ? boost::json::value{snap.cluster_role()} : boost::json::value{};

    if (lower == "dynip_enabled") return snap.dynip_enabled();
    if (lower == "dynip_realm") return boost::json::value{snap.dynip_realm()};
    if (lower == "dynip_max_hosts_per_root") return static_cast<int64_t>(snap.dynip_max_hosts_per_root());
    if (lower == "dynip_default_ttl") return static_cast<int64_t>(snap.dynip_default_ttl());
    if (lower == "dynip_min_ttl") return static_cast<int64_t>(snap.dynip_min_ttl());
    if (lower == "dynip_max_ttl") return static_cast<int64_t>(snap.dynip_max_ttl());
    if (lower == "dynip_allow_txt") return snap.dynip_allow_txt();

    if (lower == "dynip_enable_get") return snap.dynip_enable_get();
    if (lower == "dynip_enable_post_json") return snap.dynip_enable_post_json();
    if (lower == "dynip_allow_partial_multi_host") return snap.dynip_allow_partial_multi_host();
    if (lower == "dynip_max_hosts_per_request") return static_cast<int64_t>(snap.dynip_max_hosts_per_request());
    if (lower == "dynip_require_user_agent") return snap.dynip_require_user_agent();
    if (lower == "dynip_allow_private_ips") return snap.dynip_allow_private_ips();

    throw Error{3, format("Unknown variable '{}'", name)};
}

boost::json::value Vars::getDefaultValue(const pb::VarsSnapshot& snap, string_view name) {
    return getValue(snap, name);
}

vector<Vars::Item> Vars::list() const {
    const auto snap = snapshot();
    vector<Item> items;
    items.reserve(defs().size());
    for (const auto& def : defs()) {
        Item item;
        item.name = string{def.name};
        item.value = getValue(*snap, def.name);
        item.default_value = getDefaultValue(defaults_, def.name);
        item.non_mutable = def.non_mutable;
        item.requires_restart = def.requires_restart;
        item.description = string{def.description};
        items.emplace_back(std::move(item));
    }
    return items;
}

optional<Vars::Item> Vars::get(string_view name) const {
    const auto lower = toLower(name);
    for (const auto& def : defs()) {
        if (def.name == lower) {
            Item item;
            const auto snap = snapshot();
            item.name = string{def.name};
            item.value = getValue(*snap, def.name);
            item.default_value = getDefaultValue(defaults_, def.name);
            item.non_mutable = def.non_mutable;
            item.requires_restart = def.requires_restart;
            item.description = string{def.description};
            return item;
        }
    }
    return {};
}

void Vars::set(string_view name,
               const boost::json::value& value,
               bool force,
               bool allowForce) {
    auto next = *snapshot();
    apply(next, name, value, false, defaults_, force, allowForce);
    validate(next);
    persistSnapshot(next);
    setSnapshot(std::move(next));
}

void Vars::setFromAssignment(string_view assignment,
                             bool force,
                             bool allowForce) {
    const auto [name, raw] = splitAssignment(assignment);

    const auto* def = [&]() -> const Def* {
        const auto& all = defs();
        auto it = find_if(all.begin(), all.end(), [&name](const auto& v) { return v.name == name; });
        return it == all.end() ? nullptr : &*it;
    }();

    if (!def) {
        throw Error{3, format("Unknown variable '{}'", name)};
    }

    set(name, parseStringValue(raw, def->type), force, allowForce);
}

void Vars::unset(string_view name,
                 bool force,
                 bool allowForce) {
    auto next = *snapshot();
    apply(next, name, boost::json::value{}, true, defaults_, force, allowForce);
    validate(next);
    persistSnapshot(next);
    setSnapshot(std::move(next));
}

} // namespace nsblast::lib
