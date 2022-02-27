#include "gtest/gtest.h"
#include "RestApi.h"
#include "Db.h"

#include "test_res.h"

using namespace std;
using namespace nsblast;
using namespace nsblast::lib;


TEST(testRestApi, parse1) {
    Request r{"/api/v1/zone/example.com", "/api/v1"};
    Config c;
    Db d{c};
    RestApi api{d, c};
    auto p = api.parse(r);
    EXPECT_EQ(p.base, "zone/example.com");
    EXPECT_EQ(p.what, "zone");
    EXPECT_EQ(p.fdqn, "example.com");
    EXPECT_EQ(p.operation, "");
}

TEST(testRestApi, parse2) {
    Request r{"/api/v1/zone/a.b.example.com/test", "/api/v1"};
    Config c;
    Db d{c};
    RestApi api{d, c};
    auto p = api.parse(r);
    EXPECT_EQ(p.base, "zone/a.b.example.com/test");
    EXPECT_EQ(p.what, "zone");
    EXPECT_EQ(p.fdqn, "a.b.example.com");
    EXPECT_EQ(p.operation, "test");
}

TEST(testRestApi, parse3) {
    Request r{"/api/v1/zone/a.b.example.com/test/z/x/c", "/api/v1"};
    Config c;
    Db d{c};
    RestApi api{d, c};
    auto p = api.parse(r);
    EXPECT_EQ(p.base, "zone/a.b.example.com/test/z/x/c");
    EXPECT_EQ(p.what, "zone");
    EXPECT_EQ(p.fdqn, "a.b.example.com");
    EXPECT_EQ(p.operation, "test/z/x/c");
}

TEST(testRestApi, serializeToZone1) {
    Config c;
    Db d{c};
    RestApi api{d, c};

    Zone zone;
    string json{R"({})"};
    EXPECT_EQ(api.fromJson(json, zone), true);
}

TEST(testRestApi, serializeToZone2) {
    Config c;
    Db d{c};
    RestApi api{d, c};

    Zone zone;
    string json{R"({
                "soa": {
                  "ttl": 1234,
                  "refresh": 11,
                  "retry": 12,
                  "expire": 13,
                  "minimum": 14,
                  "mname": "a.b.c",
                  "rname": "hostmaster.example.com"
                },
                "ns": [
                  {
                    "fqdn": "ns1.example.com"
                  },
                  {
                    "fqdn": "ns2.example.com"
                  },
                ],
                "tns": "foobar"
              })"};
    EXPECT_EQ(api.fromJson(json, zone), true);
    EXPECT_EQ(zone.soa().ttl(), 1234);
    EXPECT_EQ(zone.soa().mname(), "a.b.c");
    EXPECT_EQ(zone.tns(), "foobar");
}

TEST(testRestApi, serializeToZone3) {
    Config c;
    Db d{c};
    RestApi api{d, c};

    Zone zone;
    string json{R"({
                "soa": {
                  "ttl": 123
                },
                "ns": [
                  {
                    "fqdn": "ns1.example.com"
                  }
                ]
              })"};
    EXPECT_EQ(api.fromJson(json, zone), true);
    EXPECT_EQ(zone.soa().ttl(), 123);
    EXPECT_EQ(zone.soa().mname(), "");
    EXPECT_EQ(zone.tns(), "");
}

TEST(testRestApi, serializeToZoneFail1) {
    Config c;
    Db d{c};
    RestApi api{d, c};

    Zone zone;
    string json{R"({"foo": "bar"})"};
    EXPECT_EQ(api.fromJson(json, zone), false);
}

TEST(testRestApi, serializeToZoneFail2) {
    Config c;
    Db d{c};
    RestApi api{d, c};

    Zone zone;
    string json{R"()"};
    EXPECT_EQ(api.fromJson(json, zone), false);
}

TEST(testRestApi, serializeToZoneFail3) {
    Config c;
    Db d{c};
    RestApi api{d, c};

    Zone zone;
    string json{R"(testing)"};
    EXPECT_EQ(api.fromJson(json, zone), false);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
