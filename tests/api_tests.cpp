
#include <format>

#include "gtest/gtest.h"
#include "RestApi.h"

#include "TmpDb.h"

#include "nsblast/DnsMessages.h"
#include "nsblast/logging.h"
#include "yahat/HttpServer.h"

//#include "test_res.h"

using namespace std;
using namespace nsblast;
using namespace nsblast::lib;

namespace {

static constexpr auto DEFAULT_SOA_SERIAL = 1000;
static constexpr string_view user_passwd = "very$ecure123";
static constexpr string_view user2_passwd = "even more very$ecure123";

auto getZoneJson(string_view zone = "example.com") {

    // Note rname: we expect the mapping from email to domain-name to be present in `build()`
    auto soa = boost::json::parse(format(R"({{
    "ttl": 1000,
    "soa": {{
    "refresh": 1001,
    "retry": 1002,
    "expire": 1003,
    "serial": 1000,
    "minimum": 1004,
    "mname": "ns1.{}",
    "rname": "hostmaster@{}"
    }},
    "ns": [
    "ns1.{}",
    "ns2.{}"
    ]
    }})", zone, zone, zone, zone));
    return soa;
}

string getJsonForNewRole(string_view name = {}) {

    ostringstream out;

    out << "{";

    if (!name.empty()) {
        out << R"("name":")" << name << R"(",)";
    }

    out << R"(
 "permissions": [
    "USE_API",
    "CREATE_ZONE"
  ],
  "filter": {
    "fqdn": "test",
    "recursive": true,
    "regex": ".*"
  }})";

    return out.str();
}

string getJsonForNewUser(string_view name = {}) {

    ostringstream out;

    out << "{";

    if (!name.empty()) {
        out << R"("name":")" << name << R"(",)";
    }

    out << R"(
 "roles": [
    "Administrator"
  ],
  "auth": {
    "password": ")" << user_passwd << R"("
  }})";

    return out.str();
}

string getJsonForNewTenant(string_view id = {}) {

    ostringstream out;

    out << "{";

    if (!id.empty()) {
        out << R"("id":")" << id << R"(",)";
    }

    out << R"(
  "root": "example.com",
  "properties": [
    {
      "key": "is-test",
      "value": "true"
    }
  ],
  "allowedPermissions": [
    "USE_API"
  ],
  "roles": [
    {
      "name": "test",
      "properties": [
        {
          "key": "created",
          "value": "yes"
        },
        {
          "key": "kind",
          "value": "dog"
        }
      ],
      "permissions": [
        "USE_API",
        "CREATE_ZONE"
      ],
      "filter": {
        "fqdn": "",
        "recursive": true,
        "regex": ".*test.*"
      }
    },
    {
      "name": "guest",
      "permissions": [
        "USE_API"
      ],
      "filter": {
        "fqdn": "guest",
        "recursive": false
      }
    }
  ],
  "users": [
    {
      "name": "admin@example.com",
      "active": true,
      "properties": [
        {
          "key": "kind",
          "value": "Cat"
        }
      ],
      "roles": [
        "test",
        "guest"
      ],
      "auth": {
        "password": ")" << user_passwd << R"("
      }
    },
    {
      "name": "guest@example.com",
      "active": true,
      "properties": [
        {
          "key": "kind",
          "value": "Camel"
        }
      ],
      "roles": [
        "guest"
      ],
      "auth": {
        "password": ")" << user2_passwd << R"("
      }
    }
  ]
})";

    return out.str();
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

auto makeRequest(MockServer& ms, const string& what, string_view fqdn, string json, yahat::Request::Type type) {
    static const string base = "/api/v1";

    LOG_DEBUG << "makeRequest fqdn=" << fqdn << ", what=" << what
              << ", json=" << json;

    std::string full_target = base + "/" + what + "/" + string{fqdn};

    yahat::Request req{full_target, json, type, {}};
    req.route = base;
    req.auth = ms.getAdminSession()->getAuth();
    return req;
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

TEST(ApiBuild, txtArrayOk) {
    const string_view fqdn = "example.com";
    const string_view text1 = "Dogs are man's best friends";
    const string_view text2 = "Dogs love cats. They taste so good!";


    boost::json::object json;
    boost::json::array txts = {text1, text2};
    json["txt"] = txts;

    StorageBuilder sb;
    EXPECT_NO_THROW(RestApi::build(fqdn, 1000, sb, json));

    Entry entry{sb.buffer()};
    EXPECT_TRUE(entry.begin() != entry.end());

    auto it = entry.begin();
    {
        RrTxt txt{sb.buffer(), it->offset()};
        EXPECT_EQ(txt.ttl(), 1000);
        EXPECT_EQ(txt.string(), text1);
    }
    ++it;
    {
        RrTxt txt{sb.buffer(), it->offset()};
        EXPECT_EQ(txt.ttl(), 1000);
        EXPECT_EQ(txt.string(), text2);
    }
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

    MockServer svr;
    {
        auto json = getZoneJson();
        auto req = makeRequest(svr, "zone", "example.com", boost::json::serialize(json), yahat::Request::Type::POST);

        RestApi api{svr};

        auto parsed = api.parse(req);

        auto res = api.onZone(req, parsed);

        EXPECT_EQ(res.code, 201);
    }
}

TEST(ApiRequest, onZoneExists) {

    MockServer svr;
    {
        auto json = getZoneJson();

        auto req = makeRequest(svr, "zone", "example.com", boost::json::serialize(json), yahat::Request::Type::POST);

        RestApi api{svr};

        auto parsed = api.parse(req);

        auto res = api.onZone(req, parsed);
        EXPECT_EQ(res.code, 201);

        res = api.onZone(req, parsed);
        EXPECT_EQ(res.code, 409);
        EXPECT_EQ(res.reason, "The zone already exists");
    }
}

TEST(ApiRequest, postRrWithSoa) {
    const string_view fqdn{"example.com"};

    MockServer svr;
    {
        auto json = boost::json::serialize(getZoneJson());
        auto req = makeRequest(svr, "rr", fqdn, json, yahat::Request::Type::POST);

        RestApi api{svr};
        auto parsed = api.parse(req);
        auto res = api.onResourceRecord(req, parsed);

        EXPECT_EQ(res.code, 404);
    }
}

TEST(ApiRequest, postRrOverwriteZone) {
    const string_view fqdn{"example.com"};

    MockServer svr;
    {
        svr->createTestZone();

        auto json = boost::json::serialize(getZoneJson());
        auto req = makeRequest(svr, "rr", fqdn, json, yahat::Request::Type::POST);

        RestApi api{svr};
        auto parsed = api.parse(req);
        auto res = api.onResourceRecord(req, parsed);

        EXPECT_EQ(res.code, 400);
    }
}

TEST(ApiRequest, postSubRr) {
    const string_view fqdn{"www.example.com"};
    const string_view soa_fqdn{"example.com"};

    MockServer svr;
    {
        svr->createTestZone();

        EXPECT_EQ(getSoaSerial(fqdn, svr->resource()), DEFAULT_SOA_SERIAL);

        auto json = getAJson();
        auto req = makeRequest(svr, "rr", fqdn, json, yahat::Request::Type::POST);

        RestApi api{svr};
        auto parsed = api.parse(req);
        auto res = api.onResourceRecord(req, parsed);

        EXPECT_EQ(res.code, 201);
        EXPECT_EQ(getSoaSerial(fqdn, svr->resource()), DEFAULT_SOA_SERIAL + 1);
        EXPECT_EQ(getSoaSerial(soa_fqdn, svr->resource()), DEFAULT_SOA_SERIAL + 1);
    }
}

TEST(ApiRequest, postSubRrZeroTtl) {
    const string_view fqdn{"zero.example.com"};

    MockServer svr;
    {
        svr->createTestZone();

        auto json = getAJsonZeroTtl();
        auto req = makeRequest(svr, "rr", fqdn, json, yahat::Request::Type::POST);

        RestApi api{svr};
        auto parsed = api.parse(req);
        auto res = api.onResourceRecord(req, parsed);

        EXPECT_EQ(res.code, 201);

        auto entry = lookup(fqdn, svr->resource());
        EXPECT_EQ(entry.count(), 2);

        for(auto rr : entry) {
            EXPECT_EQ(rr.ttl(), 0);
            EXPECT_EQ(rr.type(), TYPE_A);
        }
    }
}

TEST(ApiRequest, postSubRrExists) {
    const string_view fqdn{"www.example.com"};

    MockServer svr;
    {
        svr->createTestZone();
        auto json = getAJson();
        auto req = makeRequest(svr, "rr", fqdn, json, yahat::Request::Type::POST);

        RestApi api{svr};
        auto parsed = api.parse(req);
        auto res = api.onResourceRecord(req, parsed);
        EXPECT_EQ(res.code, 201);

        res = api.onResourceRecord(req, parsed);
        EXPECT_EQ(res.code, 409);
    }
}

TEST(ApiRequest, postSubRrNoZone) {
    const string_view fqdn{"www.otherexample.com"};

    MockServer svr;
    {
        svr->createTestZone();
        auto json = getAJson();
        auto req = makeRequest(svr, "rr", fqdn, json, yahat::Request::Type::POST);

        RestApi api{svr};
        auto parsed = api.parse(req);
        auto res = api.onResourceRecord(req, parsed);
        EXPECT_EQ(res.code, 404);
    }
}


TEST(ApiRequest, putSubRr) {
    const string_view fqdn{"www.example.com"};
    const string_view soa_fqdn{"example.com"};

    MockServer svr;
    {
        svr->createTestZone();

        EXPECT_EQ(getSoaSerial(fqdn, svr->resource()), DEFAULT_SOA_SERIAL);

        auto json = getAJson();
        auto req = makeRequest(svr, "rr", fqdn, json, yahat::Request::Type::PUT);

        RestApi api{svr};
        auto parsed = api.parse(req);
        auto res = api.onResourceRecord(req, parsed);

        EXPECT_EQ(res.code, 201);
        EXPECT_EQ(getSoaSerial(fqdn, svr->resource()), DEFAULT_SOA_SERIAL + 1);
        EXPECT_EQ(getSoaSerial(soa_fqdn, svr->resource()), DEFAULT_SOA_SERIAL + 1);
    }
}

TEST(ApiRequest, putSubRrNoZone) {
    const string_view fqdn{"www.otherexample.com"};

    MockServer svr;
    {
        svr->createTestZone();
        auto json = getAJson();
        auto req = makeRequest(svr, "rr", fqdn, json, yahat::Request::Type::PUT);

        RestApi api{svr};
        auto parsed = api.parse(req);
        auto res = api.onResourceRecord(req, parsed);
        EXPECT_EQ(res.code, 404);
    }
}

TEST(ApiRequest, putSubRrExists) {
    const string_view fqdn{"www.example.com"};

    MockServer svr;
    {
        svr->createTestZone();
        auto json = getAJson();
        auto req = makeRequest(svr, "rr", fqdn, json, yahat::Request::Type::PUT);

        RestApi api{svr};
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

    MockServer svr;
    {
        svr->createTestZone();

        EXPECT_EQ(getSoaSerial(fqdn, svr->resource()), DEFAULT_SOA_SERIAL);

        auto json = getAJson();
        auto req = makeRequest(svr, "rr", fqdn, json, yahat::Request::Type::PATCH);

        RestApi api{svr};
        auto parsed = api.parse(req);
        auto res = api.onResourceRecord(req, parsed);

        EXPECT_EQ(res.code, 201);
        EXPECT_EQ(getSoaSerial(fqdn, svr->resource()), DEFAULT_SOA_SERIAL + 1);
        EXPECT_EQ(getSoaSerial(soa_fqdn, svr->resource()), DEFAULT_SOA_SERIAL + 1);
    }
}

TEST(ApiRequest, patchSubRrAddTxt) {
    const string_view fqdn{"www.example.com"};
    const string_view soa_fqdn{"example.com"};

    MockServer svr;
    {
        svr->createTestZone();
        svr->createWwwA();

        EXPECT_EQ(getSoaSerial(fqdn, svr->resource()), DEFAULT_SOA_SERIAL);

        boost::json::object o;
        o["txt"] = "one single line of text";
        auto json = boost::json::serialize(o);
        auto req = makeRequest(svr, "rr", fqdn, json, yahat::Request::Type::PATCH);

        RestApi api{svr};
        auto parsed = api.parse(req);
        auto res = api.onResourceRecord(req, parsed);

        EXPECT_EQ(res.code, 200);
        EXPECT_EQ(getSoaSerial(fqdn, svr->resource()), DEFAULT_SOA_SERIAL + 1);
        EXPECT_EQ(getSoaSerial(soa_fqdn, svr->resource()), DEFAULT_SOA_SERIAL + 1);

        req = makeRequest(svr, "rr", fqdn, {}, yahat::Request::Type::GET);
        parsed = api.parse(req);
        res = api.onResourceRecord(req, parsed);
        EXPECT_EQ(res.code, 200);

        LOG_DEBUG << "Result-json: " << res.body;

        EXPECT_EQ(res.body, R"({"rcode":200,"error":false,"message":"","value":{"fqdn":"www.example.com","ttl":1000,"a":["127.0.0.3","127.0.0.4"],"aaaa":["2003:db8:85a3::8a2e:370:7334","2004:db8:85a3::8a2e:370:7335"],"txt":["one single line of text"]}})");

        o["txt"] = "another single line of text";
        json = boost::json::serialize(o);
        req = makeRequest(svr, "rr", fqdn, json, yahat::Request::Type::PATCH);

        parsed = api.parse(req);
        res = api.onResourceRecord(req, parsed);
        EXPECT_EQ(res.code, 200);

        req = makeRequest(svr, "rr", fqdn, {}, yahat::Request::Type::GET);
        parsed = api.parse(req);
        res = api.onResourceRecord(req, parsed);
        EXPECT_EQ(res.code, 200);

        LOG_DEBUG << "Result-json: " << res.body;

        EXPECT_EQ(res.body, R"({"rcode":200,"error":false,"message":"","value":{"fqdn":"www.example.com","ttl":1000,"a":["127.0.0.3","127.0.0.4"],"aaaa":["2003:db8:85a3::8a2e:370:7334","2004:db8:85a3::8a2e:370:7335"],"txt":["another single line of text"]}})");

        boost::json::array a;
        a.emplace_back("first text");
        a.emplace_back("second text");
        a.emplace_back("third and final text");
        o["txt"] = a;

        json = boost::json::serialize(o);
        req = makeRequest(svr, "rr", fqdn, json, yahat::Request::Type::PATCH);

        parsed = api.parse(req);
        res = api.onResourceRecord(req, parsed);
        EXPECT_EQ(res.code, 200);

        req = makeRequest(svr, "rr", fqdn, {}, yahat::Request::Type::GET);
        parsed = api.parse(req);
        res = api.onResourceRecord(req, parsed);
        EXPECT_EQ(res.code, 200);

        LOG_DEBUG << "Result-json: " << res.body;

        EXPECT_EQ(res.body, R"({"rcode":200,"error":false,"message":"","value":{"fqdn":"www.example.com","ttl":1000,"a":["127.0.0.3","127.0.0.4"],"aaaa":["2003:db8:85a3::8a2e:370:7334","2004:db8:85a3::8a2e:370:7335"],"txt":["first text","second text","third and final text"]}})");
    }
}

TEST(ApiRequest, patchSubRrNoZone) {
    const string_view fqdn{"www.otherexample.com"};

    MockServer svr;
    {
        svr->createTestZone();
        auto json = getAJson();
        auto req = makeRequest(svr, "rr", fqdn, json, yahat::Request::Type::PATCH);

        RestApi api{svr};
        auto parsed = api.parse(req);
        auto res = api.onResourceRecord(req, parsed);
        EXPECT_EQ(res.code, 404);
    }
}

TEST(ApiRequest, postRrHinfo) {
    const string_view fqdn{"foo.example.com"};

    MockServer svr;
    {
        svr->createTestZone();
        auto json = getHinfoJson();
        auto req = makeRequest(svr, "rr", fqdn, json, yahat::Request::Type::POST);

        RestApi api{svr};
        auto parsed = api.parse(req);
        auto res = api.onResourceRecord(req, parsed);
        EXPECT_EQ(res.code, 201);
    }
}

TEST(ApiRequest, postRrRp) {
    const string_view fqdn{"foo.example.com"};

    MockServer svr;
    {
        svr->createTestZone();
        auto json = getRpJson();
        auto req = makeRequest(svr, "rr", fqdn, json, yahat::Request::Type::POST);

        RestApi api{svr};
        auto parsed = api.parse(req);
        auto res = api.onResourceRecord(req, parsed);
        EXPECT_EQ(res.code, 201);
    }
}

TEST(ApiRequest, postRrAfsdb) {
    const string_view fqdn{"foo.example.com"};

    MockServer svr;
    {
        svr->createTestZone();
        auto json = getAfsdbJson();
        auto req = makeRequest(svr, "rr", fqdn, json, yahat::Request::Type::POST);

        RestApi api{svr};
        auto parsed = api.parse(req);
        auto res = api.onResourceRecord(req, parsed);
        EXPECT_EQ(res.code, 201);
    }
}

TEST(ApiRequest, postRrSrv) {
    const string_view fqdn{"_test._tcp.example.com"};

    MockServer svr;
    {
        svr->createTestZone();
        auto json = getSrvJson();
        auto req = makeRequest(svr, "rr", fqdn, json, yahat::Request::Type::POST);

        RestApi api{svr};
        auto parsed = api.parse(req);
        auto res = api.onResourceRecord(req, parsed);
        EXPECT_EQ(res.code, 201);
    }
}

TEST(ApiRequest, postRrSrvNoAddressTarget) {
    const string_view fqdn{"_test._tcp.example.com"};

    MockServer svr;
    {
        svr->createTestZone();
        svr->createFooWithHinfo();
        auto json = getSrvNoAddressTargetJson();
        auto req = makeRequest(svr, "rr", fqdn, json, yahat::Request::Type::POST);

        RestApi api{svr};
        auto parsed = api.parse(req);
        EXPECT_THROW(api.onResourceRecord(req, parsed), yahat::Response);
    }
}

TEST(ApiRequest, postRrDhcid) {
    const string_view fqdn{"foo.example.com"};

    MockServer svr;
    {
        svr->createTestZone();
        auto json = getSrvDhcidJson();
        auto req = makeRequest(svr, "rr", fqdn, json, yahat::Request::Type::POST);

        RestApi api{svr};
        auto parsed = api.parse(req);
        auto res = api.onResourceRecord(req, parsed);
        EXPECT_EQ(res.code, 201);
    }
}

TEST(ApiRequest, postRrOpenpgpkey) {
    const string_view fqdn{"4ecce23dd685d0c16e29e5959352._openpgpkey.example.com"};

    MockServer svr;
    {
    svr->createTestZone();
        // the rdata is not a valid pgp key! It's just to test the API interface.
        const string json = R"({
        "openpgpkey":"AAIBY2/AuCccgoJbsaxcQc9TUapptP69lOjxfNuVAA2kjEA="
        })";
        auto req = makeRequest(svr, "rr", fqdn, json, yahat::Request::Type::POST);

        RestApi api{svr};
        auto parsed = api.parse(req);
        auto res = api.onResourceRecord(req, parsed);
        EXPECT_EQ(res.code, 201);
        auto trx = svr->resource().transaction();
        auto e = trx->lookup(fqdn);
        EXPECT_FALSE(e.empty());
        EXPECT_EQ(e.count(), 1);
        EXPECT_EQ(e.begin()->type(), TYPE_OPENPGPKEY);
        EXPECT_EQ(e.begin()->rdataAsBase64(), "AAIBY2/AuCccgoJbsaxcQc9TUapptP69lOjxfNuVAA2kjEA=");
    }
}

TEST(ApiRequest, postRr) {
    const string_view fqdn{"foo.example.com"};

    MockServer svr;
    {
        svr->createTestZone();
        auto json = R"({
            "rr": [{
                "type": 49,
                "rdata": "AAIBY2/AuCccgoJbsaxcQc9TUapptP69lOjxfNuVAA2kjEA="
            }, {
                "type": 1,
                "rdata": "fwAAAQ=="
            }]
        })";

        auto req = makeRequest(svr, "rr", fqdn, json, yahat::Request::Type::POST);

        RestApi api{svr};
        auto parsed = api.parse(req);
        auto res = api.onResourceRecord(req, parsed);
        EXPECT_EQ(res.code, 201);
        auto trx = svr->resource().transaction();
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

    MockServer svr;
    {
        svr->createTestZone();

        EXPECT_EQ(getSoaSerial(fqdn, svr->resource()), DEFAULT_SOA_SERIAL);

        auto json = getAJson();
        auto req = makeRequest(svr, "rr", fqdn, json, yahat::Request::Type::POST);

        RestApi api{svr};
        auto parsed = api.parse(req);
        auto res = api.onResourceRecord(req, parsed);

        EXPECT_EQ(res.code, 201);
        EXPECT_EQ(getSoaSerial(fqdn, svr->resource()), DEFAULT_SOA_SERIAL + 1);

        req = makeRequest(svr, "rr", fqdn, {}, yahat::Request::Type::DELETE);
        parsed = api.parse(req);
        res = api.onResourceRecord(req, parsed);
        EXPECT_EQ(res.code, 200);
        EXPECT_EQ(getSoaSerial(fqdn, svr->resource()), DEFAULT_SOA_SERIAL + 2);
    }
}

TEST(ApiRequest, deleteZoneViaRrError) {
    const string_view fqdn{"www.example.com"};
    const string_view soa_fqdn{"example.com"};

    MockServer svr;
    {
        svr->createTestZone();

        EXPECT_EQ(getSoaSerial(fqdn, svr->resource()), DEFAULT_SOA_SERIAL);

        auto json = getAJson();
        auto req = makeRequest(svr, "rr", fqdn, json, yahat::Request::Type::POST);

        RestApi api{svr};
        auto parsed = api.parse(req);
        auto res = api.onResourceRecord(req, parsed);

        EXPECT_EQ(res.code, 201);
        EXPECT_EQ(getSoaSerial(fqdn, svr->resource()), DEFAULT_SOA_SERIAL + 1);

        req = makeRequest(svr, "rr", soa_fqdn, {}, yahat::Request::Type::DELETE);
        parsed = api.parse(req);
        res = api.onResourceRecord(req, parsed);
        EXPECT_EQ(res.code, 400);
        EXPECT_EQ(getSoaSerial(fqdn, svr->resource()), DEFAULT_SOA_SERIAL + 1);
    }
}

TEST(ApiRequest, deleteZone) {
    const string_view fqdn{"www.example.com"};
    const string_view soa_fqdn{"example.com"};

    MockServer svr;
    {
        svr->createTestZone();
        svr->createTestZone("nsblast.com");
        svr->createTestZone("awesomeexample.com");

        EXPECT_EQ(getSoaSerial(fqdn, svr->resource()), DEFAULT_SOA_SERIAL);

        RestApi api{svr};
        // Set up the test
        {
            auto json = getAJson();
            auto req = makeRequest(svr, "rr", fqdn, json, yahat::Request::Type::POST);

            auto parsed = api.parse(req);
            auto res = api.onResourceRecord(req, parsed);

            EXPECT_EQ(res.code, 201);
            EXPECT_EQ(getSoaSerial(fqdn, svr->resource()), DEFAULT_SOA_SERIAL + 1);
        }

        // Delete the zone
        {
            auto req = makeRequest(svr, "zone", soa_fqdn, {}, yahat::Request::Type::DELETE);
            auto parsed = api.parse(req);
            auto res = api.onZone(req, parsed);
            EXPECT_EQ(res.code, 200);
            EXPECT_EQ(getSoaSerial(fqdn, svr->resource()), 0);
            EXPECT_TRUE(lookup(soa_fqdn, svr->resource()).empty());
            EXPECT_FALSE(lookup("nsblast.com", svr->resource()).empty());
            EXPECT_FALSE(lookup("awesomeexample.com", svr->resource()).empty());
        }
    }
}

TEST(ApiRequest, diffCreatedForPostNewChild) {
    const string_view fqdn{"www.example.com"};
    const string_view soa_fqdn{"example.com"};

    MockServer svr;
    {
        svr->config().dns_enable_ixfr = true;
        svr->createTestZone();

        EXPECT_EQ(getSoaSerial(fqdn, svr->resource()), DEFAULT_SOA_SERIAL);

        auto json = getAJson();
        auto req = makeRequest(svr, "rr", fqdn, json, yahat::Request::Type::POST);

        RestApi api{svr};
        auto parsed = api.parse(req);
        auto res = api.onResourceRecord(req, parsed);

        EXPECT_EQ(res.code, 201);
        EXPECT_EQ(getSoaSerial(fqdn, svr->resource()), DEFAULT_SOA_SERIAL + 1);
        EXPECT_EQ(getSoaSerial(soa_fqdn, svr->resource()), DEFAULT_SOA_SERIAL + 1);

        auto trx = svr->resource().transaction();

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

    MockServer svr;
    {
        svr->config().dns_enable_ixfr = true;
        svr->createTestZone();
        svr->createWwwA();

        EXPECT_EQ(getSoaSerial(fqdn, svr->resource()), DEFAULT_SOA_SERIAL);

        auto req = makeRequest(svr, "rr", fqdn, {}, yahat::Request::Type::DELETE);

        RestApi api{svr};
        auto parsed = api.parse(req);
        auto res = api.onResourceRecord(req, parsed);

        EXPECT_EQ(res.code, 200);
        EXPECT_EQ(getSoaSerial(soa_fqdn, svr->resource()), DEFAULT_SOA_SERIAL + 1);
        auto trx = svr->resource().transaction();

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

TEST(ApiRequest, createTenant) {
    MockServer svr;

    auto json = getJsonForNewTenant();
    auto req = makeRequest(svr, "tenant", "", json, yahat::Request::Type::POST);

    RestApi api{svr};

    auto parsed = api.parse(req);
    auto res = api.onTenant(req, parsed);
    EXPECT_EQ(res.code, 201);
}

TEST(ApiRequest, createExistingTenant) {
    MockServer svr;

    auto json = getJsonForNewTenant("40af80e8-6772-11ee-bd63-4f3d941e113e");
    auto req = makeRequest(svr, "tenant", "", json, yahat::Request::Type::POST);

    RestApi api{svr};

    auto parsed = api.parse(req);
    auto res = api.onTenant(req, parsed);
    EXPECT_EQ(res.code, 201);

    res = api.onTenant(req, parsed);
    EXPECT_EQ(res.code, 409);
}


TEST(ApiRequest, createTenantInvalidTarget) {
    MockServer svr;

    auto json = getJsonForNewTenant();
    auto req = makeRequest(svr, "tenant", "40af80e8-6772-11ee-bd63-4f3d941e113e", json, yahat::Request::Type::POST);

    RestApi api{svr};

    auto parsed = api.parse(req);
    auto res = api.onTenant(req, parsed);
    EXPECT_EQ(res.code, 400);
}


TEST(ApiRequest, putNewTenant) {
    MockServer svr;

    auto json = getJsonForNewTenant("40af80e8-6772-11ee-bd63-4f3d941e113e");
    auto req = makeRequest(svr, "tenant", "40af80e8-6772-11ee-bd63-4f3d941e113e", json, yahat::Request::Type::PUT);

    RestApi api{svr};

    auto parsed = api.parse(req);
    auto res = api.onTenant(req, parsed);
    EXPECT_EQ(res.code, 201);
}

TEST(ApiRequest, putTenant) {
    MockServer svr;

    auto json = getJsonForNewTenant("40af80e8-6772-11ee-bd63-4f3d941e113e");
    auto req = makeRequest(svr, "tenant", "", json, yahat::Request::Type::POST);

    RestApi api{svr};

    auto parsed = api.parse(req);
    auto res = api.onTenant(req, parsed);
    EXPECT_EQ(res.code, 201);

    req = makeRequest(svr, "tenant", "40af80e8-6772-11ee-bd63-4f3d941e113e", json, yahat::Request::Type::PUT);
    parsed = api.parse(req);
    res = api.onTenant(req, parsed);
    EXPECT_EQ(res.code, 200);
}

TEST(ApiRequest, patchTenant) {
    MockServer svr;

    auto json = getJsonForNewTenant("40af80e8-6772-11ee-bd63-4f3d941e113e");
    auto req = makeRequest(svr, "tenant", "", json, yahat::Request::Type::POST);

    RestApi api{svr};

    auto parsed = api.parse(req);
    auto res = api.onTenant(req, parsed);
    EXPECT_EQ(res.code, 201);

    req = makeRequest(svr, "tenant", "40af80e8-6772-11ee-bd63-4f3d941e113e", json, yahat::Request::Type::PATCH);
    parsed = api.parse(req);
    res = api.onTenant(req, parsed);
    EXPECT_EQ(res.code, 200);
}

TEST(ApiRequest, listTenants) {
    MockServer svr;
    auto req = makeRequest(svr, "tenant", "", {}, yahat::Request::Type::GET);
    RestApi api{svr};

    auto parsed = api.parse(req);
    auto res = api.onTenant(req, parsed);
    EXPECT_EQ(res.code, 200);
}

TEST(ApiRequest, getTenant) {
    MockServer svr;

    auto json = getJsonForNewTenant();
    auto req = makeRequest(svr, "tenant", "", json, yahat::Request::Type::POST);

    RestApi api{svr};

    auto parsed = api.parse(req);
    auto res = api.onTenant(req, parsed);
    EXPECT_EQ(res.code, 201);

    auto tobuf = boost::json::parse(res.body);
    auto to = tobuf.as_object().at("value").as_object();
    EXPECT_FALSE(to.at("id").as_string().empty());

    const auto tid = to.at("id").as_string();

    req = makeRequest(svr, "tenant/"s + string{tid}, "", json, yahat::Request::Type::GET);
    res = api.onTenant(req, parsed);
    EXPECT_EQ(res.code, 200);
    const auto gbuf = boost::json::parse(res.body);
    const auto& go = tobuf.as_object().at("value").as_object();
    EXPECT_TRUE(go.contains("id"));
    EXPECT_EQ(go.at("id").as_string(), to.at("id").as_string());

    const auto& users = go.at("users").as_array();
    EXPECT_EQ(users.size(), 2);

    const auto& u1 = users.at(0).as_object();
    EXPECT_EQ(string(u1.at("name").as_string()), string("admin@example.com"));
    EXPECT_TRUE(u1.contains("id"));
    EXPECT_FALSE(u1.at("id").as_string().empty());
    EXPECT_TRUE(u1.contains("active"));
    EXPECT_TRUE(u1.at("active").as_bool());
    const auto& auth1 = u1.at("auth").as_object();
    EXPECT_FALSE(auth1.contains("password"));
    EXPECT_TRUE(auth1.contains("hash"));
    EXPECT_TRUE(auth1.contains("seed"));
    auto hash1 = nsblast::lib::AuthMgr::createHash(
        string{auth1.at("seed").as_string()}, user_passwd);
    EXPECT_EQ(string(auth1.at("hash").as_string()), hash1);

    const auto& u2 = users.at(1).as_object();
    EXPECT_EQ(string(u2.at("name").as_string()), string("guest@example.com"));
    EXPECT_TRUE(u2.contains("id"));
    EXPECT_FALSE(u2.at("id").as_string().empty());
    EXPECT_TRUE(u2.contains("active"));
    EXPECT_TRUE(u2.at("active").as_bool());
    const auto& auth2 = u2.at("auth").as_object();
    EXPECT_FALSE(auth2.contains("password"));
    EXPECT_TRUE(auth2.contains("hash"));
    EXPECT_TRUE(auth2.contains("seed"));
    auto hash2 = nsblast::lib::AuthMgr::createHash(
        string{auth2.at("seed").as_string()}, user2_passwd);
    EXPECT_EQ(string(auth2.at("hash").as_string()), hash2);
}

TEST(ApiRequest, deleteTenant) {
    MockServer svr;

    auto json = getJsonForNewTenant("40af80e8-6772-11ee-bd63-4f3d941e113e");
    auto req = makeRequest(svr, "tenant", "", json, yahat::Request::Type::POST);

    RestApi api{svr};

    auto parsed = api.parse(req);
    auto res = api.onTenant(req, parsed);
    EXPECT_EQ(res.code, 201);

    req = makeRequest(svr, "tenant", "40af80e8-6772-11ee-bd63-4f3d941e113e", json, yahat::Request::Type::DELETE);
    parsed = api.parse(req);
    res = api.onTenant(req, parsed);
    EXPECT_EQ(res.code, 200);

    res = api.onTenant(req, parsed);
    EXPECT_EQ(res.code, 404);
}

TEST(ApiRequest, createRole) {
    MockServer svr;


    auto json = getJsonForNewRole("testrole");
    auto req = makeRequest(svr, "role", "", json, yahat::Request::Type::POST);

    RestApi api{svr};

    auto parsed = api.parse(req);
    auto res = api.onRole(req, parsed);
    EXPECT_EQ(res.code, 201);

    auto tenant = svr.auth().getTenant(boost::uuids::to_string(nsblast::lib::nsblastTenantUuid));
    EXPECT_TRUE(tenant);
    auto roleit = tenant->roles().begin();
    EXPECT_EQ(roleit->name(), "Administrator");
    ++roleit;
    EXPECT_EQ(roleit->name(), "testrole");
}

TEST(ApiRequest, upsertNewRole) {
    MockServer svr;


    auto json = getJsonForNewRole("testrole");
    auto req = makeRequest(svr, "role", "testrole", json, yahat::Request::Type::PUT);

    RestApi api{svr};

    auto parsed = api.parse(req);
    auto res = api.onRole(req, parsed);
    EXPECT_EQ(res.code, 201);

    auto tenant = svr.auth().getTenant(boost::uuids::to_string(nsblast::lib::nsblastTenantUuid));
    EXPECT_TRUE(tenant);
    auto roleit = tenant->roles().begin();
    EXPECT_EQ(roleit->name(), "Administrator");
    ++roleit;
    EXPECT_EQ(roleit->name(), "testrole");
}

TEST(ApiRequest, upsertNewRoleMissingTarget) {
    MockServer svr;


    auto json = getJsonForNewRole("testrole");
    auto req = makeRequest(svr, "role", "", json, yahat::Request::Type::PUT);

    RestApi api{svr};

    auto parsed = api.parse(req);
    auto res = api.onRole(req, parsed);
    EXPECT_EQ(res.code, 400);
}

TEST(ApiRequest, createRoleNoName) {
    MockServer svr;


    auto json = getJsonForNewRole("");
    auto req = makeRequest(svr, "role", "", json, yahat::Request::Type::POST);

    RestApi api{svr};

    auto parsed = api.parse(req);
    auto res = api.onRole(req, parsed);
    EXPECT_EQ(res.code, 400);
}

// Not implemented
TEST(ApiRequest, DISABLED_renameExistingRole) {
    MockServer svr;


    auto json = getJsonForNewRole("testrole");
    auto req = makeRequest(svr, "role", "", json, yahat::Request::Type::POST);

    RestApi api{svr};

    auto parsed = api.parse(req);
    auto res = api.onRole(req, parsed);
    EXPECT_EQ(res.code, 201);

    json = getJsonForNewRole("renamed");
    req = makeRequest(svr, "role", "testrole", json, yahat::Request::Type::PUT);

    parsed = api.parse(req);
    res = api.onRole(req, parsed);
    EXPECT_EQ(res.code, 200);

    auto tenant = svr.auth().getTenant(boost::uuids::to_string(nsblast::lib::nsblastTenantUuid));
    EXPECT_TRUE(tenant);
    auto roleit = tenant->roles().begin();
    EXPECT_EQ(roleit->name(), "Administrator");
    ++roleit;
    EXPECT_EQ(roleit->name(), "renamed");
}

TEST(ApiRequest, getRole) {
    MockServer svr;


    auto req = makeRequest(svr, "role", "Administrator", {}, yahat::Request::Type::GET);

    RestApi api{svr};

    auto parsed = api.parse(req);
    auto res = api.onRole(req, parsed);
    EXPECT_EQ(res.code, 200);

    const auto rbuf = boost::json::parse(res.body);
    const auto& ro = rbuf.as_object().at("value").as_object();
    EXPECT_EQ(ro.at("name").as_string(), "Administrator");
}

TEST(ApiRequest, listRoles) {
    MockServer svr;


    auto req = makeRequest(svr, "role", "", {}, yahat::Request::Type::GET);

    RestApi api{svr};

    auto parsed = api.parse(req);
    auto res = api.onRole(req, parsed);
    EXPECT_EQ(res.code, 200);

    const auto rbuf = boost::json::parse(res.body);
    const auto& ra = rbuf.as_object().at("value").as_array();
    EXPECT_EQ(ra.at(0).as_object().at("name").as_string(), "Administrator");
}

TEST(ApiRequest, deleteRole) {
    MockServer svr;


    auto json = getJsonForNewRole("testrole");
    auto req = makeRequest(svr, "role", "", json, yahat::Request::Type::POST);

    RestApi api{svr};

    auto parsed = api.parse(req);
    auto res = api.onRole(req, parsed);
    EXPECT_EQ(res.code, 201);

    req = makeRequest(svr, "role", "testrole", json, yahat::Request::Type::DELETE);
    parsed = api.parse(req);
    res = api.onRole(req, parsed);
    EXPECT_EQ(res.code, 200);

    res = api.onRole(req, parsed);
    EXPECT_EQ(res.code, 404);
}

TEST(ApiRequest, createUser) {
    MockServer svr;


    auto json = getJsonForNewUser("testUser");
    auto req = makeRequest(svr, "user", "", json, yahat::Request::Type::POST);

    RestApi api{svr};

    auto res = api.onReqest(req);
    EXPECT_EQ(res.code, 201);

    auto tenant = svr.auth().getTenant(boost::uuids::to_string(nsblast::lib::nsblastTenantUuid));
    EXPECT_TRUE(tenant);
    auto Userit = tenant->users().begin();
    EXPECT_EQ(Userit->name(), "admin");
    ++Userit;
    EXPECT_EQ(Userit->name(), "testUser");
}

TEST(ApiRequest, upsertNewUser) {
    MockServer svr;


    auto json = getJsonForNewUser("testUser");
    auto req = makeRequest(svr, "user", "testUser", json, yahat::Request::Type::PUT);

    RestApi api{svr};

    auto res = api.onReqest(req);
    EXPECT_EQ(res.code, 201);

    auto tenant = svr.auth().getTenant(boost::uuids::to_string(nsblast::lib::nsblastTenantUuid));
    EXPECT_TRUE(tenant);
    auto Userit = tenant->users().begin();
    EXPECT_EQ(Userit->name(), "admin");
    ++Userit;
    EXPECT_EQ(Userit->name(), "testUser");
}

TEST(ApiRequest, upsertNewUserMissingTarget) {
    MockServer svr;


    auto json = getJsonForNewUser("testUser");
    auto req = makeRequest(svr, "user", "", json, yahat::Request::Type::PUT);

    RestApi api{svr};

    auto res = api.onReqest(req);
    EXPECT_EQ(res.code, 400);
}

TEST(ApiRequest, uppercaseTarget) {
    MockServer svr;


    auto json = getJsonForNewUser("testUser");
    auto req = makeRequest(svr, "User", "testUser", json, yahat::Request::Type::PUT);

    RestApi api{svr};
    auto res = api.onReqest(req);
    EXPECT_EQ(res.code, 404);
}

TEST(ApiRequest, createUserNoName) {
    MockServer svr;


    auto json = getJsonForNewUser("");
    auto req = makeRequest(svr, "user", "", json, yahat::Request::Type::POST);

    RestApi api{svr};

    auto res = api.onReqest(req);
    EXPECT_EQ(res.code, 400);
}

TEST(ApiRequest, upsertExistingUser) {
    MockServer svr;


    auto json = getJsonForNewUser("testUser");
    auto req = makeRequest(svr, "user", "", json, yahat::Request::Type::POST);

    RestApi api{svr};

    auto res = api.onReqest(req);
    EXPECT_EQ(res.code, 201);

    json = getJsonForNewUser("renamed");
    req = makeRequest(svr, "user", "testUser", json, yahat::Request::Type::PUT);

    res = api.onReqest(req);
    EXPECT_EQ(res.code, 200);

    auto tenant = svr.auth().getTenant(boost::uuids::to_string(nsblast::lib::nsblastTenantUuid));
    EXPECT_TRUE(tenant);
    auto Userit = tenant->users().begin();
    EXPECT_EQ(Userit->name(), "admin");
    ++Userit;
    EXPECT_EQ(Userit->name(), "renamed");
}

TEST(ApiRequest, getUser) {
    MockServer svr;


    auto req = makeRequest(svr, "user", "admin", {}, yahat::Request::Type::GET);

    RestApi api{svr};

    auto res = api.onReqest(req);
    EXPECT_EQ(res.code, 200);

    const auto rbuf = boost::json::parse(res.body);
    const auto& ro = rbuf.as_object().at("value").as_object();
    EXPECT_EQ(ro.at("name").as_string(), "admin");
}

TEST(ApiRequest, listUsers) {
    MockServer svr;


    auto req = makeRequest(svr, "user", "", {}, yahat::Request::Type::GET);

    RestApi api{svr};

    auto res = api.onReqest(req);
    EXPECT_EQ(res.code, 200);

    const auto rbuf = boost::json::parse(res.body);
    const auto& ra = rbuf.as_object().at("value").as_array();
    EXPECT_EQ(ra.at(0).as_object().at("name").as_string(), "admin");
}

TEST(ApiRequest, deleteUser) {
    MockServer svr;


    auto json = getJsonForNewUser("testUser");
    auto req = makeRequest(svr, "user", "", json, yahat::Request::Type::POST);

    RestApi api{svr};

    auto res = api.onReqest(req);
    EXPECT_EQ(res.code, 201);

    req = makeRequest(svr, "user", "testUser", json, yahat::Request::Type::DELETE);
    res = api.onReqest(req);
    EXPECT_EQ(res.code, 200);

    res = api.onReqest(req);
    EXPECT_EQ(res.code, 404);

    // Check that we still have the admin-user.
    req = makeRequest(svr, "user", "admin", {}, yahat::Request::Type::GET);
    res = api.onReqest(req);
    EXPECT_EQ(res.code, 200);

    const auto rbuf = boost::json::parse(res.body);
    const auto& ro = rbuf.as_object().at("value").as_object();
    EXPECT_EQ(ro.at("name").as_string(), "admin");
}

TEST(ApiRequest, createUserWithNonExistingRole) {
    MockServer svr;

    auto json = getJsonForNewUser("testUser");

    auto user_instance = boost::json::parse(json);
    auto& uo = user_instance.as_object();
    uo.at("roles").as_array().push_back("dontexist");

    json = boost::json::serialize(user_instance);

    auto req = makeRequest(svr, "user", "", json, yahat::Request::Type::POST);

    RestApi api{svr};

    auto res = api.onReqest(req);
    EXPECT_EQ(res.code, 400);
}

TEST(ApiRequest, listZones) {
    MockServer svr;
    RestApi api{svr};

    // Create zone
    string_view zone = "example.com";
    auto json = getZoneJson(zone);
    auto req = makeRequest(svr, "zone", zone, boost::json::serialize(json), yahat::Request::Type::POST);
    auto parsed = api.parse(req);
    auto res = api.onZone(req, parsed);
    EXPECT_EQ(res.code, 201);

    // LIst zone
    req = makeRequest(svr, "zone", "", {}, yahat::Request::Type::GET);
    parsed = api.parse(req);
    res = api.listZones(req, parsed);
    EXPECT_EQ(res.code, 200);

    LOG_TRACE << "Result-json: " << res.body;

    auto zones = boost::json::parse(res.body);
    EXPECT_FALSE(zones.at("error").as_bool());
    EXPECT_EQ(zones.at("value").as_array().size(), 1);
    EXPECT_EQ(zones.at("value").as_array()[0].as_string(), "example.com");

}

TEST(ApiRequest, listZonesAll) {
    MockServer svr;
    RestApi api{svr};

    // Create zone
    string_view zone = "example.com";
    auto json = getZoneJson(zone);
    auto req = makeRequest(svr, "zone", zone, boost::json::serialize(json), yahat::Request::Type::POST);
    auto parsed = api.parse(req);
    auto res = api.onZone(req, parsed);
    EXPECT_EQ(res.code, 201);

    // LIst zone
    req = makeRequest(svr, "zone", "", {}, yahat::Request::Type::GET);
    req.arguments["tenant"] = "*";
    parsed = api.parse(req);
    res = api.listZones(req, parsed);
    EXPECT_EQ(res.code, 200);

    LOG_TRACE << "Result-json: " << res.body;

    auto zones = boost::json::parse(res.body);
    EXPECT_FALSE(zones.at("error").as_bool());
    EXPECT_EQ(zones.at("value").as_array().size(), 1);
    EXPECT_EQ(zones.at("value").as_array()[0].as_object().at("zone"), "example.com");
    auto nsblast_tenant = boost::uuids::to_string(nsblast::lib::nsblastTenantUuid);
    EXPECT_EQ(zones.at("value").as_array()[0].as_object().at("tenant").as_string(), nsblast_tenant);

}

TEST(ApiRequest, listZonesPagination) {
    MockServer svr;
    RestApi api{svr};

    auto constexpr num_zones = 55;
    auto constexpr limit = 10;
    const auto str_limit = std::format("{}", limit);
    auto constexpr expected_pages = 6;



    // Create zone
    for(auto i = 0; i < num_zones; ++i) {
        auto this_zone = format("example-{}.com", i);
        auto json = getZoneJson(this_zone);
        auto req = makeRequest(svr, "zone", this_zone, boost::json::serialize(json), yahat::Request::Type::POST);
        auto parsed = api.parse(req);
        auto res = api.onZone(req, parsed);
        EXPECT_EQ(res.code, 201);
    }

    // List zone, page 1
    auto req = makeRequest(svr, "zone", "", {}, yahat::Request::Type::GET);
    req.arguments["limit"] = str_limit;
    auto parsed = api.parse(req);
    auto res = api.listZones(req, parsed);
    EXPECT_EQ(res.code, 200);

    LOG_TRACE << "Result-json: " << res.body;

    auto zones = boost::json::parse(res.body);
    EXPECT_EQ(zones.at("value").as_array().size(), limit);
    EXPECT_TRUE(zones.at("more").as_bool());
    EXPECT_FALSE(zones.at("error").as_bool());

    size_t pages = 2;
    for(; pages <= expected_pages; ++pages) {
        string next_key = string{zones.at("value").as_array().back().as_string()};

        req = makeRequest(svr, "zone", "", {}, yahat::Request::Type::GET);

        req.arguments["limit"] = str_limit;
        req.arguments["from"] = next_key;
        parsed = api.parse(req);

        res = api.listZones(req, parsed);
        EXPECT_EQ(res.code, 200);

        LOG_TRACE << "Result-json: " << res.body;
        zones = boost::json::parse(res.body);
        EXPECT_FALSE(zones.at("error").as_bool());

        if (pages == expected_pages) {
            EXPECT_FALSE(zones.at("more").as_bool());
            EXPECT_EQ(zones.at("value").as_array().size(), 5);
        } else {
            EXPECT_TRUE(zones.at("more").as_bool());
            EXPECT_EQ(zones.at("value").as_array().size(), limit);
        }

        if (!zones.at("more").as_bool()) {
            break;
        }
    }

    EXPECT_EQ(pages, expected_pages);
}

TEST(ApiRequest, listZonesPaginationMultipleTenants) {
    MockServer svr;
    RestApi api{svr};

    auto constexpr num_zones = 55;
    auto constexpr limit = 10;
    const auto str_limit = std::format("{}", limit);
    auto constexpr expected_pages = 6;



    // Create zone
    unsigned user_count = 0;
    string tenant_id = boost::uuids::to_string(nsblast::lib::nsblastTenantUuid);
    string current_user;
    for(auto i = 0; i < num_zones; ++i) {

        if (i % 12) {
            tenant_id = newUuidStr();
            current_user = format("user-{}", user_count);
            ++user_count;
            svr.createTenant(tenant_id, current_user,
                             user_passwd, {},
                             {"USE_API", "CREATE_ZONE", "LIST_ZONES"});
        }

        auto this_zone = format("example-{}.com", i);
        auto json = getZoneJson(this_zone);
        auto req = makeRequest(svr, "zone", this_zone, boost::json::serialize(json), yahat::Request::Type::POST);

        if (!current_user.empty()) {
            req.auth = svr.getAuthAs(current_user, user_passwd);
        }
        auto parsed = api.parse(req);
        auto res = api.onZone(req, parsed);
        EXPECT_EQ(res.code, 201);
    }

    // List zone, page 1
    auto req = makeRequest(svr, "zone", "", {}, yahat::Request::Type::GET);
    req.arguments["limit"] = str_limit;
    req.arguments["tenant"] = "*";
    auto parsed = api.parse(req);
    auto res = api.listZones(req, parsed);
    EXPECT_EQ(res.code, 200);

    LOG_TRACE << "Result-json: " << res.body;

    auto zones = boost::json::parse(res.body);
    EXPECT_EQ(zones.at("value").as_array().size(), limit);
    EXPECT_TRUE(zones.at("more").as_bool());
    EXPECT_FALSE(zones.at("error").as_bool());

    size_t pages = 2;
    for(; pages <= expected_pages; ++pages) {
        string next_key = string{zones.at("value").as_array().back().as_object().at("zone").as_string()};

        req = makeRequest(svr, "zone", "", {}, yahat::Request::Type::GET);

        req.arguments["limit"] = str_limit;
        req.arguments["from"] = next_key;
        req.arguments["tenant"] = "*";
        parsed = api.parse(req);

        res = api.listZones(req, parsed);
        EXPECT_EQ(res.code, 200);

        LOG_TRACE << "Result-json: " << res.body;
        zones = boost::json::parse(res.body);
        EXPECT_FALSE(zones.at("error").as_bool());

        if (pages == expected_pages) {
            EXPECT_FALSE(zones.at("more").as_bool());
            EXPECT_EQ(zones.at("value").as_array().size(), 5);
        } else {
            EXPECT_TRUE(zones.at("more").as_bool());
            EXPECT_EQ(zones.at("value").as_array().size(), limit);
        }

        if (!zones.at("more").as_bool()) {
            break;
        }
    }

    EXPECT_EQ(pages, expected_pages);
}

TEST(ApiRequest, listZone) {

    MockServer svr;
    RestApi api{svr};

    // Create zone
    string_view zone = "example.com";
    auto json = getZoneJson(zone);
    auto req = makeRequest(svr, "zone", zone, boost::json::serialize(json), yahat::Request::Type::POST);
    auto parsed = api.parse(req);
    auto res = api.onZone(req, parsed);
    EXPECT_EQ(res.code, 201);

    // List fqdn's in zone zone
    req = makeRequest(svr, "zone", zone, {}, yahat::Request::Type::GET);
    parsed = api.parse(req);
    res = api.listZone(req, parsed);
    EXPECT_EQ(res.code, 200);

    LOG_TRACE << "Result-json: " << res.body;

    auto result = boost::json::parse(res.body);
    EXPECT_FALSE(result.at("error").as_bool());
    EXPECT_EQ(result.at("value").as_array().size(), 1);
    EXPECT_EQ(result.at("value").as_array()[0].as_string(), "example.com");
}

TEST(ApiRequest, listZoneWithPagination) {

    MockServer svr;
    RestApi api{svr};

    auto constexpr num_fqdns = 55;
    auto constexpr limit = 10;
    const auto str_limit = std::format("{}", limit);
    auto constexpr expected_pages = 6;

    // Create zone
    string_view zone = "example.com";
    auto json = getZoneJson(zone);
    auto req = makeRequest(svr, "zone", zone, boost::json::serialize(json), yahat::Request::Type::POST);
    auto parsed = api.parse(req);
    auto res = api.onZone(req, parsed);
    EXPECT_EQ(res.code, 201);

    const auto ajson = getAJson();

    // Make more fqdn's
    for(auto i = 0; i < num_fqdns; ++i) {
        req = makeRequest(svr, "rr",
                               format("{}-test.{}", i, zone),
                               ajson,
                               yahat::Request::Type::POST);

        parsed = api.parse(req);
        res = api.onResourceRecord(req, parsed);
        EXPECT_EQ(res.code, 201);
    }

    // List fqdn's in zone zone
    req = makeRequest(svr, "rr", zone, {}, yahat::Request::Type::GET);
    req.arguments["limit"] = str_limit;
    parsed = api.parse(req);
    res = api.listZone(req, parsed);
    EXPECT_EQ(res.code, 200);

    LOG_TRACE << "Result-json: " << res.body;

    auto result = boost::json::parse(res.body);
    EXPECT_FALSE(result.at("error").as_bool());
    EXPECT_EQ(result.at("value").as_array().size(), limit);
    EXPECT_EQ(string_view{result.at("value").as_array()[0].as_string()},
              string_view{"example.com"});

    size_t pages = 2;
    for(; pages <= expected_pages; ++pages) {
        auto next_key = string{result.at("value").as_array().back().as_string()};

        req = makeRequest(svr, "rr", zone, {}, yahat::Request::Type::GET);

        req.arguments["limit"] = str_limit;
        req.arguments["from"] = next_key;
        parsed = api.parse(req);

        res = api.listZone(req, parsed);
        EXPECT_EQ(res.code, 200);

        LOG_TRACE << "Result-json: " << res.body;
        result = boost::json::parse(res.body);
        EXPECT_FALSE(result.at("error").as_bool());

        if (pages == expected_pages) {
            EXPECT_FALSE(result.at("more").as_bool());
            EXPECT_EQ(result.at("value").as_array().size(), 6);
            EXPECT_EQ(result.at("value").as_array().back().as_string(), "49-test.example.com");
        } else {
            EXPECT_TRUE(result.at("more").as_bool());
            EXPECT_EQ(result.at("value").as_array().size(), limit);
        }

        if (!result.at("more").as_bool()) {
            break;
        }
    }

    EXPECT_EQ(pages, expected_pages);
}

TEST(ApiRequest, getRr) {

    MockServer svr;
    RestApi api{svr};

    // Create zone
    string_view zone = "example.com";
    auto json = getZoneJson(zone);
    auto req = makeRequest(svr, "zone", zone, boost::json::serialize(json), yahat::Request::Type::POST);
    auto parsed = api.parse(req);
    auto res = api.onZone(req, parsed);
    EXPECT_EQ(res.code, 201);

    // Get RR's in zone zone
    req = makeRequest(svr, "rr", zone, {}, yahat::Request::Type::GET);
    parsed = api.parse(req);
    res = api.onResourceRecord(req, parsed);
    EXPECT_EQ(res.code, 200);

    LOG_DEBUG << "Result-json: " << res.body;

    EXPECT_EQ(res.body, R"({"rcode":200,"error":false,"message":"","value":{"fqdn":"example.com","ttl":1000,"soa":{"mname":"ns1.example.com","rname":"hostmaster.example.com","email":"hostmaster@example.com","serial":1000,"refresh":1001,"retry":1002,"expire":1003,"minimum":1004},"ns":["ns1.example.com","ns2.example.com"]}})");
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);

    logfault::LogManager::Instance().AddHandler(
        make_unique<logfault::StreamHandler>(clog, logfault::LogLevel::TRACE));
    return RUN_ALL_TESTS();
}
