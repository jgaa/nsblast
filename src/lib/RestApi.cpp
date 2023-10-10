
#include <set>

#include <boost/json/src.hpp>
#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/tokenizer.hpp>

#include "RestApi.h"
#include "nsblast/logging.h"
#include "nsblast/DnsMessages.h"
#include "nsblast/util.h"
#include "SlaveMgr.h"
#include "AuthMgr.h"
#include "Notifications.h"
#include "PrimaryReplication.h"
#include "RocksDbResource.h"
//#include "nsblast/DnsEngine.h"
#include "nsblast/errors.h"
#include "google/protobuf/util/json_util.h"
#include "proto_util.h"

#include "glad/AsyncCache.hpp"

using namespace std;
using namespace std::string_literals;
using namespace yahat;

ostream& operator << (ostream& out, nsblast::lib::Session& session) {
    return out << " {tenant=" << session.tenant()
               << ", user=" << session.who() << '}';
}

namespace nsblast::lib {

namespace {

auto makeRrFilter(string_view tokens) {
    static constexpr array<pair<string_view, uint16_t>, 10> rrs = {{
        {"a", TYPE_A},
        {"aaaa", TYPE_AAAA},
        {"ns", TYPE_NS},
        {"txt", TYPE_TXT},
        {"cname", TYPE_CNAME},
        {"mx", TYPE_MX},
        {"ptr", TYPE_PTR},
        {"srv", TYPE_SRV},
        {"hinfo", TYPE_HINFO},
        {"asfdb", TYPE_AFSDB}
    }};
    vector<uint16_t> filter;

    for(auto const &token : boost::tokenizer{tokens.begin(), tokens.end(), boost::char_separator{","}}) {
        if (auto it = find_if(rrs.begin(), rrs.end(), [&token](auto& v) {
                return v.first == token;
            }); it != rrs.end()) {
            filter.push_back(it->second);
        } else {
            throw Response{400, format("Invalid/unknown filter: {}", token)};
        }
    }

    return filter;
}

std::shared_ptr<Session> getSession(const yahat::Request& req) {
    try {
        if (auto session = any_cast<std::shared_ptr<Session>>(req.auth.extra)) {
            return session;
        }
    } catch(const std::bad_any_cast&) {
        assert(false);
    }

    return {};
}

optional<string_view> getQueryArg(const yahat::Request& req, const std::string& name) {
    if (auto it = req.arguments.find(name); it != req.arguments.end()) {
        return it->second;
    }

    return {};
}

string_view getQueryArg(const yahat::Request& req, const std::string& name, string_view defaultVal) {
    if (auto it = req.arguments.find(name); it != req.arguments.end()) {
        return it->second;
    }

    return defaultVal;
}

auto test_glad(boost::asio::io_context& ctx) {
    jgaa::glad::AsyncCache<string, string> cache([](const string& key, const auto& cb){;}, ctx);
    cache.get("teste", [](const auto& err, const auto& value) {});
}

optional<uint32_t> getTtl(const boost::json::value& json) {
    if (json.is_object()) {
        try {
            auto ttl = json.at("ttl");
            // This is BS! The json library must be able to convert a fucking integer
            // to unsinged or signed values, unless the actual value in
            // the json payload is a negative integer!
            if (ttl.is_int64()) {
                return static_cast<uint32_t>(ttl.as_int64());
            }

            if (ttl.is_uint64()) {
                return static_cast<uint32_t>(ttl.as_uint64());
            }

            const string kname = boost::json::to_string(ttl.kind());
            throw Response{400, "ttl must be an unsigned integer, not a "s + kname};
        } catch(const std::exception& ex) {
            LOG_TRACE << "Caught exception while extracting ttl: " << ex.what();
        }

    }

    return {};
}

template <typename T>
bool fromJson(const std::string& json, T& obj) {
    const auto res = google::protobuf::util::JsonStringToMessage(json, &obj);
    if (!res.ok()) {
        LOG_DEBUG << "Failed to convert json to "
                 << typeid(T).name() << ": "
                 << res.ToString();
        LOG_TRACE << "Failed json: " << json;
        return false;
    }
    return true;
}

// Convert a plain email-address from "some.persone.name@example.com" to some\.persons\.name.example.com
string_view toDnsEmail(string_view email, std::string& buffer) {

    if (auto pos = email.find('@'); pos != string_view::npos) {
        buffer.reserve(email.size() + 3 /* assume no more than 3 dots in the first segment */);
        for(auto it = email.begin(); it != email.end(); ++it) {
            const auto ch = *it;
            if (ch == '.') {
                buffer += "\\.";
                continue;
            }
            if (ch == '@') {
                buffer += '.';
                ++it;
                copy(it, email.end(), back_inserter(buffer));
                return buffer;
            }
            buffer += ch;
        }
    }

    return email;
}

// Create a difference sequence. RFC 1995
void createDiffSequence(StorageBuilder& sb,
                        const RrSoa& oldSoa,
                        const RrSoa& newSoa,
                        const Entry& oldContent,
                        const Entry& newContent)
{
    static const auto compare_rr = [](const Entry::Iterator& left, const Entry::Iterator& right) {
        const auto ls = left->selfSpan();
        const auto rs = right->selfSpan();

        auto res = memcmp(ls.data(), rs.data(), min(ls.size(), rs.size()));
        if (res == 0) {
            return ls.size() < rs.size();
        }
        return res < 0;
    };

    vector<Entry::Iterator> older;
    vector<Entry::Iterator> newer;
    for(auto it = oldContent.begin(); it != oldContent.end(); ++it) {
        older.push_back(it);
    }
    sort(older.begin(), older.end(), compare_rr);

    for(auto it = newContent.begin(); it != newContent.end(); ++it) {
        newer.push_back(it);
    }
    sort(newer.begin(), newer.end(), compare_rr);

    // Get deleted/changed entries
    vector<Entry::Iterator> deleted;
    set_difference(older.begin(), older.end(), newer.begin(), newer.end(),
                   back_inserter(deleted), compare_rr);

    // Get new/changed entries
    vector<Entry::Iterator> added;
    set_difference(newer.begin(), newer.end(), older.begin(), older.end(),
                   back_inserter(added), compare_rr);


    auto add_rrs = [&sb](const auto& seq) {
        for(const auto& it : seq) {
            if (it->type() == TYPE_SOA) {
                continue; // Must be in the start of *any* segment, and never inside a segment.
            }

            sb.addRr(*it);
        }
    };

    sb.addRr(oldSoa);

    add_rrs(deleted);

    sb.addRr(newSoa);

    add_rrs(added);
}

// Create and add a complete diff transaction for a normal update (one serial increment).
void addDiff(string_view zoneName,
             const RrSoa& oldSoa,
             const RrSoa& newSoa,
             const Entry& oldContent,
             const Entry& newContent,
             ResourceIf::TransactionIf& trx) {

    assert(newSoa.serial() > oldSoa.serial());

    LOG_TRACE << "addDiff: Creating diff for zone: " << zoneName;

    StorageBuilder sb;

    // We need to store the entries in the order that we added them.
    sb.doSort(false);
    // We need to store multiple soa's (something that is invalid in a normal Zone or Entity)
    sb.oneSoa(false);

    createDiffSequence(sb, oldSoa, newSoa, oldContent, newContent);

    sb.finish();

    ResourceIf::RealKey key{zoneName, newSoa.serial(), ResourceIf::RealKey::Class::DIFF};

    // Don't enforce uniqueness, even if this is a unique key.
    // In case the server crash and fails to correctly reach a consistent state
    // from the wal log, we don't want to block the zone from ever being updated again.
    // A reasonable final solution could be to set a state on the zone to enforce full
    // zone transfers if this situation actually arise.
    // TODO: Fixme

    if (trx.keyExists(key, ResourceIf::Category::DIFF)) {
        LOG_ERROR << "The DIFF key " << key
                  << " already exists in the DIFF storage. These keys are supposed to be unique!"
                  << " I will oevrwrite the existing data.";
    }
    trx.write(key, sb.buffer(), false, ResourceIf::Category::DIFF);
}

tuple<std::optional<Response>, shared_ptr<Session>, optional<pb::Tenant>, bool /*all */>
getSessionAndTenant(const yahat::Request &req, Server& server, bool allowAll = false) {

    bool all = false;
    auto session = getSession(req);
    //auto lowercaseKey = toLower(parsed.target);
    auto tenant_id = string{session->tenant()};
    //auto trx = resource_.transaction();
    int rcode = 200;

    if (auto impersonate = getQueryArg(req, "tenant")) {
        if (session->isAllowed(pb::Permission::IMPERSONATE_TENANT)) {
            if (allowAll && *impersonate == "*") {
                // Impersonate all.
                // Only valid for some queries.
                LOG_INFO << "In request" << req.uuid << ", session "
                         << *session << " the client is impersonating  all tenants";
                all = true;
            } else {
                tenant_id = toLower(*impersonate);
                LOG_INFO << "In request" << req.uuid << ", session "
                         << *session << " the client is impersonating  "
                         << *impersonate;
            }
        } else {
            LOG_WARN << "In request" << req.uuid << ", session "
                     << *session << " the client tried to impersonate "
                     << *impersonate;
            return {Response{403, "You are not allowed to impersonate another tenant!"}, {}, {}, {}};
        }
    }

    auto tenant = server.auth().getTenant(tenant_id);
    if (!tenant) {
        return {Response{404, "Tenant not found"}, {}, {}, {}};
    }

    return {{}, session, tenant, all};
}

boost::json::array& asArray(boost::json::object& v, string_view name) {

    if (auto entry = v.if_contains(name)) {
        assert(entry->is_array());
        return entry->as_array();
    }

    auto [it, _] = v.emplace(name, boost::json::array{});
    return it->value().as_array();
}

void toJson(const span_t buffer, const Rr& rr, boost::json::object& obj) {
    switch(rr.type()) {
    case TYPE_A:
        asArray(obj, "a").emplace_back(RrA(buffer, rr.offset()).address().to_string());
        break;
    case TYPE_AAAA:
        asArray(obj, "aaaa").emplace_back(RrA(buffer, rr.offset()).address().to_string());
        break;
    case TYPE_NS:
        asArray(obj, "ns").emplace_back(RrNs(buffer, rr.offset()).ns().string());
        break;
    case TYPE_CNAME:
        obj["cname"] = RrCname(buffer, rr.offset()).cname().string();
        break;
    case TYPE_SOA: {
        boost::json::object o;
        const RrSoa soa(buffer, rr.offset());
        o["mname"] = soa.mname().string();
        o["rname"] = soa.rname().string();
        o["email"] = soa.email();
        o["serial"] = soa.serial();
        o["refresh"] = soa.refresh();
        o["retry"] = soa.retry();
        o["expire"] = soa.expire();
        o["minimum"] = soa.minimum();
        obj["soa"] = std::move(o);
    } break;
    case TYPE_PTR:
        asArray(obj, "ptr").emplace_back(RrPtr(buffer, rr.offset()).ptrdname().string());
    case TYPE_MX: {
        boost::json::object o;
        const RrMx mx(buffer, rr.offset());
        o["host"] = mx.host().string();
        o["priority"] = mx.priority();
        obj["mx"] = std::move(o);
    } break;
    case TYPE_TXT:
        asArray(obj, "txt").emplace_back(RrTxt(buffer, rr.offset()).string());
        break;
    case TYPE_SRV: {
        boost::json::object o;
        const RrSrv srv(buffer, rr.offset());
        o["target"] = srv.target().string();
        o["priority"] = srv.priority();
        o["weight"] = srv.weight();
        o["port"] = srv.port();
        obj["srv"] = std::move(o);
    } break;
    case TYPE_AFSDB: {
        boost::json::object o;
        const RrAfsdb ad(buffer, rr.offset());
        o["host"] = ad.host().string();
        o["subtype"] = ad.subtype();
        obj["afsdb"] = std::move(o);
    } break;
    case TYPE_RP: {
        boost::json::object o;
        const RrRp rp(buffer, rr.offset());
        o["mbox"] = rp.mbox().string();
        o["txt"] = rp.txt().size();
        obj["rp"] = std::move(o);
    } break;
    case TYPE_HINFO: {
        boost::json::object o;
        const RrHinfo hi(buffer, rr.offset());
        o["cpu"] = hi.cpu();
        o["os"] = hi.os();
        obj["hinfo"] = std::move(o);
    } break;
    default:
        const auto name = format("#{}", rr.type());
        asArray(obj, name).emplace_back(Base64Encode(rr.rdata()));
    } // switch

}


auto toJson(const lib::Entry& entry) {
    boost::json::object o;
    boost::json::array a;
    bool has_label = false;
    for(const auto& rr : entry) {
        if (!has_label) {
            o["fqdn"] = rr.labels().string();
            o["ttl"] = rr.ttl();
            has_label = true;
        }

        toJson(entry.buffer(), rr, o);
    }

    return o;
}

enum class KindOfListing {
    DEFAULT,
    ID
};

KindOfListing getKindOfListing(const yahat::Request &req)
{
    static constexpr array<pair<string_view, KindOfListing>, 2> kinds = {
        make_pair("id", KindOfListing::ID),
        {"default", KindOfListing::DEFAULT}};

    if (auto it = req.arguments.find("kind"); it != req.arguments.end()) {
        const auto key = it->second;
        if (auto kit = find_if(kinds.begin(), kinds.end(), [&key](const auto& p ) {
                return p.first == key;
            }); kit != kinds.end()) {
            return kit->second;
        }
    }

    return KindOfListing::DEFAULT;
}


} // anon ns

RestApi::RestApi(Server& server)
    : config_{server.config()}, resource_{server.resource()}
    , server_{&server}
{
}

RestApi::RestApi(const Config &config, ResourceIf &resource)
    : config_{config}, resource_{resource} {}

Response RestApi::onReqest(const Request &req)
{
    const auto p = parse(req);

    try {
        if (p.what == "rr") {
            return onResourceRecord(req, p);
        }

        if (p.what == "zone") {
            if (req.type == Request::Type::GET) {
                if (!p.operation.empty()) {
                    return {400, "Invalid operation"};
                }
                if (p.target.empty()) {
                    return listZones(req, p);
                }
                return listZone(req, p);
            }
            return onZone(req, p);
        }

        if (p.what == "tenant") {
            return onTenant(req, p);
        }

        if (p.what == "user") {
            return onUser(req, p);
        }

        if (p.what == "role") {
            return onRole(req, p);
        }

        if (p.what == "config") {
            if (p.operation == "master") {
                return onConfigMaster(req, p);
            }
        }

        if (p.what == "backup") {
            return onBackup(req, p);
        }
    } catch(const nsblast::Exception& ex) {
        LOG_DEBUG << "RestApi::onReqest: Cautht exception while processing request "
              << req.uuid << ": " << ex.what();
        return {ex.httpError(), ex.httpMessage()};
    }

    LOG_DEBUG << "Unknown subpath: " << p.what;
    return {404, "Unknown subpath"};
}

RestApi::Parsed RestApi::parse(const Request &req)
{
    // target syntax: /api/v1/zone|rr/{fqdn}[/verb]
    //                        ^
    //                        +------               = what
    //                        |        -----        = target
    //                        |               ----  = operation
    //                        +-------------------- = base

    Parsed p;
    p.base = req.target;
    p.base = p.base.substr(req.route.size());
    while(!p.base.empty() && p.base.starts_with('/')) {
        p.base = p.base.substr(1);
    }

    if (auto pos = p.base.find('/'); pos != string_view::npos) {
        p.what = p.base.substr(0, pos);

        if (p.base.size() > pos) {
            p.target = p.base.substr(pos + 1);

            if (auto end = p.target.find('/') ; end != string_view::npos) {
                if (p.target.size() > end) {
                    p.operation = p.target.substr(end + 1);
                }
                p.target = p.target.substr(0, end);
            }
        }
    } else {
        p.what = p.base;
    }

    return p;
}

void RestApi::validateSoa(const boost::json::value &json)
{
    auto soa = json.at("soa");
    if (!soa.is_object()) {
        throw Response{400, "'soa' must be a json object"};
    }

    const auto& o = soa.as_object();

    for (string_view key : {"mname", "rname"}) {
        if (auto what = o.if_contains(key)) {
            if (!what->is_string()) {
                throw Response{400, "Not a string: "s + string(key)};
            }
        }
    }

    for (string_view key : {"refresh", "retry", "version", "expire", "minimum"}) {
        if (auto what = o.if_contains(key)) {
            if (!what->is_int64()) {
                throw Response{400, "Not a number: "s + string(key)};
            }
        }
    }
}

boost::json::value RestApi::parseJson(string_view json)
{
    boost::json::error_code ec;

    auto obj = boost::json::parse(json, ec);
    if (ec) {
        LOG_DEBUG << "Failed to parse Json object: " << ec;
        throw Response{400, "Failed to parse json"};
    }

    return obj;
}

void RestApi::validateZone(const boost::json::value &json)
{
    validateSoa(json);

    string mname; // primary dns

    try {
        mname = json.as_object().at("soa").as_object().at("mname").as_string();
    }  catch (const exception&) {
        throw Response{400, "Soa must include 'mname' with the primary NS server for the zone as a string."};
    }

    if (mname.empty()) {
        throw Response{400, "'Soa.mname' can not be empty."};
    }

    string_view ckey = "ns";
    bool has_primary = false;
    try {
        auto ns = json.at("ns");

        if (!ns.is_array()) {
            throw Response{400, "Json element 'ns' must be an array of string(s)"s};
        }

        if (ns.as_array().size() < 2) {
            throw Response{400, "RFC1036 require at least two nameservers (ns records)"s};
        }

        for(const auto& v : ns.as_array()) {
            if (!v.is_string()) {
                throw Response{400, "Json elements in 'ns' must be string(s)"s};
            }
            if (v.as_string() == mname) {
                has_primary = true;
            }
        }
    } catch(const exception& ex) {
        throw Response{400, "Missing Json element "s + string(ckey)};
    }

    if (!has_primary) {
        throw Response{400, "soa.mname must be one of the ns entries"};
    }
}

void RestApi::build(string_view fqdn, uint32_t ttl, StorageBuilder& sb,
                    const boost::json::value& json, bool finish)
{
    static const boost::unordered_flat_map<string_view, function<void(string_view, uint32_t, StorageBuilder&, const boost::json::value&)>>
        handlers = {
    { "ttl", [](string_view fqdn, uint32_t ttl, StorageBuilder& sb, const boost::json::value& v) {
        // Ignore here.
    }},
    {"soa", [](string_view fqdn, uint32_t ttl, StorageBuilder& sb, const boost::json::value& v) {

        // TODO: Set reasonable defaults
        uint32_t refresh = 1000;
        uint32_t retry = 1000;
        uint32_t expire = 1000;
        uint32_t minimum = 1000;
        uint32_t serial = 1;
        string_view mname;
        string_view rname;

        boost::unordered_flat_map<string_view, uint32_t *> nentries = {
            {"refresh", &refresh},
            {"retry", &retry},
            {"expire", &expire},
            {"minimum", &minimum},
            {"serial", &serial}
        };

        for(const auto& a : v.as_object()) {
            if (auto it = nentries.find(a.key()) ; it != nentries.end()) {
                assert(it->second);
                *it->second = a.value().as_int64();
            } else if (a.key() == "mname") {
                mname = a.value().as_string();
            } else if (a.key() == "rname") {
                rname = a.value().as_string();
            } else {
                throw Response{400, "Unknown soa entity: "s + string(a.key())};
            }
        }

        string rname_buf;
        sb.createSoa(fqdn, ttl, mname, toDnsEmail(rname, rname_buf), serial, refresh, retry, expire, minimum);
    }},
    {"srv", [](string_view fqdn, uint32_t ttl, StorageBuilder& sb, const boost::json::value& v) {

        if (!v.is_array()) {
            throw Response{400, "Json element 'srv' must be an array of objects(s)"};
        }

        for(const auto& srv : v.as_array()) {

            if (!srv.is_object()) {
                throw Response{400, "Json element 'srv' must be an array of objects(s)"};
            }

            uint32_t priority = 0;
            uint32_t weight = 0;
            uint32_t port = 0;
            string_view target;

            boost::unordered_flat_map<string_view, uint32_t *> nentries = {
                {"priority", &priority},
                {"weight", &weight},
                {"port", &port}
            };

            for(const auto& a : srv.as_object()) {
                if (auto it = nentries.find(a.key()) ; it != nentries.end()) {
                    assert(it->second);
                    *it->second = a.value().as_int64();
                } else if (a.key() == "target") {
                    target = a.value().as_string();
                } else {
                    throw Response{400, "Unknown Srv entity: "s + string(a.key())};
                }
            }

            if (target.empty() || !port) {
                throw Response{400, "Srv entities require avalid target and a valid port!"};
            }

            sb.createSrv(fqdn, ttl, priority, weight, port, target);
        }
    }},
    { "ns", [](string_view fqdn, uint32_t ttl, StorageBuilder& sb, const boost::json::value& v) {
        for(const auto& name : v.as_array()) {
            if (!name.if_string()) {
                throw Response{400, "Ns entities must be strings"};
            }

            sb.createNs(fqdn, ttl, name.as_string());
        }
    }},
    { "a", [](string_view fqdn, uint32_t ttl, StorageBuilder& sb, const boost::json::value& v) {
        for(const auto& name : v.as_array()) {
            if (!name.if_string()) {
                throw Response{400, "A entities must be strings"};
            }
            sb.createA(fqdn, ttl, string_view{name.as_string()});
        }
    }},
    { "txt", [](string_view fqdn, uint32_t ttl, StorageBuilder& sb, const boost::json::value& v) {

        if (v.if_string()) {
            // Allow a single string as well.
            sb.createTxt(fqdn, ttl, v.as_string());
            return;
        }

        if (!v.is_array()) {
            throw Response{400, "Txt entities must be an array of strings"};
        }

        for(const auto& name : v.as_array()) {
            if (!name.if_string()) {
                throw Response{400, "Txt entities must be strings"};
            }

            sb.createTxt(fqdn, ttl, name.as_string());
        }
    }},
    { "hinfo", [](string_view fqdn, uint32_t ttl, StorageBuilder& sb, const boost::json::value& v) {

        if (!v.if_object()) {
            throw Response{400, "Hinfo must be an object"};
        }

        string_view cpu;
        string_view os;

        for(const auto& a : v.as_object()) {
             if (a.key() == "cpu") {
                cpu = a.value().as_string();
             } else if (a.key() == "os") {
                os = a.value().as_string();
             } else {
                throw Response{400, "Unknown hinfo entity: "s + string(a.key())};
             }
         }

        sb.createHinfo(fqdn, ttl, cpu, os);
    }},
    { "rp", [](string_view fqdn, uint32_t ttl, StorageBuilder& sb, const boost::json::value& v) {
        if (!v.if_object()) {
            throw Response{400, "rp must be an object"};
        }

            string_view mbox;
            string_view txt;

            for(const auto& a : v.as_object()) {
                 if (a.key() == "mbox") {
                    mbox = a.value().as_string();
                 } else if (a.key() == "txt") {
                    txt = a.value().as_string();
                 } else {
                    throw Response{400, "Unknown rp entity: "s + string(a.key())};
                 }
             }

            sb.createRp(fqdn, ttl, mbox, txt);
    }},
    { "cname", [](string_view fqdn, uint32_t ttl, StorageBuilder& sb, const boost::json::value& v) {

        if (!v.if_string()) {
            throw Response{400, "Cname entities must be strings"};
        }
        sb.createCname(fqdn, ttl, v.as_string());
    }},
    { "dhcid", [](string_view fqdn, uint32_t ttl, StorageBuilder& sb, const boost::json::value& v) {

        if (!v.if_string()) {
            throw Response{400, "dhcid entities must be strings"};
        }
        sb.createBase64(fqdn, TYPE_DHCID, ttl, v.as_string());
    }},
    { "openpgpkey", [](string_view fqdn, uint32_t ttl, StorageBuilder& sb, const boost::json::value& v) {

        if (!v.if_string()) {
            throw Response{400, "openpgpkey entities must be strings"};
        }
        sb.createBase64(fqdn, TYPE_OPENPGPKEY, ttl, v.as_string());
    }},
    { "ptr", [](string_view fqdn, uint32_t ttl, StorageBuilder& sb, const boost::json::value& v) {

        // TODO: Validate that the payload is valid and in 'in-addr.arpa' or 'ip6.arpa' root-domain?

        if (!v.if_string()) {
            throw Response{400, "PTR entities must be strings (hostnames)"};
        }
        sb.createPtr(fqdn, ttl, v.as_string());
    }},
    { "mx", [](string_view fqdn, uint32_t ttl, StorageBuilder& sb, const boost::json::value& v) {

        if (!v.is_array()) {
            throw Response{400, "Json element 'mx' must be an array of objects(s)"};
        }

        for(const auto& mx: v.as_array()) {
            uint16_t priority = 10;
            string_view host;

            if (!mx.is_object()) {
                throw Response{400, "Json element 'mx' must be an array of objects(s)"};
            }

            for(const auto& e : mx.as_object()) {
                if (e.key() == "host") {
                    host = e.value().as_string();
                } else if (e.key() == "priority") {
                    priority = e.value().as_int64();
                } else {
                    throw Response{400, "Unknown entity in mx: "s + string(e.key())};
                }
            }

            if (host.empty()) {
                    throw Response{400, "Mx entry is missing 'host' attribute"};
            }

            sb.createMx(fqdn, ttl, priority, host);
        }
    }},
    { "afsdb", [](string_view fqdn, uint32_t ttl, StorageBuilder& sb, const boost::json::value& v) {
        for(const auto& mx: v.as_array()) {
            uint16_t subtype = 0;
            string_view host;

            for(const auto& e : mx.as_object()) {
                if (e.key() == "host") {
                    host = e.value().as_string();
                } else if (e.key() == "subtype") {
                    subtype = e.value().as_int64();
                } else {
                    throw Response{400, "Unknown entity in afsdb: "s + string(e.key())};
                }
            }

            sb.createAfsdb(fqdn, ttl, subtype, host);
        }
    }},
    { "rr", [](string_view fqdn, uint32_t ttl, StorageBuilder& sb, const boost::json::value& v) {

        if (!v.is_array()) {
            throw Response{400, "Json element 'rr' must be an array of objects(s)"};
        }

        for(const auto& rr: v.as_array()) {

            if (!rr.is_object()) {
                throw Response{400, "Json element 'rr' must be an array of objects(s)"};
            }

            uint16_t type = 0;
            string_view rdata;

            for(const auto& e : rr.as_object()) {
                if (e.key() == "rdata") {
                    rdata = e.value().as_string();
                } else if (e.key() == "type") {
                    const auto val = e.value().as_int64();
                    if (val < 0 ||  static_cast<size_t>(val) > numeric_limits<uint16_t>::max()) {
                        throw Response{400, "Invalid type (type must be an unsigned 16 bit integer): "s + to_string(val)};
                    }
                    type = static_cast<uint16_t>(val);
                } else {
                    throw Response{400, "Unknown entity in rr: "s + string(e.key())};
                }
            }

            if (!type) {
                throw Response{400, "Invalid or missing type in rr in "s + string{fqdn}};
            }

            if (type == TYPE_OPT) {
                throw Response{400, "OPT (41) is not a valid RR-type for storage"};
            }

            sb.createBase64(fqdn, type, ttl, rdata);
        }
    }}
    };

    LOG_TRACE << "RestApi::build - json is_object=" << json.is_object()
              << ", kind=" << json.kind()
              << ", json=" << boost::json::serialize(json);

    if (auto jttl = getTtl(json)) {
        ttl = *jttl;
    }

    for(const auto& obj : json.as_object()) {
        if (auto it = handlers.find(obj.key()); it != handlers.end()) {
            try {
                it->second(fqdn, ttl, sb, obj.value());
            } catch (const std::exception& ex) {
                LOG_ERROR << "Json entity " << obj.key() << " of type " << obj.value().kind()
                          << " failed processing with exception: " << ex.what();
                throw Response{400, "Failed converting '"s + string{obj.key()} + "' from json payload to internal format."};
            }
        } else {
            throw Response{400, "Unknown entity: "s + string(obj.key())};
        }
    }

    if (finish) {
        LOG_TRACE << "RestApi::build: " << "finishing sb";
        sb.finish();
    }
}

template <ProtoList T>
auto makeReply(T& what, int okCode = 200) {
    ostringstream out;

    out << R"({"error":false, "status":)"
        << okCode
        << R"(, "value":)";

    toJson(out, what);

    out << '}';

    return Response{okCode, "OK", out.str()};
}

template <ProtoMessage T>
auto makeReply(T& what, int okCode = 200) {
    ostringstream out;

    out << R"({"error":false, "status":)"
        << okCode
        << R"(, "value":)"
        << toJson(what)
        << '}';

    return Response{okCode, "OK", out.str()};
}

auto makeReplyWithReplStatus(int okCode, bool replicated) {
    ostringstream out;

    out << R"({"error":false, "status":)"
        << okCode
        << R"(, "replicated":)"
        << (replicated ? "true" : "false")
        << '}';

    return Response{okCode, "OK", out.str()};
}

Response RestApi::onTenant(const yahat::Request &req, const Parsed &parsed)
{
    auto session = getSession(req);
    auto lowercaseKey = toLower(parsed.target);
    auto trx = resource_.transaction();
    int rcode = 200;

    pb::Tenant tenant;
    if (req.expectBody()) {
        if (!fromJson(req.body, tenant)) {
            return {400, "Failed to parse json payload into a Tenant object"};
        }

        if (req.type == Request::Type::POST) {
            if (!parsed.target.empty()) {
                return {400, "POST Tenant cannot specify tenant-id in target"};
            }
        }

        if (req.type == Request::Type::PUT || req.type == Request::Type::PATCH){
            if (parsed.target.empty()) {
                return {400, "Tenant-id must be in the target"};
            }

            if (tenant.has_id()) {
                if (toLower(tenant.id()) != lowercaseKey) {
                    return {400, "Tenant-id in the tenant object is not the same as tenant-d in the target"};
                }
            } else {
                tenant.set_id(lowercaseKey);
            }
        }
    }

    if (tenant.has_id() && !isValidUuid(tenant.id())) {
        return {400, "id must be a valid UUID"};
    }

    try {
        switch(req.type) {
        case Request::Type::GET:
            if (parsed.target.empty()) {
                if (!session->isAllowed(pb::Permission::LIST_TENANTS, false)) {
                    return {403, "Access Denied"};
                }
                return listTenants(req, parsed);
            }
            if (!session->isAllowed(pb::Permission::GET_TENANT, false)) {
                if (lowercaseKey == session->tenant()) {
                    if (session->isAllowed(pb::Permission::GET_SELF_TENANT, false)) {
                        goto return_tenant;
                    }
                }
                return {403, "Access Denied"};
            }
return_tenant:
            if (auto tenant = server().auth().getTenant(lowercaseKey)) {
                return makeReply(*tenant, rcode);
            }
            return {404, "Not Found"};
         break;
        case Request::Type::POST: {
         if (!parsed.target.empty()) {
                return {400, "Create Tenant does not allow tenant-id in the target."};
            }
            if (!session->isAllowed(pb::Permission::CREATE_TENANT, false)) {
                return {403, "Access Denied"};
            }
            auto id = server().auth().createTenant(tenant);
            if (auto tenant = server().auth().getTenant(id)) {
                return makeReply(*tenant, 201);
            }
            LOG_ERROR << " RestApi::onTenant: I created Tenant " << id
                      << " but I failed to fetch it afterwards!";
            return {500, "Internal Server Error"};
        } break;
        case Request::Type::PUT:
            if (!session->isAllowed(pb::Permission::UPDATE_TENANT, false)) {
                return {403, "Access Denied"};
            }
            if (server().auth().upsertTenant(lowercaseKey, tenant, false)) {
                rcode = 201;
            }
            goto return_tenant;
        case Request::Type::PATCH:
            if (!session->isAllowed(pb::Permission::UPDATE_TENANT, false)) {
                return {403, "Access Denied"};
            }
            if (server().auth().upsertTenant(lowercaseKey, tenant, true)) {
                rcode = 201;
            }
            goto return_tenant;
        case Request::Type::DELETE:
            if (!session->isAllowed(pb::Permission::DELETE_TENANT, false)) {
                if (lowercaseKey == session->tenant()) {
                    if (session->isAllowed(pb::Permission::DELETE_SELF_TENANT, false)) {
                        goto delete_tenant;
                    }
                }
                return {403, "Access Denied"};
            }
delete_tenant:
            server().auth().deleteTenant(lowercaseKey);
            return {200, "OK"};
        default:
            return {400, "Invalid method"};
        }
    } catch(const Exception& ex) {
        LOG_DEBUG << "Exception while processingTenant request "
                  << req.uuid << ": " << ex.what();
        return {ex.httpError(), ex.httpMessage()};
    } catch (const exception& ex) {
        LOG_WARN << "Exception (std::exception) while processing Tenant request "
                 << req.uuid << ": " << ex.what();
        return {500, "Internal Server Error"};
    }

    return {200, "OK"};
}

Response RestApi::onRole(const yahat::Request &req, const Parsed &parsed)
{
    auto [res, session, tenant, all] = getSessionAndTenant(req, server());
    if (res) {
        return *res;
    }
    int rcode = 200;

    pb::Role role;
    if (req.expectBody()) {
        if (!fromJson(req.body, role)) {
            return {400, "Failed to parse json to a Role"};
        }

        if (req.type == Request::Type::POST) {
            if (!role.has_name()) {
                return {400, "The Role must have a name"};
            }
        } else if (parsed.target.empty()) {
            return {400, "Target must contain the role-name"};
        }
    }

    switch(req.type) {
    case Request::Type::GET:
        if (parsed.target.empty()) {
            if (!session->isAllowed(pb::Permission::LIST_ROLES, false)) {
                return {403, "Access Denied"};
            }
            // List
            return makeReply(tenant->roles());
        }
        // Get one by name
        if (!session->isAllowed(pb::Permission::GET_ROLE, false)) {
            return {403, "Access Denied"};
        }
        if (auto existing = getFromList(tenant->roles(), parsed.target)) {
            return makeReply(*existing);
        }
        return {404, "Role not found"};

    case Request::Type::POST:
        if (!session->isAllowed(pb::Permission::CREATE_ROLE, false)) {
            return {403, "Access Denied"};
        }
        if (auto existing = getFromList(tenant->roles(), role.name())) {
            return {409, "Role already exists"};
        }

        *tenant->add_roles() = role;
        server().auth().upsertTenant(toLower(tenant->id()), *tenant, false);
        return makeReply(role, 201);

    case Request::Type::PUT:
        if (auto existing = getFromList(tenant->roles(), parsed.target)) {
            if (!session->isAllowed(pb::Permission::UPDATE_ROLE, false)) {
                return {403, "Access Denied"};
            }
            removeFromList(tenant->mutable_roles(), parsed.target);
        } else {
            if (!session->isAllowed(pb::Permission::CREATE_ROLE, false)) {
                return {403, "Access Denied"};
            }
            rcode = 201;
        }
        assert(role.has_name());
        *tenant->add_roles() = role;
        server().auth().upsertTenant(toLower(tenant->id()), *tenant, false);
        return makeReply(role, rcode);

    case Request::Type::DELETE:
        if (!session->isAllowed(pb::Permission::DELETE_ROLE, false)) {
            return {403, "Access Denied"};
        }
        if (auto existing = getFromList(tenant->roles(), toLower(parsed.target))) {
            removeFromList(tenant->mutable_roles(), parsed.target);
            server().auth().upsertTenant(toLower(tenant->id()), *tenant, false);
            return {};
        }
        return {404, "Role not found"};

    default:
        return {400, "Invalid method"};
    }
}

Response RestApi::onUser(const yahat::Request &req, const Parsed &parsed)
{
    auto [res, session, tenant, all] = getSessionAndTenant(req, server());
    if (res) {
        return *res;
    }
    int rcode = 200;

    auto lcTarget = toLower(parsed.target);

    pb::User user;
    if (req.expectBody()) {
        if (!fromJson(req.body, user)) {
            return {400, "Failed to parse json to a user"};
        }

        if (req.type == Request::Type::POST) {
            if (!user.has_name()) {
                return {400, "The user must have a name"};
            }
            lcTarget = toLower(user.name());
        } else if (parsed.target.empty()) {
            return {400, "Target must contain the user-name"};
        }

        if (!user.has_id()) {
            user.set_id(newUuidStr());
        }
    }

    switch(req.type) {
    case Request::Type::GET:
        if (parsed.target.empty()) {
            if (!session->isAllowed(pb::Permission::LIST_USERS, false)) {
                return {403, "Access Denied"};
            }
            // List
            return makeReply(tenant->users());
        }
        // Get one by name
        if (!session->isAllowed(pb::Permission::GET_USER, false)) {
            return {403, "Access Denied"};
        }

get_user:
        if (auto existing = getFromList(tenant->users(), lcTarget)) {
            return makeReply(*existing, rcode);
        }
        return {404, "User not found"};

    case Request::Type::POST:
        if (!session->isAllowed(pb::Permission::CREATE_USER, false)) {
            return {403, "Access Denied"};
        }
        if (auto existing = getFromList(tenant->users(), toLower(user.name()))) {
            return {409, "user already exists"};
        }

        *tenant->add_users() = user;
        server().auth().upsertTenant(toLower(tenant->id()), *tenant, false);
        rcode = 201;
        // Get the committed data, as upsertTenant may change the user data, like
        // calculating a hash from the password.
        tenant = server().auth().getTenant(tenant->id());
        if (!tenant) {
            return {500, "Failed to fetch tenant after update."};
        }
        goto get_user;

    case Request::Type::PUT:
        if (auto existing = getFromList(tenant->users(), lcTarget)) {
            if (!session->isAllowed(pb::Permission::UPDATE_USER, false)) {
                return {403, "Access Denied"};
            }
            removeFromList(tenant->mutable_users(), lcTarget);
        } else {
            if (!session->isAllowed(pb::Permission::CREATE_USER, false)) {
                return {403, "Access Denied"};
            }
            rcode = 201;
        }
        assert(user.has_name());
        lcTarget = user.name();
        *tenant->add_users() = user;
        if (server().auth().upsertTenant(toLower(tenant->id()), *tenant, false)) {
            rcode = 201;
        }
        // Get the committed data, as upsertTenant may change the user data, like
        // calculating a hash from the password.
        tenant = server().auth().getTenant(tenant->id());
        if (!tenant) {
            return {500, "Failed to fetch tenant after update."};
        }
        goto get_user;

    case Request::Type::DELETE:
        if (!session->isAllowed(pb::Permission::DELETE_USER, false)) {
            return {403, "Access Denied"};
        }
        if (auto existing = getFromList(tenant->users(), lcTarget)) {
            removeFromList(tenant->mutable_users(), lcTarget);
            server().auth().upsertTenant(toLower(tenant->id()), *tenant, false);
            return {};
        }
        return {404, "User not found"};

    default:
        return {400, "Invalid method"};
    }
}

Response RestApi::onZone(const Request &req, const RestApi::Parsed &parsed)
{
    auto [res, session, tenant, all] = getSessionAndTenant(req, server());
    if (res) {
        return *res;
    }
    auto trx = resource_.transaction();
    auto lowercaseFqdn = toLower(parsed.target);
    auto exists = trx->zoneExists(lowercaseFqdn);
    int rcode = 200;

    switch(req.type) {
//    case Request::Type::GET: {
//        assert(parsed.target.empty()); // Should have been directed to listZones!
//        if (!session->isAllowed(pb::Permission::LIST_RRS, lowercaseFqdn)) {
//            return {403, "Access Denied"};
//        }



//    } break;
    case Request::Type::POST: {
        if (!session->isAllowed(pb::Permission::CREATE_ZONE, lowercaseFqdn)) {
            return {403, "Access Denied"};
        }
        if (exists) {
            return {409, "The zone already exists"};
        }

        auto json = parseJson(req.body);

        // check that the rrs include soa and 2 ns (and that one is primary in soa)
        validateZone(json);

        // TODO: If configured to do so, add name-servers for the zone and notify the
        //       slave servers about their new responsibility.

        // Build binary buffer
        StorageBuilder sb;
        uint32_t ttl = config_.default_ttl;
        build(parsed.target, ttl, sb, json);

        try {
            trx->write({lowercaseFqdn, key_class_t::ENTRY}, sb.buffer(), true);
        } catch(const AlreadyExistException&) {
            return {409, "The zone already exists"};
        }

        server().auth().addZone(*trx, lowercaseFqdn, session->tenant());
        rcode = 201;

    } break;
    case Request::Type::DELETE: {
        if (!session->isAllowed(pb::Permission::DELETE_ZONE, lowercaseFqdn)) {
            return {403, "Access Denied"};
        }
        if (!exists) {
            return {404, "The zone don't exist"};
        }
        try {
            trx->remove({lowercaseFqdn, key_class_t::ENTRY}, true);
        } catch(const NotFoundException&) {
            return {404, "The zone don't exist"};
        }

        server().auth().deleteZone(*trx, lowercaseFqdn, session->tenant());
    } break;
    default:
        return {405, "Only POST and DELETE is valid for 'zone' entries"};
    }

    trx->commit();
    const auto repl_id = trx->replicationId();

    if (auto waited = waitForReplication(req, repl_id)) {
        return makeReplyWithReplStatus(200, *waited);
    }

    return {rcode, "OK"};
}

Response RestApi::onResourceRecord(const Request &req, const RestApi::Parsed &parsed)
{
    auto [res, session, tenant, all] = getSessionAndTenant(req, server());
    if (res) {
        return *res;
    }

    StorageBuilder sb;
    auto trx = resource_.transaction();
    const auto lowercaseFqdn = toFqdnKey(parsed.target);
    // Get the zone
    auto existing = trx->lookupEntryAndSoa(lowercaseFqdn);

    if (existing.isSame()) {
        assert(existing.soa().header().flags.soa);
        assert(existing.rr().header().flags.soa);

        if (req.type == Request::Type::POST || req.type == Request::Type::DELETE) {
            return {409, "Please use the 'zone' rather than the 'rr' endpoint to create or delete zones."};
        }
    }

    if (!existing.hasSoa()) {
        return {404, "Not authorative for zone"};
    }

    size_t soa_zone_len = 0; // Part of the fqdn that forms the zone's fqdn. 0 = zone/same.
    optional<bool> need_to_update_zrr; // true = update, false = delete, unset = nothing.

    if (!existing.isSame()) {
        assert(existing.soa().begin()->type() == TYPE_SOA);
        assert(existing.soa().begin()->labels().size() > 0);
        soa_zone_len = existing.soa().begin()->labels().size() -1;
        sb.setZoneLen(soa_zone_len);
    }

    if (req.expectBody()) {
        build(parsed.target, config_.default_ttl, sb, parseJson(req.body));
        checkSrv(sb.buffer(), *trx);
    }

    bool need_version_increment = false;

    optional<StorageBuilder> merged;
    const auto& oldData = existing.rr();
    Entry newData;
    std::optional<RrSoa> newSoa;
    const auto oldSoa = existing.soa().getSoa();

    // Apply change
    switch(req.type) {
    case Request::Type::GET: {
        if (!session->isAllowed(pb::Permission::READ_RR, lowercaseFqdn)) {
            return {403, "Access Denied"};
        }
        if (!existing.hasRr()) {
            return {404, "Not Found"};
        }

        boost::json::object json;
        json["rcode"] = 200;
        json["error"] = false;
        json["message"] = "";
        json["value"] = toJson(existing.rr());

        return {200, "OK", boost::json::serialize(json)};
    } break;

    case Request::Type::POST: {
        if (!session->isAllowed(pb::Permission::CREATE_RR, lowercaseFqdn)) {
            return {403, "Access Denied"};
        }
        if (existing.hasRr()) {
            return {409, "The rr already exists"};
        }

        need_version_increment = true;
        assert(existing.hasSoa());
        assert(!existing.isSame());

        try {
            trx->write({lowercaseFqdn, key_class_t::ENTRY}, sb.buffer(), true);
            need_to_update_zrr = true;

            if (config_.dns_enable_ixfr) {
                newData = {sb.buffer()};
            }
        } catch(const AlreadyExistException&) {
            return {409, "The rr already exists"};
        }

    } break;

    case Request::Type::PUT: {
        if (!session->isAllowed(pb::Permission::UPDATE_RR, lowercaseFqdn)) {
            return {403, "Access Denied"};
        }
put:
        if (existing.isSame()) {
            sb.incrementSoaVersion(existing.soa());
            newSoa = sb.soa();
            assert(newSoa.has_value());
        } else {
            need_version_increment = true;
        }
        trx->write({lowercaseFqdn, key_class_t::ENTRY}, sb.buffer(), false);
        need_to_update_zrr = true;

        if (config_.dns_enable_ixfr) {
            newData = {sb.buffer()};
        }
    } break;

    case Request::Type::PATCH: {
        if (!session->isAllowed(pb::Permission::UPDATE_RR, lowercaseFqdn)) {
            return {403, "Access Denied"};
        }
        if (!existing.hasRr()) {
            // No existing data to patch. Just write the new rr's.
            goto put;
        }

        // Merge old and newq rr's. All new rr types are replaced.
        // The rest of the old types remains.
        Entry newRrs{sb.buffer()};

        set<uint16_t> new_types;
        merged.emplace();

        // Add the new rr's to the merged buffer
        for(const auto& rr : newRrs) {
            merged->createRr(lowercaseFqdn, rr.type(), rr.ttl(), rr.rdata());
            new_types.insert(rr.type());
        }

        // Add the relevant old rr's to the merged buffer
        for(const auto& rr : existing.rr()) {
            if (new_types.find(rr.type()) == new_types.end()) {
                merged->createRr(lowercaseFqdn, rr.type(), rr.ttl(), rr.rdata());
            }
        }

        if (existing.isSame()) {
            merged->incrementSoaVersion(existing.soa());
        } else {
            need_version_increment = true;
            assert(existing.soa().begin()->type() == TYPE_SOA);
            merged->setZoneLen(existing.soa().begin()->labels().size() -1);
        }

        merged->finish();

        trx->write({lowercaseFqdn, key_class_t::ENTRY}, merged->buffer(), false);
        need_to_update_zrr = true;

        if (config_.dns_enable_ixfr) {
            newData = {merged->buffer()};
        }
    } break;

    case Request::Type::DELETE: {
        if (!session->isAllowed(pb::Permission::DELETE_RR, lowercaseFqdn)) {
            return {403, "Access Denied"};
        }
        if (!existing.hasRr()) {
            return {404, "The rr don't exist"};
        }

        if (!parsed.operation.empty()) {
            merged.emplace();
            const auto filter = makeRrFilter(parsed.operation);
            for(auto& rr : existing.rr()) {
                // Add eveything *not* matching the filter
                if (find(filter.begin(), filter.end(), rr.type()) == filter.end()) {
                    merged->createRr(lowercaseFqdn, rr.type(), rr.ttl(), rr.rdata());
                }
            }

            if (existing.isSame()) {
                merged->incrementSoaVersion(existing.soa());
            } else {
                need_version_increment = true;
                assert(existing.soa().begin()->type() == TYPE_SOA);
                merged->setZoneLen(existing.soa().begin()->labels().size() -1);
            }

            merged->finish();

            // Only write back the changed entry if it still contains some RR's
            if (merged->rrCount()) {
                trx->write({lowercaseFqdn, key_class_t::ENTRY}, merged->buffer(), false);

                if (config_.dns_enable_ixfr) {
                    newData = {merged->buffer()};
                }

                break;
            }

            // Fall back to delete the entry!
        }

        try {
            trx->remove({lowercaseFqdn, key_class_t::ENTRY}, false);
            need_version_increment = true;
            need_to_update_zrr = false;
        } catch(const NotFoundException&) {
            return {404, "The rr don't exist"};
        }
    } break;
    default:
        return {405, "Operation is not implemented"};
    }

    string lowercaseSoaFqdn;

    StorageBuilder soaSb;
    if (need_version_increment) {
        assert(!existing.isSame());

        // We need to copy the Entry containing the soa and then increment the version
        auto soa_fqdn = labelsToFqdnKey(existing.soa().begin()->labels());
        for(const auto& rr : existing.soa()) {
            soaSb.createRr(soa_fqdn, rr.type(), rr.ttl(), rr.rdata());
        }
        soaSb.incrementSoaVersion(existing.soa());
        soaSb.finish();

        lowercaseSoaFqdn = toLower(existing.soa().begin()->labels().string());
        LOG_TRACE << "Incrementing soa version for " << lowercaseSoaFqdn;
        trx->write({lowercaseSoaFqdn, key_class_t::ENTRY}, soaSb.buffer(), false);
        newSoa = soaSb.soa();
    }

    if (config_.dns_enable_ixfr) {
        if (lowercaseSoaFqdn.empty()) {
            lowercaseSoaFqdn = toLower(existing.soa().begin()->labels().string());
        }
        assert(newSoa.has_value());
        assert(newSoa->serial() > oldSoa.serial());
        addDiff(lowercaseSoaFqdn, oldSoa, newSoa.value(), oldData, newData, *trx);
    }

    if (need_to_update_zrr) {
        server().auth().updateZoneRrIx(*trx, lowercaseFqdn, soa_zone_len, *need_to_update_zrr);
    }

    trx->commit();
    const auto repl_id = trx->replicationId();
    trx.reset();

    if (config_.dns_enable_notify) {
        try {
            assert(newSoa.has_value());
            LOG_TRACE << "RestApi::onResourceRecord - Notifying slave servers about update for "
                      << lowercaseSoaFqdn << " with SOA version " << newSoa->serial();
            server_->notifications().notify(lowercaseSoaFqdn);
        } catch(const exception& ex) {
            LOG_WARN << "RestApi::onResourceRecord - Failed to notify slave servers about update of zone "
                     << lowercaseSoaFqdn;
        }
    }

    auto rcode = 200;

    if (!existing.hasRr()) {
        rcode = 201;
    }

    if (auto waited = waitForReplication(req, repl_id)) {
        return makeReplyWithReplStatus(rcode, *waited);
    }

    return {rcode, "OK"};
}

Response RestApi::onConfigMaster(const Request &req, const RestApi::Parsed &parsed)
{
    if (!hasAccess(req, pb::Permission::CONFIG_SLAVE)) {
        return {403, "Access Denied"};
    }

    pb::SlaveZone zone;
    if (req.expectBody() && !fromJson(req.body, zone)) {
        return {400, "Failed to parse json payload into a Zone object"};
    }

    try {
        switch(req.type) {
        case Request::Type::GET:
            server().slave().getZone(parsed.target, zone);
            return {200, "OK", toJson(zone)};
        case Request::Type::POST:
            server().slave().addZone(parsed.target, zone);
            break;
        case Request::Type::PUT:
            server().slave().replaceZone(parsed.target, zone);
            break;
        case Request::Type::PATCH:
            server().slave().mergeZone(parsed.target, zone);
            break;
        case Request::Type::DELETE:
            server().slave().deleteZone(parsed.target);
            return {200, "OK"};
        default:
            return {400, "Invalid method"};
        }
    } catch (const exception& ex) {
        LOG_WARN << "Exception while processing config/master request "
                 << req.uuid << ": " << ex.what();
        return {500, "Server Error/ "s + ex.what()};
    }

    return {200, "OK"};
}

Response RestApi::onBackup(const yahat::Request &req, const Parsed &parsed)
{
    try {
        switch(req.type) {
        case Request::Type::GET:
            return listBackups(req, parsed);
        case Request::Type::POST:
            if (parsed.target.empty()) {
                return startBackup(req, parsed);
            } else if (parsed.operation == "verify") {
                return verifyBackup(req, parsed);
            }
            break;
        case Request::Type::DELETE:
            return deleteBackups(req, parsed);
        default:
            return {400, "Invalid method"};
        }
    } catch (const exception& ex) {
        LOG_WARN << "Exception while processing config/master request "
                 << req.uuid << ": " << ex.what();
        return {500, "Server Error/ "s + ex.what()};
    }

    return {404, "Not Found"};

}

void RestApi::checkSrv(span_t span, ResourceIf::TransactionIf& trx)
{
    if (!config_.dns_validate_srv_targets_locally) {
        return;
    }

    Entry e{span};
    set<string> targets;

    for(const auto& rr : e) {
        if (rr.type() == TYPE_SRV) {
            RrSrv srv{span, rr.offset()};
            targets.insert(toLower(srv.target().string()));
        }
    }

    for (const auto& target : targets) {
        auto e = trx.lookup(target);
        bool found_adress_rr = false;
        for(const auto& err : e) {
            const auto type = err.type();
            if (type == TYPE_A || type == TYPE_AAAA) {
                found_adress_rr = true;
                break;
            }
        }
\
        LOG_DEBUG << "RestApi::checkSrv target " << target
                  << " in Srv for " << e.begin()->labels().string()
                  << " is not pointing to a fqdn with A or AAAA records on this server.";

        if (!found_adress_rr) {
            throw Response{400, "SRV records' targets must point to an existing fqdn with address record(s)"};
        }
    }
}

bool RestApi::hasAccess(const yahat::Request &req, pb::Permission perm) const noexcept
{
    if (auto session = getSession(req)) {
        return session->isAllowed(perm, false);
    }

    return false;
}

bool RestApi::hasAccess(const yahat::Request &req,
                        std::string_view lowercaseFqdn,
                        pb::Permission perm) const noexcept
{
    if (auto session = getSession(req)) {
        return session->isAllowed(perm, lowercaseFqdn, false);
    }

    return false;
}

Response RestApi::listTenants(const yahat::Request &req, const Parsed& /*parsed*/)
{


    auto trx_ = server().resource().transaction();
    auto& trx = dynamic_cast<RocksDbResource::Transaction&>(*trx_);

    boost::json::object out;
    out["error"] = false;
    out["status"] = 200;
    auto& tenants = out["value"] = boost::json::array{};

    const auto page_size = getPageSize(req);
    const auto kind = getKindOfListing(req);

    size_t count = 0;

    auto on_tenant = [&](ResourceIf::TransactionIf::key_t key, span_t value) mutable {
        pb::Tenant tenant;
        if (!tenant.ParseFromArray(value.data(), value.size())) {
            LOG_WARN << "RestApi::listTenants - Failed to parse tenant "
                     << key << " into an object!";
            throw Response{500, "Internal Server Error - failed to deserialize object"};
        }

        if (kind == KindOfListing::ID) {
            tenants.as_array().emplace_back(tenant.id());
        } else {
            // DEFAULT kind listing
            boost::json::object item;
            item["id"] = tenant.id();
            item["active"] = tenant.active();
            item["root"] = tenant.root();
            auto& users = item["users"] = boost::json::array{};
            for(const auto& user : tenant.users()) {
                users.as_array().emplace_back(user.name());
            }

            auto& roles = item["roles"] = boost::json::array{};
            for(const auto& role: tenant.roles()) {
                roles.as_array().emplace_back(role.name());
            }

            auto& perms = item["allowedPermissions"] = boost::json::array{};
            for(auto perm : tenant.allowedpermissions()) {
                perms.as_array().emplace_back(pb::Permission_Name(perm));
            }

            tenants.as_array().emplace_back(item);
        }
        return ++count <= page_size;
    };

    if (auto from = getFrom(req); !from.end()) {
        // From last key
        ResourceIf::RealKey key{from, key_class_t::TENANT};
        trx.iterateFromPrevT(key, ResourceIf::Category::ACCOUNT, std::move(on_tenant));
    } else {
        // From start
        ResourceIf::RealKey key{"", key_class_t::TENANT};
        trx.iterateT(key, ResourceIf::Category::ACCOUNT, std::move(on_tenant));
    }

    return {200, "Ok", boost::json::serialize(out)};
}

Response RestApi::listZones(const yahat::Request &req, const Parsed &parsed)
{
    auto [res, session, tenant, all] = getSessionAndTenant(req, server(), true);
    if (res) {
        return *res;
    }

    const auto tenant_id = tenant->id();

    auto lowercaseFqdn = toLower(parsed.target);

    if (!session->isAllowed(pb::Permission::LIST_ZONES, lowercaseFqdn)) {
        return {403, "Access Denied"};
    }

    auto page_size = getPageSize(req);
    auto from = getFrom(req);
    if (!from.empty()) {
        // From last key
        if (!validateFqdn(from)) {
            return {400, "Invalid fqdn in 'from' argument"};
        }
    };

    // Return a list of zones
    optional<ResourceIf::RealKey> key;
    if (all) {
        key.emplace(from, key_class_t::ZONE);
    } else {
        key.emplace(tenant_id, from, key_class_t::TZONE);
    }

    LOG_TRACE_N << "Serch Key: " << *key;
    size_t count = 0;
    bool more = false;
    string last_key;

    string body;
    {
        boost::json::array zone_list;

        server().db().dbTransaction()->iterateFromPrevT(
            *key, ResourceIf::Category::ACCOUNT,
            [&zone_list, &tenant_id, all, page_size, &count, &more, this](ResourceIf::TransactionIf::key_t key, span_t value) mutable {

            /* In the normal case, we are listig zones for a tenant.
             * In that case we can use string_view directly to the data in the key.
             * Therefore, we use use string_views to read the relevant data.
             * If we list all data, we put the data in the strings, and point
             * the views to those strings.
             */
            string_view zone, ztenant;
            string zone_buf, tenant_buf;

            if (all) [[unlikely]] {
                // The fqdn is reversed, so we need a string buffer to get the zone in readable format.
                zone_buf = key.dataAsString();
                zone = zone_buf;

                pb::Zone z;
                if (!z.ParseFromArray(value.data(), value.size())) {
                    LOG_WARN_N << "Failed to parse tenant " << key << " into an object!";
                    throw Response{500, "Internal Server Error - failed to deserialize object"};
                }

                // protobuf can't return views over strings yet...
                tenant_buf = z.tenantid();
                ztenant = tenant_buf;
            } else {
                tie(ztenant, zone) = key.getFirstAndSecondStr();
                LOG_TRACE_N << "Key=" << key << format(", tenant={}, zone={}", ztenant, zone);

                if (!all && !compareCaseInsensitive(tenant_id, ztenant)) {
                    return false; // We rolled over to another tenant
                }
            }

            if (++count > page_size) {
                LOG_TRACE_N << "Page size full at tenant " << ztenant << " zone " << zone;
                more = true;
                return false;
            }

            boost::json::object o;
            o["zone"] = zone;
            if (all) {
                o["tenant"] = ztenant;
            }

            zone_list.push_back(std::move(o));

            return true;
        });

        boost::json::object json;
        json["rcode"] = 200;
        json["error"] = false;
        json["message"] = "";
        json["more"] = more;
        json["limit"] = page_size;
        json["value"] = std::move(zone_list);
        body = boost::json::serialize(json);
    }

    return {200, "OK", body};
}

Response RestApi::listZone(const yahat::Request &req, const Parsed &parsed)
{
    auto [res, session, tenant, all] = getSessionAndTenant(req, server(), true);
    if (res) {
        return *res;
    }

    const auto tenant_id = tenant->id();

    auto lowercaseFqdn = toLower(parsed.target);

    if (!session->isAllowed(pb::Permission::LIST_ZONES, lowercaseFqdn)) {
        return {403, "Access Denied"};
    }

    auto page_size = getPageSize(req);
    auto from = getFrom(req);
    if (!from.empty()) {
        // From last key
        if (!validateFqdn(from)) {
            return {400, "Invalid fqdn in 'from' argument"};
        }
    };

    // Return a list of zones
    ResourceIf::RealKey key {from, key_class_t::ZRR};

    LOG_TRACE_N << "Serch Key: " << key;
    size_t count = 0;
    bool more = false;
    string last_key;

    string body;
    {
        boost::json::array list;
        server().db().dbTransaction()->iterateFromPrevT(
            key, ResourceIf::Category::ACCOUNT,
            [&list, &tenant_id, all, page_size, &count, &more, this](ResourceIf::TransactionIf::key_t key, span_t value) mutable {

                auto [zone, fqdn] = key.getFirstAndSecondStr();
                if (++count > page_size) {
                    LOG_TRACE_N << "Page size full at fqdn " << fqdn << " zone " << zone;
                    more = true;
                    return false;
                }

                list.push_back(boost::json::string{fqdn});
                return true;
            });

        boost::json::object json;
        json["rcode"] = 200;
        json["error"] = false;
        json["message"] = "";
        json["more"] = more;
        json["limit"] = page_size;
        json["value"] = std::move(list);
        body = boost::json::serialize(json);
    }

    return {200, "OK", body};
}

size_t RestApi::getPageSize(const yahat::Request &req) const
{
    if (auto it = req.arguments.find("limit"); it != req.arguments.end()) {
        if (size_t psize = std::stoi(string{it->second})) {
            return min(psize, server().config().rest_max_page_size);
        }
    }

    return server().config().rest_default_page_size;
}

string_view RestApi::getFrom(const yahat::Request &req) const
{
    if (auto it = req.arguments.find("from"); it != req.arguments.end()) {
        // From last key
        return it->second;
    }
    return {};
}

std::optional<bool> RestApi::waitForReplication(const yahat::Request &req, uint64_t trxid)
{
    if (auto it = req.arguments.find("wait"); it != req.arguments.end()) {
        if (auto seconds = std::stoi(string{it->second})) {
            if (!trxid || !server().isPrimaryReplicationServer()) {
                // Not for replication
                return false;
            }

            LOG_TRACE_N << "Waiting for replication up to " << seconds << " seconds...";
            boost::system::error_code ec;
            server().primaryReplication().waiter().wait(
                trxid, chrono::seconds{seconds}, (*req.yield)[ec]);
            LOG_TRACE_N << "Done waiting for replication. wait status: " << ec;
            return !ec.failed();
        }
    }

    return {};
}

Response RestApi::startBackup(const yahat::Request &req, const Parsed &parsed)
{
    if (!hasAccess(req, pb::Permission::CREATE_BACKUP)) {
        return {403, "Access Denied"};
    }

    const auto uuid = newUuid();

    std::string db_path;

    const auto body = boost::json::parse(req.body);
    if (body.is_object()) {
        if (auto path = body.as_object().if_contains("path")) {
            db_path = string{path->as_string()};
        }
    }

    bool syncFirst = true;

    server().db().startBackup(db_path, syncFirst, uuid);

    boost::json::object json, v;

    v["uuid"] = toLower(boost::uuids::to_string(uuid));
    json["rcode"] = 201;
    json["error"] = false;
    json["message"] = "Backup operation was started.";
    json["value"] = std::move(v);

    return {201, "OK", boost::json::serialize(json)};
}

Response RestApi::verifyBackup(const yahat::Request &req, const Parsed &parsed)
{
    if (!hasAccess(req, pb::Permission::VERIFY_BACKUP)) {
        return {403, "Access Denied"};
    }

    std::string db_path;
    const auto body = boost::json::parse(req.body);
    if (body.is_object()) {
        if (auto path = body.as_object().if_contains("path")) {
            db_path = string{path->as_string()};
        }
    }

    const auto id = std::stoi(string{parsed.target});

    string message;
    if (server().db().verifyBackup(id, db_path, &message)) {
        return {200, "OK"};
    }

    boost::json::object json;
    json["rcode"] = 200;
    json["error"] = true;
    json["message"] = format("Verification of backup {} failed with error: '{}'", id, message);

    return {200, "Verification failed", boost::json::serialize(json)};
}

Response RestApi::listBackups(const yahat::Request &req, const Parsed &parsed)
{
    if (!hasAccess(req, pb::Permission::LIST_BACKUPS)) {
        return {403, "Access Denied"};
    }

    boost::json::object json, meta;
    std::string backup_dir;
    if (auto it = req.arguments.find("path"); it != req.arguments.end()) {
        backup_dir = string{it->second};
    }

    server().db().listBackups(meta, backup_dir);

    json["rcode"] = 200;
    json["error"] = false;
    json["value"] = std::move(meta);

    return {200, "OK", boost::json::serialize(json)};
}

Response RestApi::deleteBackups(const yahat::Request &req, const Parsed &parsed)
{
    if (!hasAccess(req, pb::Permission::DELETE_BACKUP)) {
        return {403, "Access Denied"};
    }

    std::string backup_dir;
    if (auto it = req.arguments.find("path"); it != req.arguments.end()) {
        backup_dir = string{it->second};
    }

    if (parsed.target.empty()) {
        int keep = 0;

        if (auto it = req.arguments.find("keep"); it != req.arguments.end()) {
            keep = std::stoi(string{it->second});
        }

        server().db().purgeBackups(keep, backup_dir);
        return {200, "OK"};
    }

    const auto id = std::stoi(string{parsed.target});
    if (server().db().deleteBackup(id, backup_dir)) {
        return {200, format("OK. Backup {} was deleted.", id)};
    }

    return {404, format("Backup id {} not found", id)};
}


} // ns
