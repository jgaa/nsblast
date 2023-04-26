
#include "gtest/gtest.h"
#include "RestApi.h"

#include "TmpDb.h"

#include "nsblast/DnsMessages.h"
#include "nsblast/logging.h"

//#include "test_res.h"

using namespace std;
using namespace nsblast;
using namespace nsblast::lib;

namespace {

static constexpr auto DEFAULT_SOA_SERIAL = 1000;

auto getZoneJson() {

    // Note rname: we expect the mapping from email to domain-name to be present in `build()`
    static const auto soa = boost::json::parse(R"({
    "ttl": 1000,
    "soa": {
    "refresh": 1001,
    "retry": 1002,
    "expire": 1003,
    "serial": 1000,
    "minimum": 1004,
    "mname": "ns1.example.com",
    "rname": "hostmaster@example.com"
    },
    "ns": [
    "ns1.example.com",
    "ns2.example.com"
    ]
    })");

    return soa;
}

auto getAJson() {
   boost::json::object json;
   json["a"] = {"127.0.0.1", "127.0.0.2"};
   return boost::json::serialize(json);
}

auto getAJsonZeroTtl() {
   boost::json::object json;
   json["a"] = {"127.0.0.1", "127.0.0.2"};
   json["ttl"] = 0;
   return boost::json::serialize(json);
}

auto getHinfoJson() {
   boost::json::object json, hinfo;

   hinfo["cpu"] = "awesome";
   hinfo["os"] = "minix";

   json["hinfo"] = hinfo;

   return boost::json::serialize(json);
}

auto getRpJson() {
   boost::json::object json, rp;

   rp["mbox"] = "admin.example.com";
   rp["txt"] = "more.info.example.com";

   json["rp"] = rp;

   return boost::json::serialize(json);
}

auto getAfsdbJson() {
    return R"({
    "afsdb": [
        {
          "subtype": 1,
          "host": "foo.example.com"
        },
        {
          "host": "bar.example.com",
          "subtype": 3
        }
    ]
    })";
}

auto getSrvDhcidJson() {
    return R"({
    "dhcid":"AAIBY2/AuCccgoJbsaxcQc9TUapptP69lOjxfNuVAA2kjEA="
    })";
}

auto getSrvJson() {
    return R"({
    "srv": [
        {
          "priority": 1,
          "weight": 200,
          "port": 3000,
          "target": "example.com"
        },
        {
          "priority": 2,
          "weight": 300,
          "port": 4000,
          "target": "example.com"
        }
      ]
    })";
}

auto getSrvNoAddressTargetJson() {
    return R"({
    "srv": [
        {
          "priority": 1,
          "weight": 200,
          "port": 3000,
          "target": "example.com"
        },
        {
          "priority": 2,
          "weight": 300,
          "port": 4000,
          "target": "foo.example.com"
        }
      ]
    })";
}

auto makeRequest(const string& what, string_view fqdn, string json, yahat::Request::Type type) {
    static const string base = "/api/v1";

    LOG_DEBUG << "makeRequest fqdn=" << fqdn << ", what=" << what
              << ", json=" << json;

    std::string full_target = base + "/" + what + "/" + string{fqdn};

    return yahat::Request{nullptr, full_target, "/api/v1", "", "", move(json), type};
}

uint32_t getSoaSerial(string_view fqdn, ResourceIf& db) {

    auto trx = db.transaction();
    auto res = trx->lookupEntryAndSoa(fqdn);
    if (res.hasSoa()) {
        RrSoa soa{res.soa().buffer(), res.soa().begin()->offset()};
        return soa.serial();
    }

    return 0;
}

auto lookup(string_view fqdn, ResourceIf& db) {
    auto trx = db.transaction();
    auto e = trx->lookup(fqdn);

    return e;
}

} // anon ns

TEST(ApiValidate, soaOk) {
    EXPECT_NO_THROW(RestApi::validateSoa(getZoneJson()));
}

TEST(ApiValidate, soaRefreshIsString) {

    auto json = getZoneJson();
    json.at("soa").at("refresh") = "teste";

    EXPECT_THROW(RestApi::validateSoa(json), yahat::Response);
}

TEST(ApiValidate, soaMnameIsNumber) {

    auto json = getZoneJson();
    json.at("soa").at("mname") = 1;

    EXPECT_THROW(RestApi::validateSoa(json), yahat::Response);
}

TEST(ApiValidate, parseJsonOk) {

    auto json = R"({"name":"value"})";

    EXPECT_NO_THROW(RestApi::parseJson(json));
}

TEST(ApiValidate, parseJsonFail) {

    auto json = R"({"name":value"})";

    EXPECT_THROW(RestApi::parseJson(json), yahat::Response);
}

TEST(ApiValidate, zoneOk) {
    EXPECT_NO_THROW(RestApi::validateZone(getZoneJson()));
}

TEST(ApiValidate, zoneWrongMname) {

    auto json = getZoneJson();
    json.at("soa").at("mname") = "foo.example.com";

    EXPECT_THROW(RestApi::validateZone(json), yahat::Response);
    try {
        RestApi::validateZone(json);
    } catch(const yahat::Response& ex) {
        EXPECT_EQ(ex.code, 400);
        EXPECT_EQ(ex.reason, "soa.mname must be one of the ns entries");
    }
}

TEST(ApiValidate, zoneNoNs) {

    auto json = getZoneJson();

    boost::json::object v;
    v["soa"] = json.at("soa");

    EXPECT_THROW(RestApi::validateZone(v), yahat::Response);
    try {
        RestApi::validateZone(v);
    } catch(const yahat::Response& ex) {
        EXPECT_EQ(ex.code, 400);
        EXPECT_EQ(ex.reason, "Missing Json element ns");
    }
}


TEST(ApiValidate, zoneNsIsNotArray) {

    auto json = getZoneJson();

    boost::json::object v;
    v["soa"] = json.at("soa");
    v["ns"] = 123;

    EXPECT_THROW(RestApi::validateZone(v), yahat::Response);
    try {
        RestApi::validateZone(v);
    } catch(const yahat::Response& ex) {
        EXPECT_EQ(ex.code, 400);
        EXPECT_EQ(ex.reason, "Json element 'ns' must be an array of string(s)");
    }
}

TEST(ApiValidate, zoneOnlyOneNs) {

    auto json = getZoneJson();

    boost::json::object v;
    v["soa"] = json.at("soa");
    v["ns"] = boost::json::array{"ns1"};

    EXPECT_THROW(RestApi::validateZone(v), yahat::Response);
    try {
        RestApi::validateZone(v);
    } catch(const yahat::Response& ex) {
        EXPECT_EQ(ex.code, 400);
        EXPECT_EQ(ex.reason, "RFC1036 require at least two nameservers (ns records)");
    }
}

TEST(ApiBuild, ZoneOk) {
    auto json = getZoneJson();

    const string_view fqdn = "example.com";

    StorageBuilder sb;
    EXPECT_NO_THROW(RestApi::build(fqdn, 1000, sb, json));

    Entry entry{sb.buffer()};
    EXPECT_TRUE(entry.begin() != entry.end());

    if (entry.begin() != entry.end()) { // Don't spam me with irrelevant test failures
        EXPECT_TRUE(entry.flags().soa);
        EXPECT_TRUE(entry.flags().ns);
        EXPECT_FALSE(entry.flags().a);
        EXPECT_FALSE(entry.flags().aaaa);
        EXPECT_FALSE(entry.flags().txt);
        EXPECT_FALSE(entry.flags().cname);

        EXPECT_EQ(entry.index().size(), 3); // One soa, two ns
        RrSoa soa{sb.buffer(), entry.begin()->offset()};
        EXPECT_EQ(soa.labels().string(), fqdn);
        EXPECT_EQ(soa.mname().string(), "ns1.example.com");
        EXPECT_EQ(soa.rname().string(), "hostmaster.example.com");
        EXPECT_EQ(soa.ttl(), 1000);
    }
}

TEST(ApiBuild, ZoneUnknownAttr) {
    auto json = getZoneJson();

    json.at("soa").as_object()["dog"] = 123;

    const string_view fqdn = "example.com";

    StorageBuilder sb;
    EXPECT_THROW(RestApi::build(fqdn, 1000, sb, json), yahat::Response);

    try {
        RestApi::validateZone(json);
    } catch(const yahat::Response& ex) {
        EXPECT_EQ(ex.code, 400);
        EXPECT_EQ(ex.reason, "Unknown soa entity: dog");
    }
}

TEST(ApiBuild, aOk) {
    const string_view fqdn = "example.com";
    const string_view ipv4 = "127.0.0.1";

    boost::json::object json;
    json["a"] = boost::json::array{ipv4};

    StorageBuilder sb;
    EXPECT_NO_THROW(RestApi::build(fqdn, 1000, sb, json));

    Entry entry{sb.buffer()};
    EXPECT_TRUE(entry.begin() != entry.end());

    if (entry.begin() != entry.end()) { // Don't spam me with irrelevant test failures
        EXPECT_FALSE(entry.flags().soa);
        EXPECT_FALSE(entry.flags().ns);
        EXPECT_TRUE(entry.flags().a);
        EXPECT_FALSE(entry.flags().aaaa);
        EXPECT_FALSE(entry.flags().txt);
        EXPECT_FALSE(entry.flags().cname);

        EXPECT_EQ(entry.index().size(), 1);
        RrA a{sb.buffer(), entry.begin()->offset()};
        EXPECT_EQ(a.labels().string(), fqdn);
        EXPECT_EQ(a.address().to_string(), ipv4);
        EXPECT_EQ(a.string(), ipv4);
        EXPECT_EQ(a.ttl(), 1000);
    }
}

TEST(ApiBuild, aaaaOk) {
    const string_view fqdn = "example.com";
    const string_view ipv6 = "2001:db8:85a3::8a2e:370:7334";

    boost::json::object json;
    json["a"] = boost::json::array{ipv6};

    StorageBuilder sb;
    EXPECT_NO_THROW(RestApi::build(fqdn, 1000, sb, json));

    Entry entry{sb.buffer()};
    EXPECT_TRUE(entry.begin() != entry.end());

    if (entry.begin() != entry.end()) { // Don't spam me with irrelevant test failures
        EXPECT_FALSE(entry.flags().soa);
        EXPECT_FALSE(entry.flags().ns);
        EXPECT_FALSE(entry.flags().a);
        EXPECT_TRUE(entry.flags().aaaa);
        EXPECT_FALSE(entry.flags().txt);
        EXPECT_FALSE(entry.flags().cname);

        EXPECT_EQ(entry.index().size(), 1);
        RrA a{sb.buffer(), entry.begin()->offset()};
        EXPECT_EQ(a.labels().string(), fqdn);
        EXPECT_EQ(a.address().to_string(), ipv6);
        EXPECT_EQ(a.string(), ipv6);
        EXPECT_EQ(a.ttl(), 1000);
    }
}

TEST(ApiBuild, aAndAaaaOk) {
    const string_view fqdn = "example.com";
    const string_view ipv4 = "127.0.0.1";
    const string_view ipv6 = "2001:db8:85a3::8a2e:370:7334";

    boost::json::object json;
    json["a"] = {ipv4, ipv6};

    StorageBuilder sb;
    EXPECT_NO_THROW(RestApi::build(fqdn, 1000, sb, json));

    Entry entry{sb.buffer()};
    EXPECT_TRUE(entry.begin() != entry.end());

    if (entry.begin() != entry.end()) { // Don't spam me with irrelevant test failures
        EXPECT_FALSE(entry.flags().soa);
        EXPECT_FALSE(entry.flags().ns);
        EXPECT_TRUE(entry.flags().a);
        EXPECT_TRUE(entry.flags().aaaa);
        EXPECT_FALSE(entry.flags().txt);
        EXPECT_FALSE(entry.flags().cname);

        EXPECT_EQ(entry.index().size(), 2);

        auto it = entry.begin();
        {
            RrA a{sb.buffer(), it->offset()};
            EXPECT_EQ(a.labels().string(), fqdn);
            EXPECT_EQ(a.address().to_string(), ipv4);
            EXPECT_EQ(a.string(), ipv4);
            EXPECT_EQ(a.ttl(), 1000);
        }

        ++it;
        {
            RrA a{sb.buffer(), it->offset()};
            EXPECT_EQ(a.labels().string(), fqdn);
            EXPECT_EQ(a.address().to_string(), ipv6);
            EXPECT_EQ(a.string(), ipv6);
            EXPECT_EQ(a.ttl(), 1000);
        }
    }
}

TEST(ApiBuild, txtOk) {
    const string_view fqdn = "example.com";
    const string_view text = "Dogs are man's best friends";


    boost::json::object json;
    json["txt"] = text;

    StorageBuilder sb;
    EXPECT_NO_THROW(RestApi::build(fqdn, 1000, sb, json));

    Entry entry{sb.buffer()};
    EXPECT_TRUE(entry.begin() != entry.end());

    RrTxt txt{sb.buffer(), entry.begin()->offset()};
    EXPECT_EQ(txt.ttl(), 1000);
    EXPECT_EQ(txt.string(), text);
}

TEST(ApiBuild, cnameOk) {
    const string_view fqdn = "example.com";
    const string_view refer = "a.b.c.d.example.com";


    boost::json::object json;
    json["cname"] = refer;

    StorageBuilder sb;
    EXPECT_NO_THROW(RestApi::build(fqdn, 1000, sb, json));

    Entry entry{sb.buffer()};
    EXPECT_TRUE(entry.begin() != entry.end());

    RrCname cname{sb.buffer(), entry.begin()->offset()};
    EXPECT_EQ(cname.ttl(), 1000);
    EXPECT_EQ(cname.cname().string(), refer);
}

TEST(ApiBuild, mxOk) {
    const string_view fqdn = "example.com";
    const string_view host = "mail.example.com";

    boost::json::object json;
    auto mx_data = boost::json::object{};
    mx_data["priority"] = 10;
    mx_data["host"] = host;
    json["mx"] = boost::json::array{mx_data};


    StorageBuilder sb;
    EXPECT_NO_THROW(RestApi::build(fqdn, 1000, sb, json));

    Entry entry{sb.buffer()};
    EXPECT_TRUE(entry.begin() != entry.end());

    RrMx mx{sb.buffer(), entry.begin()->offset()};
    EXPECT_EQ(mx.ttl(), 1000);
    EXPECT_EQ(mx.host().string(), host);
    EXPECT_EQ(mx.priority(), 10);
}

TEST(ApiBuild, unknownEntity) {
    const string_view fqdn = "example.com";

    boost::json::object json;
    json["dog"] = boost::json::object{};

    StorageBuilder sb;
    EXPECT_THROW(RestApi::build(fqdn, 1000, sb, json), yahat::Response);

    try {
        RestApi::build(fqdn, 1000, sb, json);
    } catch(const yahat::Response& ex) {
        EXPECT_EQ(ex.code, 400);
        EXPECT_EQ(ex.reason, "Unknown entity: dog");
    }
}


TEST(ApiRequest, onZoneOk) {

    TmpDb db;
    {
        auto json = getZoneJson();
        auto req = makeRequest("zone", "example.com", boost::json::serialize(json), yahat::Request::Type::POST);

        RestApi api{db.config(), db.resource()};

        auto parsed = api.parse(req);

        auto res = api.onZone(req, parsed);

        EXPECT_EQ(res.code, 200);
    }
}

TEST(ApiRequest, onZoneExists) {

    TmpDb db;
    {
        auto json = getZoneJson();

        auto req = makeRequest("zone", "example.com", boost::json::serialize(json), yahat::Request::Type::POST);

        RestApi api{db.config(), *db};

        auto parsed = api.parse(req);

        auto res = api.onZone(req, parsed);
        EXPECT_EQ(res.code, 200);

        res = api.onZone(req, parsed);
        EXPECT_EQ(res.code, 409);
        EXPECT_EQ(res.reason, "The zone already exists");
    }
}

TEST(ApiRequest, postRrWithSoa) {
    const string_view fqdn{"example.com"};

    TmpDb db;
    {
        auto json = boost::json::serialize(getZoneJson());
        auto req = makeRequest("rr", fqdn, json, yahat::Request::Type::POST);

        RestApi api{db.config(), db.resource()};
        auto parsed = api.parse(req);
        auto res = api.onResourceRecord(req, parsed);

        EXPECT_EQ(res.code, 404);
    }
}

TEST(ApiRequest, postRrOverwriteZone) {
    const string_view fqdn{"example.com"};

    TmpDb db;
    {
        db.createTestZone();

        auto json = boost::json::serialize(getZoneJson());
        auto req = makeRequest("rr", fqdn, json, yahat::Request::Type::POST);

        RestApi api{db.config(), db.resource()};
        auto parsed = api.parse(req);
        auto res = api.onResourceRecord(req, parsed);

        EXPECT_EQ(res.code, 409);
    }
}

TEST(ApiRequest, postSubRr) {
    const string_view fqdn{"www.example.com"};
    const string_view soa_fqdn{"example.com"};

    TmpDb db;
    {
        db.createTestZone();

        EXPECT_EQ(getSoaSerial(fqdn, *db), DEFAULT_SOA_SERIAL);

        auto json = getAJson();
        auto req = makeRequest("rr", fqdn, json, yahat::Request::Type::POST);

        RestApi api{db.config(), db.resource()};
        auto parsed = api.parse(req);
        auto res = api.onResourceRecord(req, parsed);

        EXPECT_EQ(res.code, 201);
        EXPECT_EQ(getSoaSerial(fqdn, *db), DEFAULT_SOA_SERIAL + 1);
        EXPECT_EQ(getSoaSerial(soa_fqdn, *db), DEFAULT_SOA_SERIAL + 1);
    }
}

TEST(ApiRequest, postSubRrZeroTtl) {
    const string_view fqdn{"zero.example.com"};

    TmpDb db;
    {
        db.createTestZone();

        auto json = getAJsonZeroTtl();
        auto req = makeRequest("rr", fqdn, json, yahat::Request::Type::POST);

        RestApi api{db.config(), db.resource()};
        auto parsed = api.parse(req);
        auto res = api.onResourceRecord(req, parsed);

        EXPECT_EQ(res.code, 201);

        auto entry = lookup(fqdn, *db);
        EXPECT_EQ(entry.count(), 2);

        for(auto rr : entry) {
            EXPECT_EQ(rr.ttl(), 0);
            EXPECT_EQ(rr.type(), TYPE_A);
        }
    }
}

TEST(ApiRequest, postSubRrExists) {
    const string_view fqdn{"www.example.com"};

    TmpDb db;
    {
        db.createTestZone();
        auto json = getAJson();
        auto req = makeRequest("rr", fqdn, json, yahat::Request::Type::POST);

        RestApi api{db.config(), db.resource()};
        auto parsed = api.parse(req);
        auto res = api.onResourceRecord(req, parsed);
        EXPECT_EQ(res.code, 201);

        res = api.onResourceRecord(req, parsed);
        EXPECT_EQ(res.code, 409);
    }
}

TEST(ApiRequest, postSubRrNoZone) {
    const string_view fqdn{"www.otherexample.com"};

    TmpDb db;
    {
        db.createTestZone();
        auto json = getAJson();
        auto req = makeRequest("rr", fqdn, json, yahat::Request::Type::POST);

        RestApi api{db.config(), db.resource()};
        auto parsed = api.parse(req);
        auto res = api.onResourceRecord(req, parsed);
        EXPECT_EQ(res.code, 404);
    }
}


TEST(ApiRequest, putSubRr) {
    const string_view fqdn{"www.example.com"};
    const string_view soa_fqdn{"example.com"};

    TmpDb db;
    {
        db.createTestZone();

        EXPECT_EQ(getSoaSerial(fqdn, *db), DEFAULT_SOA_SERIAL);

        auto json = getAJson();
        auto req = makeRequest("rr", fqdn, json, yahat::Request::Type::PUT);

        RestApi api{db.config(), db.resource()};
        auto parsed = api.parse(req);
        auto res = api.onResourceRecord(req, parsed);

        EXPECT_EQ(res.code, 201);
        EXPECT_EQ(getSoaSerial(fqdn, *db), DEFAULT_SOA_SERIAL + 1);
        EXPECT_EQ(getSoaSerial(soa_fqdn, *db), DEFAULT_SOA_SERIAL + 1);
    }
}

TEST(ApiRequest, putSubRrNoZone) {
    const string_view fqdn{"www.otherexample.com"};

    TmpDb db;
    {
        db.createTestZone();
        auto json = getAJson();
        auto req = makeRequest("rr", fqdn, json, yahat::Request::Type::PUT);

        RestApi api{db.config(), db.resource()};
        auto parsed = api.parse(req);
        auto res = api.onResourceRecord(req, parsed);
        EXPECT_EQ(res.code, 404);
    }
}

TEST(ApiRequest, putSubRrExists) {
    const string_view fqdn{"www.example.com"};

    TmpDb db;
    {
        db.createTestZone();
        auto json = getAJson();
        auto req = makeRequest("rr", fqdn, json, yahat::Request::Type::PUT);

        RestApi api{db.config(), db.resource()};
        auto parsed = api.parse(req);
        auto res = api.onResourceRecord(req, parsed);
        EXPECT_EQ(res.code, 201);

        res = api.onResourceRecord(req, parsed);
        EXPECT_EQ(res.code, 200);
    }
}


TEST(ApiRequest, patchSubRr) {
    const string_view fqdn{"www.example.com"};
    const string_view soa_fqdn{"example.com"};

    TmpDb db;
    {
        db.createTestZone();

        EXPECT_EQ(getSoaSerial(fqdn, *db), DEFAULT_SOA_SERIAL);

        auto json = getAJson();
        auto req = makeRequest("rr", fqdn, json, yahat::Request::Type::PATCH);

        RestApi api{db.config(), db.resource()};
        auto parsed = api.parse(req);
        auto res = api.onResourceRecord(req, parsed);

        EXPECT_EQ(res.code, 201);
        EXPECT_EQ(getSoaSerial(fqdn, *db), DEFAULT_SOA_SERIAL + 1);
        EXPECT_EQ(getSoaSerial(soa_fqdn, *db), DEFAULT_SOA_SERIAL + 1);
    }
}

TEST(ApiRequest, patchSubRrNoZone) {
    const string_view fqdn{"www.otherexample.com"};

    TmpDb db;
    {
        db.createTestZone();
        auto json = getAJson();
        auto req = makeRequest("rr", fqdn, json, yahat::Request::Type::PATCH);

        RestApi api{db.config(), db.resource()};
        auto parsed = api.parse(req);
        auto res = api.onResourceRecord(req, parsed);
        EXPECT_EQ(res.code, 404);
    }
}

TEST(ApiRequest, postRrHinfo) {
    const string_view fqdn{"foo.example.com"};

    TmpDb db;
    {
        db.createTestZone();
        auto json = getHinfoJson();
        auto req = makeRequest("rr", fqdn, json, yahat::Request::Type::POST);

        RestApi api{db.config(), db.resource()};
        auto parsed = api.parse(req);
        auto res = api.onResourceRecord(req, parsed);
        EXPECT_EQ(res.code, 201);
    }
}

TEST(ApiRequest, postRrRp) {
    const string_view fqdn{"foo.example.com"};

    TmpDb db;
    {
        db.createTestZone();
        auto json = getRpJson();
        auto req = makeRequest("rr", fqdn, json, yahat::Request::Type::POST);

        RestApi api{db.config(), db.resource()};
        auto parsed = api.parse(req);
        auto res = api.onResourceRecord(req, parsed);
        EXPECT_EQ(res.code, 201);
    }
}

TEST(ApiRequest, postRrAfsdb) {
    const string_view fqdn{"foo.example.com"};

    TmpDb db;
    {
        db.createTestZone();
        auto json = getAfsdbJson();
        auto req = makeRequest("rr", fqdn, json, yahat::Request::Type::POST);

        RestApi api{db.config(), db.resource()};
        auto parsed = api.parse(req);
        auto res = api.onResourceRecord(req, parsed);
        EXPECT_EQ(res.code, 201);
    }
}

TEST(ApiRequest, postRrSrv) {
    const string_view fqdn{"_test._tcp.example.com"};

    TmpDb db;
    {
        db.createTestZone();
        auto json = getSrvJson();
        auto req = makeRequest("rr", fqdn, json, yahat::Request::Type::POST);

        RestApi api{db.config(), db.resource()};
        auto parsed = api.parse(req);
        auto res = api.onResourceRecord(req, parsed);
        EXPECT_EQ(res.code, 201);
    }
}

TEST(ApiRequest, postRrSrvNoAddressTarget) {
    const string_view fqdn{"_test._tcp.example.com"};

    TmpDb db;
    {
        db.createTestZone();
        db.createFooWithHinfo();
        auto json = getSrvNoAddressTargetJson();
        auto req = makeRequest("rr", fqdn, json, yahat::Request::Type::POST);

        RestApi api{db.config(), db.resource()};
        auto parsed = api.parse(req);
        EXPECT_THROW(api.onResourceRecord(req, parsed), yahat::Response);
    }
}

TEST(ApiRequest, postRrDhcid) {
    const string_view fqdn{"foo.example.com"};

    TmpDb db;
    {
        db.createTestZone();
        auto json = getSrvDhcidJson();
        auto req = makeRequest("rr", fqdn, json, yahat::Request::Type::POST);

        RestApi api{db.config(), db.resource()};
        auto parsed = api.parse(req);
        auto res = api.onResourceRecord(req, parsed);
        EXPECT_EQ(res.code, 201);
    }
}

TEST(ApiRequest, postRrOpenpgpkey) {
    const string_view fqdn{"4ecce23dd685d0c16e29e5959352._openpgpkey.example.com"};

    TmpDb db;
    {
    db.createTestZone();
        // the rdata is not a valid pgp key! It's just to test the API interface.
        const string json = R"({
        "openpgpkey":"AAIBY2/AuCccgoJbsaxcQc9TUapptP69lOjxfNuVAA2kjEA="
        })";
        auto req = makeRequest("rr", fqdn, json, yahat::Request::Type::POST);

        RestApi api{db.config(), db.resource()};
        auto parsed = api.parse(req);
        auto res = api.onResourceRecord(req, parsed);
        EXPECT_EQ(res.code, 201);
        auto trx = db.resource().transaction();
        auto e = trx->lookup(fqdn);
        EXPECT_FALSE(e.empty());
        EXPECT_EQ(e.count(), 1);
        EXPECT_EQ(e.begin()->type(), TYPE_OPENPGPKEY);
        EXPECT_EQ(e.begin()->rdataAsBase64(), "AAIBY2/AuCccgoJbsaxcQc9TUapptP69lOjxfNuVAA2kjEA=");
    }
}

TEST(ApiRequest, postRr) {
    const string_view fqdn{"foo.example.com"};

    TmpDb db;
    {
        db.createTestZone();
        auto json = R"({
            "rr": [{
                "type": 49,
                "rdata": "AAIBY2/AuCccgoJbsaxcQc9TUapptP69lOjxfNuVAA2kjEA="
            }, {
                "type": 1,
                "rdata": "fwAAAQ=="
            }]
        })";

        auto req = makeRequest("rr", fqdn, json, yahat::Request::Type::POST);

        RestApi api{db.config(), db.resource()};
        auto parsed = api.parse(req);
        auto res = api.onResourceRecord(req, parsed);
        EXPECT_EQ(res.code, 201);
        auto trx = db.resource().transaction();
        auto e = trx->lookup(fqdn);
        EXPECT_FALSE(e.empty());
        EXPECT_EQ(e.count(), 2);
        EXPECT_EQ(e.begin()->type(), TYPE_A);
        EXPECT_EQ(e.begin()->rdata().size(), 4);
        RrA a{e.buffer(), e.begin()->offset()};
        EXPECT_EQ(a.string(), "127.0.0.1");
        EXPECT_EQ(a.rdataAsBase64(), "fwAAAQ==");
    }
}

TEST(ApiRequest, deleteRr) {
    const string_view fqdn{"www.example.com"};
    const string_view soa_fqdn{"example.com"};

    TmpDb db;
    {
        db.createTestZone();

        EXPECT_EQ(getSoaSerial(fqdn, *db), DEFAULT_SOA_SERIAL);

        auto json = getAJson();
        auto req = makeRequest("rr", fqdn, json, yahat::Request::Type::POST);

        RestApi api{db.config(), db.resource()};
        auto parsed = api.parse(req);
        auto res = api.onResourceRecord(req, parsed);

        EXPECT_EQ(res.code, 201);
        EXPECT_EQ(getSoaSerial(fqdn, *db), DEFAULT_SOA_SERIAL + 1);

        req = makeRequest("rr", fqdn, {}, yahat::Request::Type::DELETE);
        parsed = api.parse(req);
        res = api.onResourceRecord(req, parsed);
        EXPECT_EQ(res.code, 200);
        EXPECT_EQ(getSoaSerial(fqdn, *db), DEFAULT_SOA_SERIAL + 2);
    }
}

TEST(ApiRequest, deleteZoneViaRrError) {
    const string_view fqdn{"www.example.com"};
    const string_view soa_fqdn{"example.com"};

    TmpDb db;
    {
        db.createTestZone();

        EXPECT_EQ(getSoaSerial(fqdn, *db), DEFAULT_SOA_SERIAL);

        auto json = getAJson();
        auto req = makeRequest("rr", fqdn, json, yahat::Request::Type::POST);

        RestApi api{db.config(), db.resource()};
        auto parsed = api.parse(req);
        auto res = api.onResourceRecord(req, parsed);

        EXPECT_EQ(res.code, 201);
        EXPECT_EQ(getSoaSerial(fqdn, *db), DEFAULT_SOA_SERIAL + 1);

        req = makeRequest("rr", soa_fqdn, {}, yahat::Request::Type::DELETE);
        parsed = api.parse(req);
        res = api.onResourceRecord(req, parsed);
        EXPECT_EQ(res.code, 409);
        EXPECT_EQ(getSoaSerial(fqdn, *db), DEFAULT_SOA_SERIAL + 1);
    }
}

TEST(ApiRequest, deleteZone) {
    const string_view fqdn{"www.example.com"};
    const string_view soa_fqdn{"example.com"};

    TmpDb db;
    {
        db.createTestZone();
        db.createTestZone("nsblast.com");
        db.createTestZone("awesomeexample.com");

        EXPECT_EQ(getSoaSerial(fqdn, *db), DEFAULT_SOA_SERIAL);

        RestApi api{db.config(), db.resource()};
        // Set up the test
        {
            auto json = getAJson();
            auto req = makeRequest("rr", fqdn, json, yahat::Request::Type::POST);

            auto parsed = api.parse(req);
            auto res = api.onResourceRecord(req, parsed);

            EXPECT_EQ(res.code, 201);
            EXPECT_EQ(getSoaSerial(fqdn, *db), DEFAULT_SOA_SERIAL + 1);
        }

        // Delete the zone
        {
            auto req = makeRequest("zone", soa_fqdn, {}, yahat::Request::Type::DELETE);
            auto parsed = api.parse(req);
            auto res = api.onZone(req, parsed);
            EXPECT_EQ(res.code, 200);
            EXPECT_EQ(getSoaSerial(fqdn, *db), 0);
            EXPECT_TRUE(lookup(soa_fqdn, *db).empty());
            EXPECT_FALSE(lookup("nsblast.com", *db).empty());
            EXPECT_FALSE(lookup("awesomeexample.com", *db).empty());
        }
    }
}

TEST(ApiRequest, diffCreatedForPostNewChild) {
    const string_view fqdn{"www.example.com"};
    const string_view soa_fqdn{"example.com"};

    TmpDb db;
    {
        db.config().dns_enable_ixfr = true;
        db.createTestZone();

        EXPECT_EQ(getSoaSerial(fqdn, *db), DEFAULT_SOA_SERIAL);

        auto json = getAJson();
        auto req = makeRequest("rr", fqdn, json, yahat::Request::Type::POST);

        RestApi api{db.config(), db.resource()};
        auto parsed = api.parse(req);
        auto res = api.onResourceRecord(req, parsed);

        EXPECT_EQ(res.code, 201);
        EXPECT_EQ(getSoaSerial(fqdn, *db), DEFAULT_SOA_SERIAL + 1);
        EXPECT_EQ(getSoaSerial(soa_fqdn, *db), DEFAULT_SOA_SERIAL + 1);

        auto trx = db->transaction();

        ResourceIf::RealKey key{soa_fqdn, DEFAULT_SOA_SERIAL + 1, ResourceIf::RealKey::Class::DIFF};
        auto data = trx->read(key, ResourceIf::Category::DIFF);
        Entry entry{data->data()};
        EXPECT_EQ(entry.count(), 4);
        auto it = entry.begin();

        // Start of deleted sequence, old soa
        EXPECT_NE(it, entry.end());
        EXPECT_EQ(it->type(), TYPE_SOA);
        if (it->type() == TYPE_SOA)
        {
            RrSoa soa(entry.buffer(), it->offset());
            EXPECT_EQ(soa.serial(), DEFAULT_SOA_SERIAL);
        }

        ++it;
        // Start of new entries sequence, new soa
        EXPECT_NE(it, entry.end());
        EXPECT_EQ(it->type(), TYPE_SOA);
        if (it->type() == TYPE_SOA)
        {
            RrSoa soa(entry.buffer(), it->offset());
            EXPECT_EQ(soa.serial(), DEFAULT_SOA_SERIAL + 1);
        }

        ++it;
        // A record
        EXPECT_NE(it, entry.end());
        EXPECT_EQ(it->type(), TYPE_A);

        ++it;
        // A record
        EXPECT_NE(it, entry.end());
        EXPECT_EQ(it->type(), TYPE_A);

        ++it;
        // End of this difference sequence
        EXPECT_EQ(it, entry.end());
    }
}

TEST(ApiRequest, diffCreatedForDeleteChild) {
    const string_view fqdn{"www.example.com"};
    const string_view soa_fqdn{"example.com"};

    TmpDb db;
    {
        db.config().dns_enable_ixfr = true;
        db.createTestZone();
        db.createWwwA();

        EXPECT_EQ(getSoaSerial(fqdn, *db), DEFAULT_SOA_SERIAL);

        auto req = makeRequest("rr", fqdn, {}, yahat::Request::Type::DELETE);

        RestApi api{db.config(), db.resource()};
        auto parsed = api.parse(req);
        auto res = api.onResourceRecord(req, parsed);

        EXPECT_EQ(res.code, 200);
        EXPECT_EQ(getSoaSerial(soa_fqdn, *db), DEFAULT_SOA_SERIAL + 1);
        auto trx = db->transaction();

        ResourceIf::RealKey key{soa_fqdn, DEFAULT_SOA_SERIAL + 1, ResourceIf::RealKey::Class::DIFF};
        auto data = trx->read(key, ResourceIf::Category::DIFF);
        Entry entry{data->data()};
        EXPECT_EQ(entry.count(), 6);
        auto it = entry.begin();

        // Start of deleted sequence, old soa
        EXPECT_NE(it, entry.end());
        EXPECT_EQ(it->type(), TYPE_SOA);
        if (it->type() == TYPE_SOA)
        {
            RrSoa soa(entry.buffer(), it->offset());
            EXPECT_EQ(soa.serial(), DEFAULT_SOA_SERIAL);
        }

        ++it;
        // A record
        EXPECT_NE(it, entry.end());
        EXPECT_EQ(it->type(), TYPE_A);

        ++it;
        // A record
        EXPECT_NE(it, entry.end());
        EXPECT_EQ(it->type(), TYPE_A);

        ++it;
        // A record
        EXPECT_NE(it, entry.end());
        EXPECT_EQ(it->type(), TYPE_AAAA);

        ++it;
        // A record
        EXPECT_NE(it, entry.end());
        EXPECT_EQ(it->type(), TYPE_AAAA);

        ++it;
        // Start of new entries sequence, new soa
        EXPECT_NE(it, entry.end());
        EXPECT_EQ(it->type(), TYPE_SOA);
        if (it->type() == TYPE_SOA)
        {
            RrSoa soa(entry.buffer(), it->offset());
            EXPECT_EQ(soa.serial(), DEFAULT_SOA_SERIAL + 1);
        }

        ++it;
        // End of this difference sequence
        EXPECT_EQ(it, entry.end());
    }
}


int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);

    logfault::LogManager::Instance().AddHandler(
                make_unique<logfault::StreamHandler>(clog, logfault::LogLevel::TRACE));
    return RUN_ALL_TESTS();
}
