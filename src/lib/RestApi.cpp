
#include <set>

#include <boost/json/src.hpp>
#include <boost/unordered/unordered_flat_map.hpp>

#include "RestApi.h"
#include "nsblast/logging.h"
#include "nsblast/DnsMessages.h"
#include "nsblast/util.h"
#include "SlaveMgr.h"
#include "Notifications.h"
#include "nsblast/DnsEngine.h"
#include "proto/nsblast.pb.h"
#include "google/protobuf/util/json_util.h"

using namespace std;
using namespace std::string_literals;
using namespace yahat;

namespace nsblast::lib {

namespace {

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

template <typename T>
std::string toJson(const T& obj) {
    string str;
    auto res = google::protobuf::util::MessageToJsonString(obj, &str);
    if (!res.ok()) {
        LOG_DEBUG << "Failed to convert object to json: "
                 << typeid(T).name() << ": "
                 << res.ToString();
        throw std::runtime_error{"Failed to convertt object to json"};
    }
    return str;
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

    vector<Entry::Iterator> older, newer;
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

} // anon ns

RestApi::RestApi(ApiEngine& apiEngine)
    : config_{apiEngine.config()}, resource_{apiEngine.resource()}
    , api_engine_{&apiEngine}
{
}

RestApi::RestApi(const Config &config, ResourceIf &resource)
    : config_{config}, resource_{resource} {}

Response RestApi::onReqest(const Request &req, const Auth &auth)
{
    const auto p = parse(req);

    if (p.what == "rr") {
        return onResourceRecord(req, p);
    }

    if (p.what == "zone") {
        return onZone(req, p);
    }

    if (p.what == "config") {
        if (p.operation == "master") {
            return onConfigMaster(req, p);
        }
    }

    LOG_DEBUG << "Unknown subpath: " << p.what;
    return {404, "Unknown subpath"};
}

RestApi::Parsed RestApi::parse(const Request &req)
{
    // target syntax: /api/v1/zone|rr/{fqdn}[/verb]
    //                        ^
    //                        +------               = what
    //                        |        -----        = fqdn
    //                        |               ----  = operation
    //                        +-------------------- = base

    Parsed p;
    p.base = req.target;
    p.base = p.base.substr(req.route.size());
    while(!p.base.empty() && p.base.at(0) == '/') {
        p.base = p.base.substr(1);
    }

    if (auto pos = p.base.find('/'); pos != string_view::npos) {
        p.what = p.base.substr(0, pos);

        if (p.base.size() > pos) {
            p.fqdn = p.base.substr(pos + 1);

            if (auto end = p.fqdn.find('/') ; end != string_view::npos) {
                if (p.fqdn.size() > end) {
                    p.operation = p.fqdn.substr(end + 1);
                }
                p.fqdn = p.fqdn.substr(0, end);
            }
        }
    }

    return p;
}

void RestApi::validateSoa(const boost::json::value &json)
{
    auto soa = json.at("soa");
    if (!soa.is_object()) {
        throw Response{400, "'soa' must be a json object"};
    }

    for (string_view key : {"mname", "rname"}) {
        try {
            if (!soa.at(key).is_string()) {
                throw Response{400, "Not a string: "s + string(key)};
            }
        } catch (std::exception& ex) {
            throw Response{400, "Missing "s + string(key)};
        }
    }

    for (string_view key : {"refresh", "retry", "version", "expire", "minimum"}) {
        try {
            if (!soa.at(key).is_int64()) {
                throw Response{400, "Not a number: "s + string(key)};
            }
        } catch (std::exception& ) {
            ; // OK
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
                    const boost::json::value json, bool finish)
{
    static const boost::unordered_flat_map<string_view, function<void(string_view, uint32_t, StorageBuilder&, const boost::json::value&)>>
        handlers = {
    { "ttl", [](string_view fqdn, uint32_t ttl, StorageBuilder& sb, const boost::json::value& v) {
        // Ignore here.
    }},
    {"soa", [](string_view fqdn, uint32_t ttl, StorageBuilder& sb, const boost::json::value& v) {

        // TODO: Set reasonable defaults
        uint32_t refresh = 1000, retry = 1000, expire = 1000, minimum = 1000,
                serial = 1;
        string_view mname, rname;

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

            uint32_t priority = 0, weight = 0, port = 0;
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

        if (!v.if_string()) {
            throw Response{400, "Txt entities must be strings"};
        }
        sb.createTxt(fqdn, ttl, v.as_string());
    }},
    { "hinfo", [](string_view fqdn, uint32_t ttl, StorageBuilder& sb, const boost::json::value& v) {

        if (!v.if_object()) {
            throw Response{400, "Hinfo must be an object"};
        }

        string_view cpu, os;

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

            string_view mbox, txt;

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

Response RestApi::onZone(const Request &req, const RestApi::Parsed &parsed)
{
    auto trx = resource_.transaction();

    auto lowercaseFqdn = toLower(parsed.fqdn);

    auto exists = trx->zoneExists(lowercaseFqdn);

    switch(req.type) {
    case Request::Type::POST: {
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
        build(parsed.fqdn, ttl, sb, json);

        try {
            trx->write({lowercaseFqdn}, sb.buffer(), true);
        } catch(const ResourceIf::AlreadyExistException&) {
            return {409, "The zone already exists"};
        }
    } break;
    case Request::Type::DELETE: {
        if (!exists) {
            return {404, "The zone don't exist"};
        }
        try {
            trx->remove({lowercaseFqdn}, true);
        } catch(const ResourceIf::NotFoundException&) {
            return {404, "The zone don't exist"};
        }
    } break;
    default:
        return {405, "Only POST and DELETE is valid for 'zone' entries"};
    }

    trx->commit();
    return {};
}

Response RestApi::onResourceRecord(const Request &req, const RestApi::Parsed &parsed)
{
    // Validate the request
    StorageBuilder sb;

    auto trx = resource_.transaction();
    const auto lowercaseFqdn = toFqdnKey(parsed.fqdn);
    // Get the zone
    auto existing = trx->lookupEntryAndSoa(lowercaseFqdn);

    if (existing.isSame()) {
        assert(existing.soa().header().flags.soa);
        assert(existing.rr().header().flags.soa);

        if (req.type == Request::Type::POST || req.type == Request::Type::DELETE) {
            return {409, "Please use the 'zone' rather than the 'rr' endpoint to create or delete zones."};
        }
    }

    // TODO: Check that the user has write access to the zone
    if (!existing.hasSoa()) {
        return {404, "Not authorative for zone"};
    }

    if (!existing.isSame()) {
        assert(existing.soa().begin()->type() == TYPE_SOA);
        sb.setZoneLen(existing.soa().begin()->labels().size() -1);
    }

    if (req.expectBody()) {
        build(parsed.fqdn, config_.default_ttl, sb, parseJson(req.body));
        checkSrv(sb.buffer(), *trx);
    }

    bool need_version_increment = false;

    const auto& oldData = existing.rr();
    Entry newData;
    std::optional<RrSoa> newSoa;
    const auto oldSoa = existing.soa().getSoa();

    // Apply change
    switch(req.type) {
    case Request::Type::POST: {
        if (existing.hasRr()) {
            return {409, "The rr already exists"};
        }

        need_version_increment = true;
        assert(existing.hasSoa());
        assert(!existing.isSame());

        try {
            trx->write({lowercaseFqdn}, sb.buffer(), true);

            if (config_.dns_enable_ixfr) {
                newData = {sb.buffer()};
            }
        } catch(const ResourceIf::AlreadyExistException&) {
            return {409, "The rr already exists"};
        }

    } break;

    case Request::Type::PUT: {
put:
        if (existing.isSame()) {
            sb.incrementSoaVersion(existing.soa());
            newSoa = sb.soa();
            assert(newSoa.has_value());
        } else {
            need_version_increment = true;
        }
        trx->write({lowercaseFqdn}, sb.buffer(), false);

        if (config_.dns_enable_ixfr) {
            newData = {sb.buffer()};
        }
    } break;

    case Request::Type::PATCH: {
        if (!existing.hasRr()) {
            // No existing data to patch. Just write the new rr's.
            goto put;
        }

        // Merge old and newq rr's. All new rr types are replaced.
        // The rest of the old types remains.
        Entry newRrs{sb.buffer()};

        set<uint16_t> new_types;
        StorageBuilder merged;

        // Add the new rr's to the merged buffer
        for(const auto& rr : newRrs) {
            merged.createRr(lowercaseFqdn, rr.type(), rr.ttl(), rr.rdata());
            new_types.insert(rr.type());
        }

        // Add the relevant old rr's to the merged buffer
        for(const auto& rr : existing.rr()) {
            if (new_types.find(rr.type()) == new_types.end()) {
                merged.createRr(lowercaseFqdn, rr.type(), rr.ttl(), rr.rdata());
            }
        }

        if (existing.isSame()) {
            merged.incrementSoaVersion(existing.soa());
        } else {
            need_version_increment = true;
            assert(existing.soa().begin()->type() == TYPE_SOA);
            merged.setZoneLen(existing.soa().begin()->labels().size() -1);
        }

        merged.finish();

        trx->write({lowercaseFqdn}, merged.buffer(), false);

        if (config_.dns_enable_ixfr) {
            newData = {merged.buffer()};
        }
    } break;

    case Request::Type::DELETE: {
        if (!existing.hasRr()) {
            return {404, "The rr don't exist"};
        }
        try {
            trx->remove({lowercaseFqdn}, false);
            need_version_increment = true;
        } catch(const ResourceIf::NotFoundException&) {
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
        trx->write({lowercaseSoaFqdn}, soaSb.buffer(), false);
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

    trx->commit();
    trx.reset();

    if (!existing.hasRr()) {
        return {201, "OK"};
    }

    if (config_.dns_enable_notify) {
        try {
            api_engine_->dns().notifications().notify(lowercaseSoaFqdn);
        } catch(const exception& ex) {
            LOG_WARN << "RestApi::onResourceRecord - Failed to notify slave servers about update of zone "
                     << lowercaseSoaFqdn;
        }
    }

    return  {};
}

Response RestApi::onConfigMaster(const Request &req, const RestApi::Parsed &parsed)
{
    pb::Zone zone;
    if (req.expectBody() && !fromJson(req.body, zone)) {
        return {400, "Failed to parse json payload into a Zone object"};
    }

    try {
        switch(req.type) {
        case Request::Type::GET:
            apiEngine().slaveMgr().getZone(parsed.fqdn, zone);
            return {200, "OK", toJson(zone)};
        case Request::Type::POST:
            apiEngine().slaveMgr().addZone(parsed.fqdn, zone);
            break;
        case Request::Type::PUT:
            apiEngine().slaveMgr().replaceZone(parsed.fqdn, zone);
            break;
        case Request::Type::PATCH:
            apiEngine().slaveMgr().mergeZone(parsed.fqdn, zone);
            break;
        case Request::Type::DELETE:
            apiEngine().slaveMgr().deleteZone(parsed.fqdn);
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


} // ns
