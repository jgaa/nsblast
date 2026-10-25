#include "MailProvisioner.h"
#include "nsblast/Vars.h"
#include "nsblast/logging.h"
#include <openssl/evp.h>
#include <openssl/x509.h>
#include <boost/asio/ip/address.hpp>
#include <charconv>
#include <sstream>
#include <cctype>

namespace nsblast::lib {
namespace {
namespace json = boost::json;
using Object = json::object;
using Array = json::array;
using Class = ResourceIf::RealKey::Class;
using Method = yahat::Request::Type;
constexpr auto account = ResourceIf::Category::ACCOUNT;
struct Error : std::runtime_error {
    int code;
    Error(int status, std::string message) : runtime_error{std::move(message)}, code{status} {}
};
std::string str(const Object& o, std::string_view k, std::string_view fallback = {}) {
    const auto* v = o.if_contains(k);
    if (!v) return std::string{fallback};
    if (!v->is_string()) throw Error{400, std::string{k} + " must be a string"};
    return std::string{v->as_string()};
}
int64_t num(const Object& o, std::string_view k, int64_t fallback, int64_t low, int64_t high) {
    const auto* v = o.if_contains(k);
    if (!v) return fallback;
    if (!v->is_int64() || v->as_int64() < low || v->as_int64() > high) throw Error{400, std::string{k} + " out of range"};
    return v->as_int64();
}
bool flag(const Object& o, std::string_view k, bool fallback) {
    const auto* v = o.if_contains(k);
    if (!v) return fallback;
    if (!v->is_bool()) throw Error{400, std::string{k} + " must be boolean"};
    return v->as_bool();
}
Object section(const Object& o, std::string_view k) {
    const auto* v = o.if_contains(k);
    if (!v) return {};
    if (!v->is_object()) throw Error{400, std::string{k} + " must be an object"};
    return v->as_object();
}
void fields(const Object& o, std::initializer_list<std::string_view> allowed) {
    for (const auto& member : o) {
        if (std::find(allowed.begin(), allowed.end(), member.key()) == allowed.end()) throw Error{400, "Unknown field: " + std::string{member.key()}};
    }
}
std::string name(std::string_view raw, bool underscores = false) {
    auto result = toLower(raw);
    if (result.ends_with('.')) result.pop_back();
    if (result.empty() || result.ends_with('.') || result.size() > 253) throw Error{400, "Invalid DNS name"};
    size_t begin = 0;
    while (begin < result.size()) {
        auto end = result.find('.', begin);
        if (end == std::string::npos) end = result.size();
        const auto label = std::string_view{result}.substr(begin, end - begin);
        if (label.empty() || label.size() > 63 || label.front() == '-' || label.back() == '-') throw Error{400, "Invalid DNS label"};
        for (unsigned char c : label) if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || (underscores && c == '_'))) throw Error{400, "Invalid DNS name character"};
        begin = end + 1;
    }
    return result;
}
std::string trimText(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == text.npos) return {};
    return std::string{text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1)};
}
std::map<std::string, std::string> tags(std::string_view text) {
    std::map<std::string, std::string> result;
    while (!text.empty()) {
        const auto end = text.find(';');
        const auto part = text.substr(0, end);
        if (auto eq = part.find('='); eq != part.npos) {
            auto key = toLower(trimText(part.substr(0, eq)));
            if (!result.emplace(key, trimText(part.substr(eq + 1))).second) return {};
        } else if (!trimText(part).empty()) return {};
        if (end == text.npos) break;
        text.remove_prefix(end + 1);
    }
    return result;
}
std::string purpose(std::string_view owner, const Rr& rr, const std::string& domain) {
    if (owner == domain && rr.type() == TYPE_MX) return "mx";
    if (rr.type() != TYPE_TXT) return {};
    auto value = RrTxt{rr.span(), rr.offset()}.string();
    const auto lower = toLower(trimText(value));
    if (owner == domain && (lower == "v=spf1" || lower.starts_with("v=spf1 "))) return "spf";
    const auto parsed = tags(value);
    const auto version = parsed.find("v");
    auto is_version = [&](std::string_view v) { return version != parsed.end() && compareCaseInsensitive(version->second, v); };
    if (owner == "_dmarc." + domain && is_version("DMARC1")) return "dmarc";
    if (owner == "_mta-sts." + domain && is_version("STSv1")) return "mta_sts";
    if (owner == "_smtp._tls." + domain && is_version("TLSRPTv1")) return "tls_reporting";
    if (owner.ends_with("._domainkey." + domain)) {
        if (is_version("DKIM1")) return "dkim";
        if (version == parsed.end() && parsed.contains("p")) {
            const auto key_type = parsed.find("k");
            if (key_type != parsed.end() && key_type->second != "rsa" && key_type->second != "ed25519") return {};
            std::string encoded;
            for (unsigned char c : parsed.at("p")) if (!std::isspace(c)) encoded += c;
            bool valid = encoded.size() % 4 == 0;
            const auto padding = encoded.find('=');
            for (size_t i = 0; i < encoded.size(); ++i) {
                const auto c = static_cast<unsigned char>(encoded[i]);
                valid = valid && (std::isalnum(c) || c == '+' || c == '/' || (c == '=' && i >= encoded.size() - 2));
                if (padding != encoded.npos && i > padding && c != '=') valid = false;
            }
            if (valid) return "dkim"; // Includes the empty/revoked public key.
        }
    }
    return {};
}
std::string oldMxHost(const Rr& rr) {
    try { return name(RrMx{rr.span(), rr.offset()}.host().string()); }
    catch (const Error&) { return {}; } // Null/malformed MX still belongs to mail, but owns no safe address target.
}
std::string buffer(const Entry& e) { auto b = e.buffer(); return {b.data(), b.size()}; }
std::string buffer(const StorageBuilder& b) { auto s = b.buffer(); return {s.data(), s.size()}; }
Object load(ResourceIf::TransactionIf& trx, std::string_view domain) {
    std::string raw;
    if (!trx.read({domain, Class::MAIL_POLICY}, raw, account, false)) return {};
    auto result = json::parse(raw).as_object();
    if (num(result, "version", 0, 0, 100) != 1) throw Error{409, "Unsupported mail policy version"};
    return result;
}
void save(ResourceIf::TransactionIf& trx, const std::string& domain, const Object& metadata) {
    if (metadata.empty()) {
        if (trx.keyExists({domain, Class::MAIL_POLICY}, account)) trx.remove({domain, Class::MAIL_POLICY}, false, account);
    } else trx.write({domain, Class::MAIL_POLICY}, json::serialize(metadata), false, account);
}
std::string policyId(const Object& policy) {
    // sha256 is Base64 in this project. Encode the digest as RFC 8461 alphanumeric hex.
    const auto serialized = json::serialize(policy);
    unsigned char digest[EVP_MAX_MD_SIZE]; unsigned int size = 0;
    if (EVP_Digest(serialized.data(), serialized.size(), digest, &size, EVP_sha256(), nullptr) != 1 || size < 16) throw std::runtime_error{"Policy digest failed"};
    constexpr char hex[] = "0123456789abcdef";
    std::string result;
    for (size_t i = 0; i < 16; ++i) { result += hex[digest[i] >> 4]; result += hex[digest[i] & 15]; }
    return result;
}
json::value canonical(const json::value& value) {
    if (value.is_object()) {
        std::map<std::string, json::value> sorted;
        for (const auto& pair : value.as_object()) sorted.emplace(std::string{pair.key()}, canonical(pair.value()));
        Object result;
        for (const auto& [key, item] : sorted) result[key] = item;
        return result;
    }
    if (value.is_array()) { Array result; for (const auto& item : value.as_array()) result.push_back(canonical(item)); return result; }
    return value;
}
void reporting(std::string_view value, bool https = false) {
    if (value.empty() || value.size() > 2048) throw Error{400, "Reporting URI required (maximum 2048 bytes)"};
    size_t count = 0;
    while (!value.empty()) {
        const auto end = value.find(',');
        const auto uri = trimText(value.substr(0, end));
        if (++count > 8) throw Error{400, "At most eight reporting destinations"};
        if (uri.starts_with("mailto:")) {
            auto address = std::string_view{uri}.substr(7);
            const auto at = address.find('@');
            if (at == address.npos || at == 0 || address.find('@', at + 1) != address.npos) throw Error{400, "Invalid reporting mailbox"};
            name(address.substr(at + 1));
        } else if (https && uri.starts_with("https://")) {
            const auto host = std::string_view{uri}.substr(8);
            name(host.substr(0, host.find('/')));
        } else throw Error{400, "Unsupported reporting URI"};
        for (unsigned char c : uri) if (c <= 32 || c >= 127 || c == ';' || c == '"' || c == '\\') throw Error{400, "Invalid reporting URI character"};
        if (end == value.npos) break;
        value.remove_prefix(end + 1);
        if (value.empty()) throw Error{400, "Empty reporting URI"};
    }
}
void spf(std::string_view policy) {
    if (policy.size() > 4096 || !toLower(policy).starts_with("v=spf1 ")) throw Error{400, "SPF must start with v=spf1 and be at most 4096 bytes"};
    for (unsigned char c : policy) if (c < 32 || c >= 127) throw Error{400, "Invalid SPF character"};
    std::istringstream input{std::string{policy.substr(7)}};
    std::string token; size_t lookups = 0; std::set<std::string> modifiers;
    while (input >> token) {
        std::string_view term{token};
        if (auto eq = term.find('='); eq != term.npos) {
            const auto modifier = toLower(term.substr(0, eq));
            if (!modifiers.insert(modifier).second || (modifier != "redirect" && modifier != "exp")) throw Error{400, "Unsupported/duplicate SPF modifier"};
            name(term.substr(eq + 1));
            if (modifier == "redirect") ++lookups;
            continue;
        }
        if (term.starts_with('+') || term.starts_with('-') || term.starts_with('~') || term.starts_with('?')) term.remove_prefix(1);
        const auto split = term.find_first_of(":/");
        const auto mechanism = toLower(term.substr(0, split));
        auto rest = split == term.npos ? std::string_view{} : term.substr(split);
        if (mechanism == "all") { if (!rest.empty()) throw Error{400, "Invalid SPF all"}; }
        else if (mechanism == "include" || mechanism == "exists") {
            if (!rest.starts_with(':')) throw Error{400, "SPF mechanism requires domain"};
            name(rest.substr(1)); ++lookups;
        } else if (mechanism == "ip4" || mechanism == "ip6") {
            if (!rest.starts_with(':')) throw Error{400, "SPF mechanism requires address"};
            rest.remove_prefix(1);
            const auto slash = rest.find('/');
            boost::system::error_code ec;
            const auto address = boost::asio::ip::make_address(std::string{rest.substr(0, slash)}, ec);
            if (ec || (address.is_v4() != (mechanism == "ip4"))) throw Error{400, "Invalid SPF IP address"};
            if (slash != rest.npos) {
                unsigned prefix = 0; auto digits = rest.substr(slash + 1);
                auto [ptr, err] = std::from_chars(digits.data(), digits.data() + digits.size(), prefix);
                if (err != std::errc{} || ptr != digits.data() + digits.size() || prefix > (address.is_v4() ? 32 : 128)) throw Error{400, "Invalid SPF IP prefix"};
            }
        } else if (mechanism == "a" || mechanism == "mx" || mechanism == "ptr") {
            ++lookups;
            if (rest.starts_with(':')) { rest.remove_prefix(1); const auto slash = rest.find('/'); name(rest.substr(0, slash)); rest = slash == rest.npos ? std::string_view{} : rest.substr(slash); }
            if (mechanism == "ptr" && !rest.empty()) throw Error{400, "Invalid SPF ptr"};
            if (!rest.empty()) {
                // SPF dual-CIDR: /v4, //v6, or /v4//v6.
                if (!rest.starts_with('/')) throw Error{400, "Invalid SPF CIDR"};
                rest.remove_prefix(1);
                auto parse_prefix = [](std::string_view v, unsigned max) { unsigned n = 0; auto [p, e] = std::from_chars(v.data(), v.data()+v.size(), n); if (v.empty() || e != std::errc{} || p != v.data()+v.size() || n > max) throw Error{400, "Invalid SPF CIDR"}; };
                if (rest.starts_with('/')) { rest.remove_prefix(1); parse_prefix(rest, 128); }
                else { auto dual = rest.find("//"); parse_prefix(rest.substr(0, dual), 32); if (dual != rest.npos) parse_prefix(rest.substr(dual + 2), 128); }
            }
        } else throw Error{400, "Unsupported SPF mechanism"};
    }
    if (lookups > 10) throw Error{400, "SPF contains more than ten DNS lookup terms"};
}
std::string rsaKey(std::string_view encoded) {
    if (encoded.empty() || encoded.size() > 2048 || encoded.size() % 4 != 0) throw Error{400, "DKIM key must be Base64 DER RSAPublicKey"};
    for (unsigned char c : encoded) if (!(std::isalnum(c) || c == '+' || c == '/' || c == '=')) throw Error{400, "Invalid DKIM Base64"};
    std::vector<unsigned char> der(encoded.size());
    const auto decoded = EVP_DecodeBlock(der.data(), reinterpret_cast<const unsigned char*>(encoded.data()), encoded.size());
    if (decoded < 0) throw Error{400, "Invalid DKIM Base64"};
    size_t padding = encoded.ends_with("==") ? 2 : encoded.ends_with('=') ? 1 : 0;
    der.resize(static_cast<size_t>(decoded) - padding);
    const unsigned char* ptr = der.data();
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key{d2i_PublicKey(EVP_PKEY_RSA, nullptr, &ptr, der.size()), EVP_PKEY_free};
    if (!key || ptr != der.data() + der.size() || EVP_PKEY_bits(key.get()) < 2048 || EVP_PKEY_bits(key.get()) > 8192) throw Error{400, "DKIM RSA key must be 2048..8192 bits in DER RSAPublicKey format"};
    std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> ctx{EVP_PKEY_CTX_new(key.get(), nullptr), EVP_PKEY_CTX_free};
    if (!ctx || EVP_PKEY_public_check(ctx.get()) != 1) throw Error{400, "Invalid DKIM RSA public key"};
    std::string canonical(4 * ((der.size() + 2) / 3), '\0');
    EVP_EncodeBlock(reinterpret_cast<unsigned char*>(canonical.data()), der.data(), der.size());
    if (canonical != encoded) throw Error{400, "Noncanonical DKIM Base64"};
    return canonical;
}
yahat::Response reply(int code, Object value) {
    return {code, "OK", json::serialize(Object{{"error", false}, {"status", code}, {"value", std::move(value)}})};
}
yahat::Response failure(const Error& error) {
    return {error.code, "Mail provisioning failed", json::serialize(Object{{"error", true}, {"status", error.code}, {"message", error.what()}})};
}
std::vector<std::string> discovered(ResourceIf::TransactionIf& trx, const std::string& domain) {
    std::vector<std::string> names{domain, "_dmarc." + domain, "_mta-sts." + domain, "_smtp._tls." + domain};
    const std::string suffix = "_domainkey." + domain;
    size_t scanned = 0;
    trx.iterate({suffix, Class::ENTRY}, [&](const auto& key, auto raw) {
        if (!key.isInZone(suffix)) return false;
        if (++scanned > 10000) throw Error{409, "DKIM discovery exceeds 10000 owners"};
        const auto owner = key.dataAsString();
        auto zone = trx.lookupEntryAndSoa(owner);
        auto main = trx.lookupEntryAndSoa(domain);
        // Preserve delegated DNS subtrees. Never treat a subzone as part of this mail domain.
        if (!zone.hasSoa() || !main.hasSoa() || zone.soa().getSoa().labels().string() != main.soa().getSoa().labels().string()) return true;
        auto ancestor = owner;
        const auto authoritative = toLower(main.soa().getSoa().labels().string());
        while (ancestor != authoritative) {
            auto node = trx.lookup(ancestor);
            for (const auto& rr : node) if (rr.type() == TYPE_NS || rr.type() == TYPE_CNAME) return true;
            const auto dot = ancestor.find('.');
            if (dot == ancestor.npos) break;
            ancestor.erase(0, dot + 1);
        }
        Entry entry{raw};
        for (const auto& rr : entry) if (purpose(owner, rr, domain) == "dkim") { if (names.size() >= 132) throw Error{409, "At most 128 discovered DKIM selectors per operation"}; names.push_back(owner); break; }
        return true;
    });
    return names;
}
std::string entryFingerprint(const Entry& entry);
bool exclusiveHost(ResourceIf::TransactionIf& trx, const std::string& host, const std::string& domain, const Session& session) {
    std::string raw;
    trx.lock({host, Class::MAIL_HOST}, account);
    if (!trx.read({host, Class::MAIL_HOST}, raw, account, false)) return false;
    auto claim = json::parse(raw).as_object();
    if (num(claim, "version", 0, 0, 100) != 1 || str(claim, "domain") != domain || str(claim, "tenant") != session.tenant()
        || str(claim, "fingerprint") != entryFingerprint(trx.lookup(host))) return false;
    const auto prefix = host + "/";
    bool shared = false;
    trx.iterate({prefix, Class::DNS_REF}, [&](const auto& key, auto value) {
        const auto text = key.dataAsString();
        if (!text.starts_with(prefix)) return false;
        if (text.substr(prefix.size()) != domain || std::string_view{value.data(), value.size()} != "mx") { shared = true; return false; }
        return true;
    }, account);
    return !shared;
}
void claimHosts(ResourceIf::TransactionIf& trx, const std::set<std::string>& hosts, const std::string& domain, const Session& session) {
    for (const auto& host : hosts) {
        const auto prefix = host + "/";
        bool shared = false;
        trx.iterate({prefix, Class::DNS_REF}, [&](const auto& key, auto value) {
            const auto text = key.dataAsString();
            if (!text.starts_with(prefix)) return false;
            if (text.substr(prefix.size()) != domain || std::string_view{value.data(), value.size()} != "mx") { shared = true; return false; }
            return true;
        }, account);
        if (shared) continue;
        Object claim{{"version", 1}, {"domain", domain}, {"tenant", session.tenant()}, {"fingerprint", entryFingerprint(trx.lookup(host))}};
        trx.write({host, Class::MAIL_HOST}, json::serialize(claim), false, account);
    }
}
} // namespace

struct MailProvisioner::Plan {
    RestApi::DnsChanges changes;
    Object policy;
    std::set<std::string> claim_hosts;
    Object old_metadata;
    Object report;
    pb::Permission permission;
    bool existing = false;
};
MailProvisioner::MailProvisioner(Server& server) : server_{server} {}

std::shared_ptr<void> MailProvisioner::lockZones(ResourceIf::TransactionIf& trx, const std::string& domain, const Object& input) {
    std::set<std::string> names{domain, "_dmarc." + domain, "_mta-sts." + domain, "_smtp._tls." + domain, "_domainkey." + domain, "mta-sts." + domain};
    if (const auto* servers = input.if_contains("mail_servers"); servers && servers->is_array()) {
        for (const auto& item : servers->as_array()) if (item.is_object()) names.insert(name(str(item.as_object(), "hostname")));
    }
    if (const auto* keys = input.if_contains("dkim"); keys && keys->is_array()) {
        for (const auto& item : keys->as_array()) if (item.is_object()) names.insert(name(str(item.as_object(), "selector")) + "._domainkey." + domain);
    }
    auto current = trx.lookup(domain);
    for (const auto& rr : current) if (rr.type() == TYPE_MX) { const auto host = oldMxHost(rr); if (!host.empty()) names.insert(host); }
    std::set<std::string> zones;
    for (const auto& owner : names) {
        auto entry = trx.lookupEntryAndSoa(owner);
        if (entry.hasSoa()) zones.insert(toLower(entry.soa().getSoa().labels().string()));
        else if (owner == domain) throw Error{404, "Not authoritative for mail domain"};
    }
    struct Locks { std::vector<std::shared_ptr<std::mutex>> mutexes; std::vector<std::unique_lock<std::mutex>> locks; };
    auto result = std::make_shared<Locks>();
    for (const auto& zone : zones) result->mutexes.push_back(server_.api().getRrUpdateMutex(zone));
    for (const auto& mutex : result->mutexes) result->locks.emplace_back(*mutex);
    for (const auto& owner : names) {
        auto fresh = trx.lookupEntryAndSoa(owner);
        if (fresh.hasSoa() && !zones.contains(toLower(fresh.soa().getSoa().labels().string()))) throw Error{409, "Authoritative zone changed concurrently; retry"};
    }
    return result;
}

MailProvisioner::Plan MailProvisioner::plan(ResourceIf::TransactionIf& trx, const Session& session,
    const std::string& domain, Method method, const Object& input, bool preserve_ip)
{
    Plan result;
    fields(input, {"mail_servers", "spf", "dkim", "dmarc", "mta_sts", "tls_reporting", "ttl"});
    auto root = trx.lookupEntryAndSoa(domain);
    if (!root.hasSoa()) throw Error{404, "Not authoritative for mail domain"};
    if (!root.soa().tenantId() || *root.soa().tenantId() != session.tenantId()) throw Error{403, "Not your zone"};
    const bool permitted = method == Method::POST ? session.isAllowed(pb::CREATE_MAIL, domain)
        : method == Method::DELETE ? session.isAllowed(pb::DELETE_MAIL, domain)
        : session.isAllowed(pb::CREATE_MAIL, domain) || session.isAllowed(pb::UPDATE_MAIL, domain);
    if (!permitted) throw Error{403, "Mail permission denied for domain"};
    const auto root_zone = toLower(root.soa().getSoa().labels().string());
    if (trx.keyExists({root_zone, Class::ENTRY}, ResourceIf::Category::MASTER_ZONE)) throw Error{409, "Mail provisioning cannot modify secondary DNS zones"};
    result.old_metadata = load(trx, domain);
    if (!result.old_metadata.empty() && str(result.old_metadata, "tenant") != session.tenant()) throw Error{403, "Not your mail policy"};
    auto owners = discovered(trx, domain);
    std::map<std::string, std::unique_ptr<StorageBuilder>> desired;
    std::map<std::string, std::set<uint16_t>> addresses;
    std::set<std::string> mx_hosts, old_hosts;
    const auto ttl = num(input, "ttl", server_.config().default_ttl, 1, 2147483647);
    Array warnings, created, updated, deleted;
    const auto builder = [&](const std::string& owner) -> StorageBuilder& {
        auto& ptr = desired[owner];
        if (!ptr) ptr = std::make_unique<StorageBuilder>();
        return *ptr;
    };
    const auto txt = [&](const std::string& owner, const std::string& value) { builder(owner).createTxt(owner, ttl, value); };
    const auto ips = [&](const Object& object, const std::string& owner) {
        for (const auto& [key, type] : {std::pair{"ipv4", TYPE_A}, std::pair{"ipv6", TYPE_AAAA}}) {
            if (const auto* value = object.if_contains(key)) {
                if (!value->is_array() || value->as_array().empty() || value->as_array().size() > 16) throw Error{400, "IP arrays must contain 1..16 addresses"};
                addresses[owner].insert(type);
                std::set<std::string> seen;
                for (const auto& ip : value->as_array()) {
                    if (!ip.is_string()) throw Error{400, "IP address must be a string"};
                    boost::system::error_code ec;
                    auto address = boost::asio::ip::make_address(std::string{ip.as_string()}, ec);
                    if (ec || address.is_unspecified() || address.is_multicast() || (address.is_v4() != (type == TYPE_A))) throw Error{400, "Wrong IP address family or invalid IP"};
                    if (!seen.insert(address.to_string()).second) throw Error{400, "Duplicate IP address"};
                    builder(owner).createA(owner, ttl, address);
                }
            }
        }
    };
    if (method != Method::DELETE) {
        const auto* servers = input.if_contains("mail_servers");
        if (!servers || !servers->is_array() || servers->as_array().empty() || servers->as_array().size() > 16) throw Error{400, "mail_servers must contain 1..16 hosts"};
        for (const auto& item : servers->as_array()) {
            if (!item.is_object()) throw Error{400, "Mail server must be an object"};
            const auto& host = item.as_object(); fields(host, {"hostname", "priority", "ipv4", "ipv6"});
            const auto hostname = name(str(host, "hostname"));
            if (!mx_hosts.insert(hostname).second) throw Error{400, "Duplicate MX hostname"};
            builder(domain).createMx(domain, ttl, num(host, "priority", 10, 0, 65535), hostname);
            ips(host, hostname);
        }
        auto spf_config = section(input, "spf"); fields(spf_config, {"enabled", "policy"});
        const auto spf_policy = str(spf_config, "policy", "v=spf1 mx -all");
        spf(spf_policy);
        if (flag(spf_config, "enabled", true)) txt(domain, spf_policy);
        auto dmarc = section(input, "dmarc"); fields(dmarc, {"enabled", "policy", "rua", "ruf"});
        const auto mode = str(dmarc, "policy", "none");
        if (mode != "none" && mode != "quarantine" && mode != "reject") throw Error{400, "Invalid DMARC policy"};
        auto dmarc_value = "v=DMARC1; p=" + mode;
        for (auto field : {"rua", "ruf"}) if (dmarc.contains(field)) { auto uri = str(dmarc, field); reporting(uri); dmarc_value += "; " + std::string{field} + "=" + uri; }
        if (flag(dmarc, "enabled", true)) txt("_dmarc." + domain, dmarc_value);
        if (const auto* dkim = input.if_contains("dkim")) {
            if (!dkim->is_array() || dkim->as_array().size() > 32) throw Error{400, "dkim must contain at most 32 selectors"};
            std::set<std::string> selectors;
            for (const auto& item : dkim->as_array()) {
                if (!item.is_object()) throw Error{400, "DKIM selector must be an object"};
                const auto& key = item.as_object(); fields(key, {"selector", "algorithm", "public_key"});
                const auto selector = name(str(key, "selector"));
                if (!selectors.insert(selector).second) throw Error{400, "Duplicate DKIM selector"};
                if (str(key, "algorithm", "rsa") != "rsa") throw Error{400, "Only RSA DKIM is supported"};
                const auto owner = name(selector + "._domainkey." + domain, true);
                txt(owner, "v=DKIM1; k=rsa; p=" + rsaKey(str(key, "public_key")));
            }
        }
        const auto mta = section(input, "mta_sts"); fields(mta, {"enabled", "mode", "max_age", "ipv4", "ipv6"});
        const auto mta_mode = str(mta, "mode", "testing");
        if (mta_mode != "none" && mta_mode != "testing" && mta_mode != "enforce") throw Error{400, "Invalid MTA-STS mode"};
        const auto age = num(mta, "max_age", 604800, 0, 31557600);
        if (flag(mta, "enabled", false)) {
            Array hosts; for (const auto& host : mx_hosts) hosts.push_back(json::value{host});
            result.policy = Object{{"mode", mta_mode}, {"mx", hosts}, {"max_age", age}};
            txt("_mta-sts." + domain, "v=STSv1; id=" + policyId(result.policy));
            ips(mta, "mta-sts." + domain);
        } else if (mta.contains("ipv4") || mta.contains("ipv6")) throw Error{400, "MTA-STS addresses require enabled=true"};
        const auto tls = section(input, "tls_reporting"); fields(tls, {"enabled", "rua"});
        if (tls.contains("rua")) reporting(str(tls, "rua"), true);
        if (flag(tls, "enabled", false)) { const auto uri = str(tls, "rua"); reporting(uri, true); txt("_smtp._tls." + domain, "v=TLSRPTv1; rua=" + uri); }
    }
    for (const auto& rr : root.rr()) if (rr.type() == TYPE_MX) { const auto host = oldMxHost(rr); if (!host.empty()) old_hosts.insert(host); }
    std::set<std::string> cleanup_hosts;
    if (method != Method::POST && !preserve_ip) {
        for (const auto& host : old_hosts) {
            if (host == domain || !host.ends_with("." + domain) || mx_hosts.contains(host) || addresses.contains(host)) continue;
            if (exclusiveHost(trx, host, domain, session)) { cleanup_hosts.insert(host); owners.push_back(host); }
        }
    }
    for (const auto& [owner, ptr] : desired) owners.push_back(name(owner, true));
    std::sort(owners.begin(), owners.end()); owners.erase(std::unique(owners.begin(), owners.end()), owners.end());
    // First discover recognized mail configuration. A pre-existing zone is not a mail configuration.
    for (const auto& owner : owners) {
        auto existing = trx.lookup(owner);
        for (const auto& rr : existing) {
            if (!purpose(owner, rr, domain).empty()) result.existing = true;
            if (owner == domain && rr.type() == TYPE_MX) { const auto host = oldMxHost(rr); if (!host.empty()) old_hosts.insert(host); }
        }
    }
    result.existing = result.existing || !result.old_metadata.empty();
    result.permission = method == Method::POST ? pb::CREATE_MAIL : method == Method::DELETE ? pb::DELETE_MAIL : result.existing ? pb::UPDATE_MAIL : pb::CREATE_MAIL;
    if (!session.isAllowed(result.permission, domain)) throw Error{403, "Mail permission denied for domain"};
    if (method == Method::POST && !result.old_metadata.empty()) throw Error{409, "Mail policy already exists"};
    bool old_sts = false;
    for (const auto& owner : owners) {
        auto state = trx.lookupEntryAndSoa(owner);
        const auto old = buffer(state.rr());
        auto it = desired.find(owner);
        const bool adding = it != desired.end();
        if (adding) {
            if (!state.hasSoa()) throw Error{404, "No authoritative zone for " + owner};
            if (!state.soa().tenantId() || *state.soa().tenantId() != session.tenantId()) throw Error{403, "Not your zone: " + owner};
            if (trx.keyExists({toLower(state.soa().getSoa().labels().string()), Class::ENTRY}, ResourceIf::Category::MASTER_ZONE)) throw Error{409, "Mail provisioning cannot modify secondary DNS zones"};
            if (!session.isAllowed(result.permission, owner)) throw Error{403, "Mail permission denied for " + owner};
            for (const auto& rr : state.rr()) if (rr.type() == TYPE_CNAME) throw Error{409, "CNAME conflicts with " + owner};
            // Do not cross delegated _domainkey subtrees even when another local zone exists.
            if (owner.ends_with("._domainkey." + domain) && toLower(state.soa().getSoa().labels().string()) != root_zone) throw Error{409, "DKIM selector crosses delegated zone"};
            auto ancestor = owner;
            while (ancestor != root_zone && !ancestor.empty()) {
                auto entry = trx.lookup(ancestor);
                for (const auto& rr : entry) if (rr.type() == TYPE_CNAME || rr.type() == TYPE_NS) throw Error{409, "CNAME or delegation conflicts with " + owner};
                const auto dot = ancestor.find('.'); if (dot == ancestor.npos) break; ancestor.erase(0, dot + 1);
            }
            it->second->finish();
        }
        Entry new_mail{adding ? it->second->buffer() : span_t{}};
        for (const auto& old_rr : state.rr()) {
            if (old_rr.type() != TYPE_TXT || !purpose(owner, old_rr, domain).empty()) continue;
            for (const auto& new_rr : new_mail) if (new_rr.type() == TYPE_TXT && new_rr.ttl() != old_rr.ttl()) throw Error{409, "TXT TTL conflicts with unrelated TXT at " + owner};
        }
        if (addresses.contains(owner)) {
            const bool exclusive = exclusiveHost(trx, owner, domain, session);
            if (method == Method::PUT && !exclusive) {
                for (const auto type : addresses.at(owner)) {
                    std::multiset<std::pair<uint32_t, std::string>> old_values, new_values;
                    for (const auto& rr : state.rr()) if (rr.type() == type) old_values.emplace(rr.ttl(), std::string{rr.rdata().data(), rr.rdata().size()});
                    for (const auto& rr : new_mail) if (rr.type() == type) new_values.emplace(rr.ttl(), std::string{rr.rdata().data(), rr.rdata().size()});
                    if (!old_values.empty() && old_values != new_values) throw Error{409, "Address ownership is ambiguous; use the RR API to change " + owner};
                }
            }
            if (exclusive || state.rr().empty()) result.claim_hosts.insert(owner);
        }
        StorageBuilder merged;
        if (state.hasSoa()) {
            merged.setTenantId(state.soa().tenantId());
            const auto zone = toLower(state.soa().getSoa().labels().string());
            if (zone != owner) merged.setZoneLen(state.soa().getSoa().labels().size() - 1);
        }
        bool touched = false;
        for (const auto& rr : state.rr()) {
            const auto kind = purpose(owner, rr, domain);
            const bool manage_address = (addresses.contains(owner) && addresses.at(owner).contains(rr.type()))
                || (cleanup_hosts.contains(owner) && (rr.type() == TYPE_A || rr.type() == TYPE_AAAA));
            const bool mail_record = !kind.empty();
            if (kind == "mta_sts") old_sts = true;
            if (method == Method::POST && (mail_record || manage_address)) {
                // POST conflicts only with requested mail purposes, not an unrelated TXT.
                bool targeted = manage_address;
                for (const auto& requested : new_mail) if (purpose(owner, requested, domain) == kind && !kind.empty()) targeted = true;
                if (targeted) throw Error{409, "Mail record already exists: " + owner};
            }
            const bool removing = method != Method::POST && (mail_record || manage_address);
            if (removing) {
                if (!state.soa().tenantId() || *state.soa().tenantId() != session.tenantId() || !session.isAllowed(result.permission, owner)) throw Error{403, "Mail permission denied for affected name: " + owner};
                if (trx.keyExists({toLower(state.soa().getSoa().labels().string()), Class::ENTRY}, ResourceIf::Category::MASTER_ZONE)) throw Error{409, "Mail provisioning cannot modify secondary DNS zones"};
                touched = true;
            } else merged.createRr(owner, rr.type(), rr.ttl(), rr.rdata());
        }
        for (const auto& rr : new_mail) { merged.createRr(owner, rr.type(), rr.ttl(), rr.rdata()); touched = true; }
        if (touched) {
            merged.finish();
            result.changes.emplace(owner, std::pair{old, merged.rrCount() ? buffer(merged) : std::string{}});
        }
    }
    if (old_sts && result.old_metadata.empty()) throw Error{409, "MTA-STS retirement requires known policy history; imported TXT alone is insufficient"};
    // A local MX must have usable addresses in the complete proposed state. External
    // targets are deliberately not resolved inside a database transaction.
    for (const auto& host : mx_hosts) {
        auto state = trx.lookupEntryAndSoa(host);
        if (!state.hasSoa()) continue;
        const auto changed = result.changes.find(host);
        const auto proposed = changed == result.changes.end() ? buffer(state.rr()) : changed->second.second;
        Entry entry{proposed}; bool usable = false;
        for (const auto& rr : entry) {
            if (rr.type() == TYPE_CNAME) throw Error{409, "MX target is a CNAME: " + host};
            usable = usable || rr.type() == TYPE_A || rr.type() == TYPE_AAAA;
        }
        if (!usable) throw Error{409, "Local MX target has no proposed address: " + host};
    }
    if (method != Method::POST && !preserve_ip) {
        for (const auto& host : old_hosts) {
            if (!mx_hosts.contains(host) && !addresses.contains(host) && !cleanup_hosts.contains(host)) {
                auto entry = trx.lookup(host);
                if (std::any_of(entry.begin(), entry.end(), [](const auto& rr) { return rr.type() == TYPE_A || rr.type() == TYPE_AAAA; })) {
                    const auto reason = host != domain && !host.ends_with("." + domain)
                        ? ": external host addresses are never removed automatically"
                        : ": exclusive mail use could not be established";
                    warnings.push_back(json::value{"Preserved addresses for " + host + reason});
                }
            }
        }
    }
    created.clear(); updated.clear(); deleted.clear();
    for (const auto& [owner, data] : result.changes) {
        using Values = std::multiset<std::pair<uint32_t, std::string>>;
        using Group = std::pair<std::string, std::string>;
        std::map<Group, std::pair<Values, Values>> groups;
        auto collect = [&](const std::string& raw, bool after) {
            for (const auto& rr : Entry{raw}) {
                auto& values = groups[{std::string{rr.typeName()}, purpose(owner, rr, domain)}];
                (after ? values.second : values.first).emplace(rr.ttl(), std::string{rr.rdata().data(), rr.rdata().size()});
            }
        };
        collect(data.first, false); collect(data.second, true);
        for (const auto& [kind, values] : groups) {
            if (values.first == values.second) continue;
            Object item{{"name", owner}, {"type", kind.first}, {"purpose", kind.second}};
            if (values.first.empty()) created.push_back(item);
            else if (values.second.empty()) deleted.push_back(item);
            else updated.push_back(item);
        }
    }
    result.report = Object{{"domain", domain}, {"status", method == Method::DELETE ? "deleted" : "provisioned"},
                           {"created", created}, {"updated", updated}, {"deleted", deleted}, {"warnings", warnings}};
    return result;
}

namespace {
std::string entryFingerprint(const Entry& entry) {
    std::string data;
    for (const auto& rr : entry) {
        if (rr.type() == TYPE_SOA) continue; // Other records changing the serial do not invalidate a plan.
        data += std::format("{}:{}:{}:", rr.type(), rr.ttl(), rr.rdlength());
        data.append(rr.rdata().data(), rr.rdata().size());
    }
    return sha256(data);
}
std::string mailFingerprint(ResourceIf::TransactionIf& trx, const std::string& domain) {
    std::string data;
    auto owners = discovered(trx, domain);
    std::sort(owners.begin(), owners.end());
    for (const auto& owner : owners) {
        auto entry = trx.lookup(owner);
        for (const auto& rr : entry) {
            if (purpose(owner, rr, domain).empty()) continue;
            data += owner + std::format(":{}:{}:{}:", rr.type(), rr.ttl(), rr.rdlength());
            data.append(rr.rdata().data(), rr.rdata().size());
        }
    }
    return sha256(data);
}
Object makeMetadata(const std::string& domain, const Session& session, const Object& policy, const Object& old, int64_t now) {
    if (policy.empty()) return {};
    auto history = num(old, "history_until", 0, 0, INT64_MAX);
    auto enforced = num(old, "enforce_until", 0, 0, INT64_MAX);
    const auto previous = section(old, "policy");
    if (!previous.empty() && previous != policy) {
        const auto expiry = now + num(previous, "max_age", 0, 0, 31557600);
        history = std::max(history, expiry);
        if (str(previous, "mode") == "enforce") enforced = std::max(enforced, expiry);
    }
    Object result{{"version", 1}, {"domain", domain}, {"tenant", session.tenant()}, {"policy", policy},
                  {"history_until", history}, {"enforce_until", enforced}, {"generation", newUuidStr()}};
    if (previous == policy && !old.contains("pending")) return old; // Preserve revision/generation on an identical PUT.
    return result;
}
} // namespace

yahat::Response MailProvisioner::request(const yahat::Request& req, std::string_view domainInput, std::string_view operation) {
    try {
        const auto domain = name(domainInput);
        if (req.type != Method::GET && req.type != Method::POST && req.type != Method::PUT && req.type != Method::DELETE) throw Error{405, "Unsupported mail operation"};
        if (server_.role() == Server::Role::CLUSTER_FOLLOWER && req.type != Method::GET) throw Error{403, "Mail mutations require the primary"};
        const auto* ptr = std::any_cast<std::shared_ptr<Session>>(&req.auth.extra);
        if (!ptr || !*ptr || !req.auth.access) throw Error{401, "Authentication required"};
        const auto& session = **ptr;
        // Mail mutations use the authenticated tenant. Impersonation must never become
        // durable authority accidentally through a query parameter.
        if (req.arguments.contains("tenant")) throw Error{400, "Mail provisioning does not support tenant impersonation"};
        if (req.type == Method::GET) {
            if (operation != "status") throw Error{404, "Use /status for mail transition status"};
            auto trx = server_.resource().transaction();
            auto root = trx->lookupEntryAndSoa(domain);
            if (!root.hasSoa()) throw Error{404, "Unknown zone"};
            if (!root.soa().tenantId() || *root.soa().tenantId() != session.tenantId()
                || !(session.isAllowed(pb::CREATE_MAIL, domain) || session.isAllowed(pb::UPDATE_MAIL, domain) || session.isAllowed(pb::DELETE_MAIL, domain))) throw Error{403, "Mail status access denied"};
            auto meta = load(*trx, domain);
            Object value{{"domain", domain}, {"status", "complete"}};
            if (auto* pending = meta.if_contains("pending")) {
                const auto& p = pending->as_object();
                auto task = server_.timedTasks().get(*trx, str(p, "task"));
                value["transition"] = str(p, "task");
                value["earliest_completion"] = p.at("due");
                value["applied_policy"] = meta.at("policy");
                value["status"] = task.empty() ? json::value{"failed"} : task.at("state");
                for (auto key : {"attempts", "max_retries", "failure", "next"}) if (auto* field = task.if_contains(key)) value[key] = *field;
            }
            return reply(200, std::move(value));
        }
        if (!operation.empty()) throw Error{404, "Unknown mail operation"};
        if (req.body.size() > 131072) throw Error{400, "Mail request exceeds 128 KiB"};
        Object input;
        if (req.type != Method::DELETE) {
            boost::system::error_code ec;
            auto value = json::parse(req.body, ec);
            if (ec || !value.is_object()) throw Error{400, "Mail request must be a JSON object"};
            input = value.as_object();
        } else if (!req.body.empty()) throw Error{400, "DELETE accepts no request body"};
        bool preserve_ip = false;
        for (const auto& [key, value] : req.arguments) {
            if (key != "preserve_ip" || req.type != Method::DELETE || (value != "true" && value != "false")) throw Error{400, "Unsupported mail query argument"};
            preserve_ip = value == "true";
        }
        auto initial = server_.resource().transaction();
        auto guard = lockZones(*initial, domain, input);
        initial.reset();
        auto trx = server_.resource().transaction();
        auto proposed = plan(*trx, session, domain, req.type, input, preserve_ip);
        const auto now = server_.timedTasks().now();
        const auto identity = sha256(json::serialize(canonical(Object{{"input", input}, {"delete", req.type == Method::DELETE}, {"preserve_ip", preserve_ip}})));
        auto old = proposed.old_metadata;
        if (const auto* p = old.if_contains("pending"); p && str(p->as_object(), "identity") == identity) {
            auto task = server_.timedTasks().get(*trx, str(p->as_object(), "task"));
            return reply(200, Object{{"domain", domain}, {"status", task.empty() ? json::value{"failed"} : task.at("state")},
                                    {"transition", p->as_object().at("task")}, {"earliest_completion", p->as_object().at("due")},
                                    {"applied_policy", old.at("policy")}, {"warnings", proposed.report.at("warnings")}});
        }
        if (const auto* pending = old.if_contains("pending")) server_.timedTasks().cancel(*trx, str(pending->as_object(), "task"), "Superseded by a newer mail request");
        const auto previous = section(old, "policy");
        auto safe_after = num(old, "history_until", 0, 0, INT64_MAX);
        auto enforce_after = num(old, "enforce_until", 0, 0, INT64_MAX);
        if (!previous.empty()) {
            safe_after = std::max(safe_after, now + num(previous, "max_age", 0, 0, 31557600));
            if (str(previous, "mode") == "enforce") enforce_after = std::max(enforce_after, now + num(previous, "max_age", 0, 0, 31557600));
        }
        const bool disable = proposed.policy.empty();
        const bool weaker = !previous.empty() && (disable || (str(previous, "mode") == "enforce" && str(proposed.policy, "mode") != "enforce"));
        auto protected_mx = old.if_contains("protected_mx");
        const auto* old_mx = protected_mx ? protected_mx : previous.if_contains("mx");
        const auto* new_mx = proposed.policy.if_contains("mx");
        const bool compatible = old_mx && new_mx && *old_mx == *new_mx && str(proposed.policy, "mode") == "enforce";
        const bool retirement = (weaker && safe_after > now) || (enforce_after > now && !compatible && previous != proposed.policy);
        std::vector<std::string> changed;
        if (retirement) {
            if (server_.config().enable_auth && session.principal().empty()) throw Error{403, "No durable initiating principal"};
            if (!session.isAllowed(proposed.permission, "_mta-sts." + domain)) throw Error{403, "Mail permission denied for MTA-STS owner"};
            if (previous.empty()) throw Error{409, "Policy history is incomplete; cannot safely retire"};
            // Preserve the entire mail configuration during retirement. Only the policy
            // discovery TXT and its stored policy change in this transaction.
            Object retiring{{"mode", "none"}, {"mx", previous.at("mx")}, {"max_age", 86400}};
            const auto owner = "_mta-sts." + domain;
            auto entry = trx->lookupEntryAndSoa(owner);
            if (!entry.hasSoa() || !entry.soa().tenantId() || *entry.soa().tenantId() != session.tenantId()) throw Error{403, "Not your MTA-STS zone"};
            StorageBuilder sb; sb.setTenantId(entry.soa().tenantId());
            if (toLower(entry.soa().getSoa().labels().string()) != owner) sb.setZoneLen(entry.soa().getSoa().labels().size() - 1);
            uint32_t retirement_ttl = server_.config().default_ttl;
            bool have_txt_ttl = false;
            for (const auto& rr : entry.rr()) {
                if (rr.type() == TYPE_TXT) {
                    if (have_txt_ttl && rr.ttl() != retirement_ttl) throw Error{409, "Existing MTA-STS TXT TTLs are inconsistent"};
                    retirement_ttl = rr.ttl(); have_txt_ttl = true;
                }
                if (rr.type() == TYPE_CNAME) throw Error{409, "MTA-STS owner has a CNAME"};
                if (purpose(owner, rr, domain) != "mta_sts") sb.createRr(owner, rr.type(), rr.ttl(), rr.rdata());
            }
            sb.createTxt(owner, retirement_ttl, "v=STSv1; id=" + policyId(retiring)); sb.finish();
            changed = server_.api().applyDnsChanges(*trx, {{owner, {buffer(entry.rr()), buffer(sb)}}});
            const auto generation = newUuidStr();
            Object snapshots;
            for (const auto& [n, data] : proposed.changes) snapshots[n] = entryFingerprint(trx->lookup(n));
            Object payload{{"domain", domain}, {"tenant", session.tenant()}, {"principal", session.principal()},
                           {"generation", generation}, {"input", input}, {"delete", req.type == Method::DELETE},
                           {"preserve_ip", preserve_ip}, {"snapshots", snapshots}, {"mail_fingerprint", mailFingerprint(*trx, domain)}};
            const auto due = std::max(safe_after, enforce_after);
            const auto id = server_.timedTasks().schedule(*trx, "mail-retirement/v1", due, std::move(payload));
            Object meta{{"version", 1}, {"domain", domain}, {"tenant", session.tenant()}, {"policy", retiring},
                        {"history_until", safe_after}, {"enforce_until", enforce_after}, {"generation", generation},
                        {"protected_mx", old_mx ? *old_mx : previous.at("mx")},
                        {"pending", Object{{"task", id}, {"identity", identity}, {"due", due}}}};
            save(*trx, domain, meta);
            proposed.report["status"] = "pending";
            proposed.report["transition"] = id;
            proposed.report["earliest_completion"] = due;
            proposed.report["applied_policy"] = retiring;
            // The replacement records have been validated but have not been applied yet.
            proposed.report["created"] = Array{}; proposed.report["deleted"] = Array{};
            proposed.report["updated"] = Array{Object{{"name", owner}, {"purpose", "mta_sts"}}};
        } else {
            changed = server_.api().applyDnsChanges(*trx, proposed.changes);
            if (!changed.empty()) claimHosts(*trx, proposed.claim_hosts, domain, session);
            auto meta = makeMetadata(domain, session, proposed.policy, old, now);
            if (meta != old) save(*trx, domain, meta);
            if (changed.empty()) {
                proposed.report["created"] = Array{}; proposed.report["updated"] = Array{}; proposed.report["deleted"] = Array{};
            }
        }
        trx->commit();
        guard.reset();
        server_.api().notifyDnsChanges(changed);
        server_.timedTasks().wake();
        LOG_DEBUG << "Mail provisioning " << domain << " method=" << static_cast<int>(req.type) << " zones=" << changed.size() << " retirement=" << retirement;
        return reply(req.type == Method::POST ? 201 : 200, std::move(proposed.report));
    } catch (const Error& error) { return failure(error); }
      catch (const yahat::Response& response) { return failure(Error{response.code, response.reason}); }
      catch (const std::length_error& error) { return failure(Error{400, error.what()}); }
}

TimedTasks::Execution MailProvisioner::execute(ResourceIf::TransactionIf& trx, const Object& payload) {
    try {
        const auto domain = name(str(payload, "domain"));
        const auto input = section(payload, "input");
        auto guard = lockZones(trx, domain, input);
        auto metadata = load(trx, domain);
        if (str(metadata, "generation") != str(payload, "generation") || !metadata.contains("pending")) return {guard, {}}; // Superseded; complete without effects.
        const auto due = num(section(metadata, "pending"), "due", INT64_MAX, 0, INT64_MAX);
        if (server_.timedTasks().now() < due) throw std::runtime_error{"Retirement deadline has not elapsed"};
        std::shared_ptr<Session> session;
        if (server_.config().enable_auth) session = server_.auth().resumePrincipal(str(payload, "tenant"), str(payload, "principal"));
        else session = std::make_shared<Session>(server_.auth());
        if (!session || session->tenant() != str(payload, "tenant")) throw Error{403, "Initiating principal revoked or tenant changed"};
        for (const auto& item : section(payload, "snapshots")) {
            if (entryFingerprint(trx.lookup(item.key())) != item.value().as_string()) throw Error{409, "DNS changed while mail retirement was pending"};
        }
        if (mailFingerprint(trx, domain) != str(payload, "mail_fingerprint")) throw Error{409, "Mail DNS changed while retirement was pending"};
        auto proposed = plan(trx, *session, domain, flag(payload, "delete", false) ? Method::DELETE : Method::PUT, input, flag(payload, "preserve_ip", false));
        const auto changed = server_.api().applyDnsChanges(trx, proposed.changes);
        if (!changed.empty()) claimHosts(trx, proposed.claim_hosts, domain, *session);
        auto meta = makeMetadata(domain, *session, proposed.policy, metadata, server_.timedTasks().now());
        save(trx, domain, meta);
        return {guard, [this, changed] { server_.api().notifyDnsChanges(changed); }};
    } catch (const Error& error) { throw TimedTasks::PermanentFailure{error.what()}; }
      catch (const yahat::Response& response) { throw TimedTasks::PermanentFailure{response.reason}; }
}

yahat::Response MailProvisioner::policy(const yahat::Request& req) {
    try {
        if (req.target != "/.well-known/mta-sts.txt" || req.type != Method::GET) return {404, "Not found"};
        auto host = std::string{req.getHeader("Host")};
        if (auto colon = host.find(':'); colon != host.npos) {
            if (host.substr(colon) != ":443" && host.substr(colon) != ":80") return {404, "Not found"};
            host.resize(colon);
        }
        host = name(host);
        if (!host.starts_with("mta-sts.")) return {404, "Not found"};
        const auto domain = name(std::string_view{host}.substr(8));
        auto trx = server_.resource().transaction();
        const auto meta = load(*trx, domain);
        const auto p = section(meta, "policy");
        if (p.empty()) return {404, "Not found"};
        // Ownership changes/deleted zones must not leave a stale public policy active.
        auto zone = trx->lookupEntryAndSoa(domain);
        if (!zone.hasSoa() || !zone.soa().tenantId() || boost::uuids::to_string(*zone.soa().tenantId()) != str(meta, "tenant")) return {404, "Not found"};
        std::string body = "version: STSv1\nmode: " + str(p, "mode") + "\n";
        for (const auto& mx : p.at("mx").as_array()) body += "mx: " + std::string{mx.as_string()} + "\n";
        body += "max_age: " + std::to_string(num(p, "max_age", 0, 0, 31557600)) + "\n";
        yahat::Response response{200, "OK", std::move(body)};
        response.mime_type = "text/plain";
        response.headers.set("Cache-Control", "no-store");
        return response;
    } catch (const Error&) { return {404, "Not found"}; }
}
} // namespace nsblast::lib
