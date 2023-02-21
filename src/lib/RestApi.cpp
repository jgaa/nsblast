
#include <set>

#include <boost/json/src.hpp>

#include "RestApi.h"
#include "nsblast/logging.h"
#include "nsblast/DnsMessages.h"
#include "nsblast/util.h"

using namespace std;
using namespace std::string_literals;
using namespace yahat;

namespace nsblast::lib {

namespace {

} // anon ns

RestApi::RestApi(const Config &config, ResourceIf& resource)
    : config_{config}, resource_{resource}
{
}

Response RestApi::onReqest(const Request &req, const Auth &auth)
{
    const auto p = parse(req);

    if (p.what == "rr") {
        return onResourceRecord(req, p);
    }

    if (p.what == "zone") {
        return onZone(req, p);
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
    for (string_view key : {"mname", "rname"}) {
        try {
            if (!soa.at(key).is_string()) {
                throw Response{400, "Not a string: "s + string(key)};
            }
        } catch (std::exception& ex) {
            throw Response{400, "Missing "s + string(key)};
        }
    }

    for (string_view key : {"ttl", "refresh", "retry", "version", "expire", "minimum"}) {
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

    const auto& primary_ns = json.at_pointer("/soa/rname");
    assert(primary_ns.is_string());

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
            if (v.as_string() == primary_ns) {
                has_primary = true;
            }
        }
    } catch(const exception& ex) {
        throw Response{400, "Missing Json element "s + string(ckey)};
    }

    if (!has_primary) {
        throw Response{400, "soa.rname must be one of the ns entries"};
    }
}

void RestApi::build(string_view fqdn, uint32_t ttl, StorageBuilder& sb, const boost::json::value json)
{
    static const map<string_view, function<void(string_view, uint32_t, StorageBuilder&, const boost::json::value&)>>
        handlers = {
    {"soa", [](string_view fqdn, uint32_t ttl, StorageBuilder& sb, const boost::json::value& v) {

        // TODO: Set reasonable defaults
        uint32_t soattl = ttl, refresh = 1000, retry = 1000, expire = 1000, minimum = 1000,
                serial = 1;
        string_view mname, rname;

        map<string_view, uint32_t *> nentries = {
            {"ttl", &soattl},
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

        sb.createSoa(fqdn, ttl, mname, rname, serial, refresh, retry, expire, minimum);
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
    { "cname", [](string_view fqdn, uint32_t ttl, StorageBuilder& sb, const boost::json::value& v) {

        if (!v.if_string()) {
            throw Response{400, "Cname entities must be strings"};
        }
        sb.createCname(fqdn, ttl, v.as_string());
    }},
    { "mx", [](string_view fqdn, uint32_t ttl, StorageBuilder& sb, const boost::json::value& v) {
        uint16_t priority = 10;
        string_view host;

        for(const auto& e : v.as_object()) {
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
    }}
    };


    LOG_DEBUG << "json is_object=" << json.is_object()
              << ", kind=" << json.kind()
              << ", json=" << boost::json::serialize(json);


    for(const auto& obj : json.as_object()) {
        if (auto it = handlers.find(obj.key()); it != handlers.end()) {
            it->second(fqdn, ttl, sb, obj.value());
        } else {
            throw Response{400, "Unknown entity: "s + string(obj.key())};
        }
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

        // Build binary buffer
        StorageBuilder sb;
        uint32_t ttl = 0; // TODO: Set to some supplied or default value
        build(parsed.fqdn, ttl, sb, json);

        try {
            trx->write(lowercaseFqdn, sb.buffer(), true);
        } catch(const ResourceIf::AlreadyExistException&) {
            return {409, "The zone already exists"};
        }
    } break;
    case Request::Type::DELETE: {
        if (!exists) {
            return {404, "The zone don't exist"};
        }
        try {
            trx->remove(lowercaseFqdn, true);
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
    auto lowercaseFqdn = toLower(parsed.fqdn);
    // Get the zone
    auto existing = trx->lookupEntryAndSoa(lowercaseFqdn);

    if (existing.isSame()) {
        assert(existing.soa().header().flags.soa);
        assert(existing.rr().header().flags.soa);

        if (req.type == Request::Type::POST || req.type == Request::Type::DELETE) {
            return {409, "Please use the 'zone' rather than the 'rr' endpoint to create or delete zones."};
        }
    }

    // TODO: Check that the user has write access toi the zone
    if (!existing.hasSoa()) {
        return {404, "Not authorative for zone"};
    }

    uint32_t ttl = 0; // TODO: Set to some supplied or default value
    build(parsed.fqdn, ttl, sb, parseJson(req.body));
    if (!existing.isSame()) {
        assert(existing.soa().begin()->type() == TYPE_SOA);
        sb.setZoneLen(existing.soa().begin()->labels().size() -1);
    }
    sb.finish();

    bool need_version_increment = false;

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
            trx->write(lowercaseFqdn, sb.buffer(), true);
        } catch(const ResourceIf::AlreadyExistException&) {
            return {409, "The rr already exists"};
        }
    } break;

    case Request::Type::PUT: {
put:
        if (existing.isSame()) {
            sb.incrementSoaVersion(existing.soa());
        } else {
            need_version_increment = true;
        }
        trx->write(lowercaseFqdn, sb.buffer(), false);
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

        trx->write(lowercaseFqdn, merged.buffer(), false);
    } break;

    case Request::Type::DELETE: {
        if (!existing.hasRr()) {
            return {404, "The rr don't exist"};
        }
        try {
            trx->remove(lowercaseFqdn, false);
        } catch(const ResourceIf::NotFoundException&) {
            return {404, "The rr don't exist"};
        }
    } break;
    default:
        return {405, "Operation is not implemented"};
    }

    if (need_version_increment) {
        assert(!existing.isSame());

        // We need to copy the Entry containing the soa and then increment the version
        StorageBuilder soaSb;
        for(const auto& rr : existing.soa()) {
            soaSb.createRr(lowercaseFqdn, rr.type(), rr.ttl(), rr.rdata());
        }
        soaSb.incrementSoaVersion(existing.soa());
        soaSb.finish();

        const auto lowercaseSoaFqdn = toLower(existing.soa().begin()->labels().string());
        LOG_TRACE << "Incrementing soa version for " << lowercaseSoaFqdn;
        trx->write(lowercaseSoaFqdn, soaSb.buffer(), false);
    }

    trx->commit();

    if (!existing.hasRr()) {
        return {201, "OK"};
    }

    return  {};
}


} // ns
