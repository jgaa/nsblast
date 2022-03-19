
#include "gtest/gtest.h"
#include "RestApi.h"

#include "TmpDb.h"

#include "test_res.h"

using namespace std;
using namespace nsblast;
using namespace nsblast::lib;


TEST(writeZone, newZone) {
    TmpDb db;

    Zone zone;
    auto soa = new Soa;
    soa->set_ttl(123);
    zone.set_allocated_soa(soa);

    EXPECT_NO_THROW(db->writeZone("example.com", zone, true));

    // Verify
    auto z = db->getZone("example.com");
    EXPECT_EQ(z.has_value(), true);
    if (z) {
        EXPECT_EQ(z->soa().ttl(), 123);
        EXPECT_EQ(z->soa().serial(), 0);
    }

    EXPECT_EQ(db->getZone("example.net").has_value(), false);
}

TEST(writeZone, zoneSerialIncrement) {
    TmpDb db;

    {
        Zone zone;
        auto soa = new Soa;
        soa->set_ttl(123);
        zone.set_allocated_soa(soa);

        EXPECT_NO_THROW(db->writeZone("example.com", zone, true));
    }

    {
        Zone zone;
        EXPECT_NO_THROW(db->writeZone("example.com", zone));
    }

    {
        Zone zone;
        EXPECT_NO_THROW(db->writeZone("example.com", zone));
    }

    // Verify
    auto z = db->getZone("example.com");
    EXPECT_EQ(z.has_value(), true);
    if (z) {
        EXPECT_EQ(z->soa().ttl(), 123);
        EXPECT_EQ(z->soa().serial(), 2);
    }
}

TEST(writeZone, zoneMerge) {
    TmpDb db;

    {
        Zone zone;
        auto soa = new Soa;
        soa->set_serial(1);
        soa->set_ttl(123);
        zone.set_allocated_soa(soa);
        zone.set_tns("foobar");
        zone.add_ns()->set_fqdn("ns1.example.com");
        zone.add_ns()->set_fqdn("ns2.example.com");

        EXPECT_NO_THROW(db->writeZone("example.com", zone, true));
    }

    // Verify
    auto z = db->getZone("example.com");
    EXPECT_EQ(z.has_value(), true);
    if (z) {
        EXPECT_EQ(z->soa().ttl(), 123);
        EXPECT_EQ(z->soa().serial(), 1);
        EXPECT_EQ(z->soa().refresh(), 0);
        EXPECT_EQ(z->soa().retry(), 0);
        EXPECT_EQ(z->soa().mname(), "");
        EXPECT_EQ(z->soa().rname(), "");
        EXPECT_EQ(z->soa().minimum(), 0);
        EXPECT_EQ(z->tns(), "foobar");
        EXPECT_EQ(z->ns_size(), 2);
        EXPECT_EQ(z->ns().at(0).fqdn(), "ns1.example.com");
        EXPECT_EQ(z->ns().at(1).fqdn(), "ns2.example.com");
    }

    {
        Zone zone;
        auto soa = new Soa;
        soa->set_serial(10);
        soa->set_ttl(1);
        soa->set_refresh(2);
        soa->set_retry(3);
        soa->set_mname("ns1");
        zone.set_allocated_soa(soa);
        zone.set_tns("barfoo");
        EXPECT_NO_THROW(db->writeZone("example.com", zone));
    }

    // Verify
    z = db->getZone("example.com");
    EXPECT_EQ(z.has_value(), true);
    if (z) {
        EXPECT_EQ(z->soa().ttl(), 1);
        EXPECT_EQ(z->soa().serial(), 10);
        EXPECT_EQ(z->soa().refresh(), 2);
        EXPECT_EQ(z->soa().retry(), 3);
        EXPECT_EQ(z->soa().mname(), "ns1");
        EXPECT_EQ(z->soa().rname(), "");
        EXPECT_EQ(z->soa().minimum(), 0);
        EXPECT_EQ(z->tns(), "barfoo");
        EXPECT_EQ(z->ns_size(), 2);
        EXPECT_EQ(z->ns().at(0).fqdn(), "ns1.example.com");
        EXPECT_EQ(z->ns().at(1).fqdn(), "ns2.example.com");
    }

    {
        Zone zone;
        auto soa = new Soa;
        soa->set_ttl(0); // Protobuf3 can't distinguish between 0 and `unset`, so soa.MergeFrom() will choose the soa with a ttl value > 0
        soa->set_rname("hostmaster.example.com");
        soa->set_mname("ns");
        zone.set_allocated_soa(soa);
        zone.add_ns()->set_fqdn("ns.example.com");
        EXPECT_NO_THROW(db->writeZone("example.com", zone));
    }

    // Verify
    z = db->getZone("example.com");
    EXPECT_EQ(z.has_value(), true);
    if (z) {
        EXPECT_EQ(z->soa().ttl(), 1);
        EXPECT_EQ(z->soa().serial(), 11);
        EXPECT_EQ(z->soa().refresh(), 2);
        EXPECT_EQ(z->soa().retry(), 3);
        EXPECT_EQ(z->soa().mname(), "ns");
        EXPECT_EQ(z->soa().rname(), "hostmaster.example.com");
        EXPECT_EQ(z->soa().minimum(), 0);
        EXPECT_EQ(z->tns(), "barfoo");
        EXPECT_EQ(z->ns_size(), 1);
        EXPECT_EQ(z->ns().at(0).fqdn(), "ns.example.com");
    }
}

TEST(writeZone, zoneRewrite1) {
    TmpDb db;

    {
        Zone zone;
        auto soa = new Soa;
        soa->set_ttl(123);
        zone.set_allocated_soa(soa);
        zone.set_tns("foobar");

        EXPECT_NO_THROW(db->writeZone("example.com", zone));
    }

    // Verify
    auto z = db->getZone("example.com");
    EXPECT_EQ(z.has_value(), true);
    if (z) {
        EXPECT_EQ(z->soa().serial(), 0);
        EXPECT_EQ(z->soa().ttl(), 123);
        EXPECT_EQ(z->soa().refresh(), 0);
        EXPECT_EQ(z->soa().retry(), 0);
        EXPECT_EQ(z->soa().mname(), "");
        EXPECT_EQ(z->soa().rname(), "");
        EXPECT_EQ(z->soa().minimum(), 0);
        EXPECT_EQ(z->tns(), "foobar");
    }

    {
        Zone zone;
        auto soa = new Soa;
        soa->set_mname("ns1");
        zone.set_allocated_soa(soa);
        EXPECT_NO_THROW(db->writeZone("example.com", zone, {}, false));
    }

    // Verify
    z = db->getZone("example.com");
    EXPECT_EQ(z.has_value(), true);
    if (z) {
        EXPECT_EQ(z->soa().ttl(), 0);
        EXPECT_EQ(z->soa().serial(), 1);
        EXPECT_EQ(z->soa().refresh(), 0);
        EXPECT_EQ(z->soa().retry(), 0);
        EXPECT_EQ(z->soa().mname(), "ns1");
        EXPECT_EQ(z->soa().rname(), "");
        EXPECT_EQ(z->soa().minimum(), 0);
        EXPECT_EQ(z->tns(), "");
    }
}

TEST(writeZone, zoneRewrite2) {
    TmpDb db;

    {
        Zone zone;
        auto soa = new Soa;
        soa->set_ttl(123);
        zone.set_allocated_soa(soa);

        EXPECT_NO_THROW(db->writeZone("example.com", zone));
    }

    // Verify
    auto z = db->getZone("example.com");
    EXPECT_EQ(z.has_value(), true);
    if (z) {
        EXPECT_EQ(z->soa().serial(), 0);
        EXPECT_EQ(z->soa().ttl(), 123);
        EXPECT_EQ(z->soa().refresh(), 0);
        EXPECT_EQ(z->soa().retry(), 0);
        EXPECT_EQ(z->soa().mname(), "");
        EXPECT_EQ(z->soa().rname(), "");
        EXPECT_EQ(z->soa().minimum(), 0);
    }

    {
        Zone zone;
        EXPECT_NO_THROW(db->writeZone("example.com", zone, {}, false));
    }

    // Verify
    z = db->getZone("example.com");
    EXPECT_EQ(z.has_value(), true);
    if (z) {
        EXPECT_EQ(z->soa().ttl(), 0);
        EXPECT_EQ(z->soa().serial(), 1);
        EXPECT_EQ(z->soa().refresh(), 0);
        EXPECT_EQ(z->soa().retry(), 0);
        EXPECT_EQ(z->soa().mname(), "");
        EXPECT_EQ(z->soa().rname(), "");
        EXPECT_EQ(z->soa().minimum(), 0);
    }
}

TEST(writeZone, newZoneFailDuplicate) {
    TmpDb db;

    Zone zone;
    auto soa = new Soa;
    soa->set_ttl(123);
    zone.set_allocated_soa(soa);

    EXPECT_NO_THROW(db->writeZone("example.com", zone, true));
    EXPECT_ANY_THROW(db->writeZone("example.com", zone, true));
}

TEST(writeZone, updateZoneFailNonexistant) {
    TmpDb db;

    Zone zone;
    auto soa = new Soa;
    soa->set_ttl(123);
    zone.set_allocated_soa(soa);

    EXPECT_ANY_THROW(db->writeZone("example.com", zone, false));
}

TEST(deleteZone, success) {
    TmpDb db;

    Zone zone;
    EXPECT_NO_THROW(db->writeZone("example.com", zone, true));
    EXPECT_EQ(db->getZone("example.com").has_value(), true);
    EXPECT_NO_THROW(db->deleteZone("example.com"));
    EXPECT_EQ(db->getZone("example.com").has_value(), false);
}

TEST(deleteZone, nonexisting) {
    TmpDb db;
    EXPECT_ANY_THROW(db->deleteZone("example.com"));
}

TEST(findZone, fullMatch) {
    TmpDb db;

    Zone zone;
    auto soa = new Soa;
    soa->set_ttl(123);
    zone.set_allocated_soa(soa);

    EXPECT_NO_THROW(db->writeZone("example.com", zone, true));

    // Verify
    auto r = db->findZone("example.com");
    EXPECT_EQ(r.has_value(), true);
    if (r) {
        const auto& [fdqn, z] = *r;
        EXPECT_EQ(z.soa().ttl(), 123);
        EXPECT_EQ(z.soa().serial(), 0);
        EXPECT_EQ(fdqn, "example.com");
    }
}

TEST(findZone, partialMatch) {
    TmpDb db;

    Zone zone;
    auto soa = new Soa;
    soa->set_ttl(123);
    zone.set_allocated_soa(soa);

    EXPECT_NO_THROW(db->writeZone("example.com", zone, true));

    // Verify
    auto r = db->findZone("a.example.com");
    EXPECT_EQ(r.has_value(), true);
    if (r) {
        const auto& [fdqn, z] = *r;
        EXPECT_EQ(z.soa().ttl(), 123);
        EXPECT_EQ(z.soa().serial(), 0);
        EXPECT_EQ(fdqn, "example.com");
    }

    // Verify
    r = db->findZone("a.b.c.d.example.com");
    EXPECT_EQ(r.has_value(), true);
    if (r) {
        const auto& [fdqn, z] = *r;
        EXPECT_EQ(z.soa().ttl(), 123);
        EXPECT_EQ(z.soa().serial(), 0);
        EXPECT_EQ(fdqn, "example.com");
    }
}

TEST(findZone, noMatch) {
    TmpDb db;

    Zone zone;
    auto soa = new Soa;
    soa->set_ttl(123);
    zone.set_allocated_soa(soa);

    EXPECT_NO_THROW(db->writeZone("example.com", zone, true));

    // Verify
    auto r = db->findZone("example.net");
    EXPECT_EQ(r.has_value(), false);
    r = db->findZone("anexample.com");
    EXPECT_EQ(r.has_value(), false);
    r = db->findZone("a.b.c.d.e.f.g.e.xample.com");
    EXPECT_EQ(r.has_value(), false);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
