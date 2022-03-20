#include "gtest/gtest.h"
#include "RestApi.h"
#include "Db.h"

#include "TmpDb.h"

#include "test_res.h"

using namespace std;

namespace nsblast::lib {

TEST(testRestApi, parse1) {
    Request r{"/api/v1/zone/example.com", "/api/v1"};
    Config c;
    Db d{c};
    RestApi api{d, c};
    auto p = api.parse(r);
    EXPECT_EQ(p.base, "zone/example.com");
    EXPECT_EQ(p.what, "zone");
    EXPECT_EQ(p.fqdn, "example.com");
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
    EXPECT_EQ(p.fqdn, "a.b.example.com");
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
    EXPECT_EQ(p.fqdn, "a.b.example.com");
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

TEST(testRestApi, reduce) {
    Config c;
    Db d{c};
    RestApi api{d, c};

    EXPECT_EQ(api.reduce("a.b.c"), "b.c");
    EXPECT_EQ(api.reduce("a.b"), "b");
    EXPECT_EQ(api.reduce(".a.b"), "a.b");
    EXPECT_EQ(api.reduce("."), "");
    EXPECT_EQ(api.reduce(""), "");
    EXPECT_EQ(api.reduce(api.reduce("a.b.c")), "c");
}

TEST(testRestApi, lookup) {
    Config c;
    TmpDb db;

    Zone zone;
    auto soa = new Soa;
    soa->set_ttl(123);
    zone.set_allocated_soa(soa);

    EXPECT_NO_THROW(db->writeZone("example.com", zone, true));

    RestApi api{*db, c};

    auto zi = api.lookupZone("example.com");
    EXPECT_EQ(zi.has_value(), true);
    EXPECT_EQ(zi->fqdn, "example.com");

    zi = api.lookupZone("a.b.example.com");
    EXPECT_EQ(zi.has_value(), true);
    EXPECT_EQ(zi->fqdn, "example.com");

    EXPECT_NO_THROW(db->writeZone("b.example.com", zone, true));

    zi = api.lookupZone("a.b.example.com");
    EXPECT_EQ(zi.has_value(), true);
    EXPECT_EQ(zi->fqdn, "b.example.com");

    EXPECT_NO_THROW(db->writeZone("leaf.z.x.c.v.example.com", zone, true));

    zi = api.lookupZone("z.x.c.v.example.com");
    EXPECT_EQ(zi.has_value(), true);
    EXPECT_EQ(zi->fqdn, "example.com");

    zi = api.lookupZone("leaf.z.x.c.v.example.com");
    EXPECT_EQ(zi.has_value(), true);
    EXPECT_EQ(zi->fqdn, "leaf.z.x.c.v.example.com");
    zi = api.lookupZone("aa.bb.cc.abc.leaf.z.x.c.v.example.com");
    EXPECT_EQ(zi.has_value(), true);
    EXPECT_EQ(zi->fqdn, "leaf.z.x.c.v.example.com");

    zi = api.lookupZone("a.b.c.example2.com");
    EXPECT_EQ(zi.has_value(), false);
}

TEST(testZoneApi, zonePOST) {
    Config c;
    TmpDb db;

    RestApi api{*db, c};
    auto json = R"({
        "soa":{"ttl":123, "rname":"hostmaster.example.com"},
        "ns":[{"fqdn":"127.0.0.1"}, {"fqdn":"127.0.0.2"}],
        "tns":"admin"
        })";

    Request req{"", "", "", json, Request::Type::POST};
    RestApi::Parsed parsed{"", "zone", "example.com"};
    auto res = api.updateZone(req, parsed, true, false);
    EXPECT_EQ(res.ok(), true);
    auto zi = api.lookupZone("example.com");
    EXPECT_EQ(zi.has_value(), true);
    if (zi) {
        EXPECT_EQ(zi->fqdn, "example.com");
        EXPECT_EQ(zi->zone.soa().ttl(), 123);
        EXPECT_EQ(zi->zone.soa().rname(), "hostmaster.example.com");
        EXPECT_EQ(zi->zone.tns(), "admin");
        EXPECT_EQ(zi->zone.ns(0).fqdn(), "127.0.0.1");
        EXPECT_EQ(zi->zone.ns(1).fqdn(), "127.0.0.2");
    }
}

TEST(testRrApi, updateResourceRecordAdd) {
    Config c;
    TmpDb db;

    {
        Zone zone;
        zone.set_tns("admin");
        db->writeZone("example.com", zone, true);
    }

    RestApi api{*db, c};
    auto json = R"({
        "a":["127.0.0.1"],
        "txt":"teste"
        })";

    Request req{"", "", "", json, Request::Type::POST};
    RestApi::Parsed parsed{"", "rr", "a.example.com"};
    auto zi = api.lookupZone("example.com");
    EXPECT_EQ(zi.has_value(), true);
    auto res = api.updateResourceRecord(req, parsed, *zi, true, false);
    EXPECT_EQ(res.ok(), true);
    auto rr = db->getRr("a.example.com");
    EXPECT_EQ(rr.has_value(), true);
    if (rr) {
        EXPECT_EQ(rr->a(0), "127.0.0.1");
        EXPECT_EQ(rr->aa_size(), 0);
        EXPECT_EQ(rr->txt(), "teste");
        EXPECT_EQ(rr->cname(), "");
        EXPECT_EQ(rr->mx_size(), 0);
    }
}

TEST(testRrApi, updateResourceRecordMerge) {
    Config c;
    TmpDb db;

    {
        Zone zone;
        zone.set_tns("admin");
        db->writeZone("example.com", zone, true);

        Rr rr;
        rr.add_a("127.0.0.1");
        rr.set_txt("teste");
        db->writeRr("example.com", "a.example.com", rr, true);
    }

    RestApi api{*db, c};
    auto json = R"({
        "a":["127.0.0.2"]
        })";

    Request req{"", "", "", json, Request::Type::PATCH};
    RestApi::Parsed parsed{"", "rr", "a.example.com"};
    auto zi = api.lookupZone("example.com");
    EXPECT_EQ(zi.has_value(), true);
    auto res = api.updateResourceRecord(req, parsed, *zi, false, true);
    EXPECT_EQ(res.ok(), true);
    auto rr = db->getRr("a.example.com");
    EXPECT_EQ(rr.has_value(), true);
    if (rr) {
        EXPECT_EQ(rr->a_size(), 1);
        EXPECT_EQ(rr->a(0), "127.0.0.2");
        EXPECT_EQ(rr->aa_size(), 0);
        EXPECT_EQ(rr->txt(), "teste");
        EXPECT_EQ(rr->cname(), "");
        EXPECT_EQ(rr->mx_size(), 0);
    }
}

TEST(testRrApi, updateResourceRecordReplace) {
    Config c;
    TmpDb db;

    {
        Zone zone;
        zone.set_tns("admin");
        db->writeZone("example.com", zone, true);

        Rr rr;
        rr.add_a("127.0.0.1");
        rr.set_txt("teste");
        db->writeRr("example.com", "a.example.com", rr, true);
    }

    RestApi api{*db, c};
    auto json = R"({
        "a":["127.0.0.2"]
        })";

    Request req{"", "", "", json, Request::Type::PUT};
    RestApi::Parsed parsed{"", "rr", "a.example.com"};
    auto zi = api.lookupZone("example.com");
    EXPECT_EQ(zi.has_value(), true);
    auto res = api.updateResourceRecord(req, parsed, *zi, {}, false);
    EXPECT_EQ(res.ok(), true);
    auto rr = db->getRr("a.example.com");
    EXPECT_EQ(rr.has_value(), true);
    if (rr) {
        EXPECT_EQ(rr->a_size(), 1);
        EXPECT_EQ(rr->a(0), "127.0.0.2");
        EXPECT_EQ(rr->aa_size(), 0);
        EXPECT_EQ(rr->txt(), "");
        EXPECT_EQ(rr->cname(), "");
        EXPECT_EQ(rr->mx_size(), 0);
    }
}

TEST(testRrApi, deleteResourceRecord) {
    Config c;
    TmpDb db;

    {
        Zone zone;
        zone.set_tns("admin");
        db->writeZone("example.com", zone, true);

        Rr rr;
        rr.add_a("127.0.0.1");
        rr.set_txt("teste");
        db->writeRr("example.com", "a.example.com", rr, true);
    }

    EXPECT_EQ(db->getRr("a.example.com").has_value(), true);

    RestApi api{*db, c};
    Request req{"", "", "", "", Request::Type::DELETE};
    RestApi::Parsed parsed{"", "rr", "a.example.com"};
    auto zi = api.lookupZone("example.com");
    EXPECT_EQ(zi.has_value(), true);
    api.deleteResourceRecord(req, parsed, *zi);

    EXPECT_EQ(db->getRr("a.example.com").has_value(), false);
    EXPECT_EQ(db->getZone("example.com").has_value(), true);
}

} // ns

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
