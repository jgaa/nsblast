
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

auto getZoneJson() {
    static const auto soa = boost::json::parse(R"({
    "soa": {
    "ttl": 1000,
    "refresh": 1001,
    "retry": 1002,
    "expire": 1003,
    "minimum": 1004,
    "mname": "hostmaster.example.com",
    "rname": "ns1.example.com"
    },
    "ns": [
    "ns1.example.com",
    "ns2.example.com"
    ]
    })");

    return soa;
}

} // anon ns

TEST(ApiValidate, soaOk) {
    EXPECT_NO_THROW(RestApi::validateSoa(getZoneJson()));
}

TEST(ApiValidate, soaTtlIsString) {

    auto json = getZoneJson();
    json.at("soa").at("ttl") = "teste";

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

TEST(ApiValidate, zoneWrongRname) {

    auto json = getZoneJson();
    json.at("soa").at("rname") = "foo.example.com";

    EXPECT_THROW(RestApi::validateZone(json), yahat::Response);
    try {
        RestApi::validateZone(json);
    } catch(const yahat::Response& ex) {
        EXPECT_EQ(ex.code, 400);
        EXPECT_EQ(ex.reason, "soa.rname must be one of the ns entries");
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

    sb.finish();

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
        EXPECT_EQ(soa.mname().string(), "hostmaster.example.com");
        EXPECT_EQ(soa.rname().string(), "ns1.example.com");
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

    sb.finish();

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

    sb.finish();

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

    sb.finish();

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
    sb.finish();

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
    sb.finish();

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
    json["mx"] = boost::json::object{};
    json["mx"].as_object()["priority"] = 10;
    json["mx"].as_object()["host"] = host;

    StorageBuilder sb;
    EXPECT_NO_THROW(RestApi::build(fqdn, 1000, sb, json));
    sb.finish();

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

auto makeRequest(const string& what, const string& fqdn, string json, yahat::Request::Type type) {
    static const string base = "/api/v1";

    std::string full_target = base + "/" + what + "/" + fqdn;

    return yahat::Request{nullptr, full_target, "/api/v1", "", "", move(json), type};
}

TEST(ApiRequest, onZoneOk) {

    TmpDb db;
    auto json = getZoneJson();
    auto req = makeRequest("zone", "example.com", boost::json::serialize(json), yahat::Request::Type::POST);

    RestApi api{db.config(), db.resource()};

    auto parsed = api.parse(req);

    auto res = api.onZone(req, parsed);

    EXPECT_EQ(res.code, 200);
}

TEST(ApiRequest, onZoneExists) {

    TmpDb db;
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

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);

    logfault::LogManager::Instance().AddHandler(
                make_unique<logfault::StreamHandler>(clog, logfault::LogLevel::DEBUGGING));
    return RUN_ALL_TESTS();
}
