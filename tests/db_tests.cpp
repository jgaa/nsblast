
#include "gtest/gtest.h"
#include "RestApi.h"

#include "TmpDb.h"

#include "nsblast/DnsMessages.h"
#include "nsblast/logging.h"

//#include "test_res.h"

using namespace std;
using namespace nsblast;
using namespace nsblast::lib;

//auto json = R"({
//"soa": {
//"ttl": 1000,
//"refresh": 1001,
//"retry": 1002,
//"expire": 1003,
//"minimum": 1004,
//"mname": "hostmaster.example.com",
//"rname": "ns1.example.com"
//},
//"ns": [
//{"fqdn": "ns1.example.com"},
//{"fqdn": "ns2.example.com"}
//]
//})";

TEST(DbWriteZone, newZone) {
    TmpDb db;

    const string_view fqdn = "example.com";
    StorageBuilder sb;
    sb.createSoa(fqdn, 1000, "hostmaster.example.com", "ns1.example.com", 1,
                 1001, 1002, 1003, 1004);
    sb.finish();

    auto tx = db->transaction();
    EXPECT_NO_THROW(tx->write(fqdn, sb.buffer(), true));
    EXPECT_NO_THROW(tx->commit());
}

// Fails - does not throw
TEST(DbWriteZone, newZoneOnExisting) {
    TmpDb db;

    const string_view fqdn = "example.com";
    StorageBuilder sb;
    sb.createSoa(fqdn, 1000, "hostmaster.example.com", "ns1.example.com", 1,
                 1001, 1002, 1003, 1004);
    sb.finish();

    {
        auto tx = db->transaction();
        EXPECT_FALSE(tx->keyExists(fqdn));
        EXPECT_NO_THROW(tx->write(fqdn, sb.buffer(), true));
        EXPECT_NO_THROW(tx->commit());
    }

    {
        auto tx = db->transaction();
        EXPECT_TRUE(tx->keyExists(fqdn));
        EXPECT_THROW(tx->write(fqdn, sb.buffer(), true), ResourceIf::AlreadyExistException);
        tx->commit();
    }
}

TEST(DbDeleteZone, existing) {
    TmpDb db;

    const string_view fqdn = "example.com";
    StorageBuilder sb;
    sb.createSoa(fqdn, 1000, "hostmaster.example.com", "ns1.example.com", 1,
                 1001, 1002, 1003, 1004);
    sb.finish();

    {
        auto tx = db->transaction();
        EXPECT_NO_THROW(tx->write(fqdn, sb.buffer(), true));
        EXPECT_NO_THROW(tx->commit());
    }
    {
        auto tx = db->transaction();
        EXPECT_NO_THROW(tx->remove(fqdn));
        EXPECT_NO_THROW(tx->commit());
    }
}

TEST(DbDeleteZone, existingRecursive) {
    TmpDb db;

    const string_view fqdn = "example.com";
    StorageBuilder sb;
    sb.createSoa(fqdn, 1000, "hostmaster.example.com", "ns1.example.com", 1,
                 1001, 1002, 1003, 1004);
    sb.finish();

    {
        auto tx = db->transaction();
        EXPECT_NO_THROW(tx->write(fqdn, sb.buffer(), true));
        EXPECT_NO_THROW(tx->commit());
    }
    {
        auto tx = db->transaction();
        EXPECT_NO_THROW(tx->remove(fqdn, true));
        EXPECT_NO_THROW(tx->commit());
    }
}

TEST(DbDeleteZone, nonexisting) {
    TmpDb db;

    const string_view fqdn = "example.com";

    auto tx = db->transaction();
    EXPECT_NO_THROW(tx->remove(fqdn));
    tx->commit();
}

TEST(DbReadZone, exists) {
    TmpDb db;

    const string_view fqdn = "example.com";
    StorageBuilder sb;
    sb.createSoa(fqdn, 1000, "hostmaster.example.com", "ns1.example.com", 1,
                 1001, 1002, 1003, 1004);
    sb.finish();

    {
        auto tx = db->transaction();
        EXPECT_NO_THROW(tx->write(fqdn, sb.buffer(), true));
        EXPECT_NO_THROW(tx->commit());
    }

    {
        auto tx = db->transaction();
        EXPECT_TRUE(tx->keyExists(fqdn));
        EXPECT_TRUE(tx->zoneExists(fqdn));
    }
}

TEST(DbReadZone, notExists) {
    TmpDb db;

    const string_view fqdn = "example.com";

    auto tx = db->transaction();
    EXPECT_FALSE(tx->zoneExists(fqdn));
}

TEST(DbReadZone, read) {
    TmpDb db;

    const string_view fqdn = "example.com";
    StorageBuilder sb;
    sb.createSoa(fqdn, 1000, "hostmaster.example.com", "ns1.example.com", 1,
                 1001, 1002, 1003, 1004);
    sb.finish();

    {
        auto tx = db->transaction();
        EXPECT_NO_THROW(tx->write(fqdn, sb.buffer(), true));
        EXPECT_NO_THROW(tx->commit());
    }

    {
        auto tx = db->transaction();
        auto b = tx->read(fqdn);
        EXPECT_TRUE(b);

        Entry entry{b->data()};
        EXPECT_TRUE(entry.flags().soa);
        EXPECT_EQ(entry.count(), 1);
        EXPECT_NE(entry.begin(), entry.end());

        const auto rr = *entry.begin();
        EXPECT_EQ(rr.type(), TYPE_SOA);
        RrSoa soa{entry.buffer(), rr.offset()};

        EXPECT_EQ(soa.mname().string(), "hostmaster.example.com");
        EXPECT_EQ(soa.rname().string(), "ns1.example.com");
        EXPECT_EQ(soa.serial(), 1);
        EXPECT_EQ(soa.ttl(), 1000);
        EXPECT_EQ(soa.refresh(), 1001);
        EXPECT_EQ(soa.retry(), 1002);
        EXPECT_EQ(soa.expire(), 1003);
        EXPECT_EQ(soa.minimum(), 1004);
    }
}

TEST(DbReadZone, readNoExist) {
    TmpDb db;

    const string_view fqdn = "example.com";

    auto tx = db->transaction();
    EXPECT_THROW(tx->read(fqdn), ResourceIf::NotFoundException);
}

#if 0

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
        const auto& [fqdn, z] = *r;
        EXPECT_EQ(z.soa().ttl(), 123);
        EXPECT_EQ(z.soa().serial(), 0);
        EXPECT_EQ(fqdn, "example.com");
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
        const auto& [fqdn, z] = *r;
        EXPECT_EQ(z.soa().ttl(), 123);
        EXPECT_EQ(z.soa().serial(), 0);
        EXPECT_EQ(fqdn, "example.com");
    }

    // Verify
    r = db->findZone("a.b.c.d.example.com");
    EXPECT_EQ(r.has_value(), true);
    if (r) {
        const auto& [fqdn, z] = *r;
        EXPECT_EQ(z.soa().ttl(), 123);
        EXPECT_EQ(z.soa().serial(), 0);
        EXPECT_EQ(fqdn, "example.com");
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

TEST(writeRr, addNew) {
    TmpDb db;
    string_view zone_fqdn{"example.com"};
    string_view rr_fqdn{"a.b.c.example.com"};

    Zone zone;
    EXPECT_NO_THROW(db->writeZone(zone_fqdn, zone, true));
    {
        auto z = db->getZone(zone_fqdn);
        EXPECT_EQ(z->soa().serial(), 0);
    }

    Rr rr;
    rr.add_a("127.0.0.1");
    rr.set_txt("testing");

    db->writeRr(zone_fqdn, rr_fqdn, rr, true);

    {
        // Check that zone serial no is incremented
        auto z = db->getZone(zone_fqdn);
        EXPECT_EQ(z->soa().serial(), 1);
    }

    auto r = db->getRr(rr_fqdn);
    EXPECT_EQ(r.has_value(), true);
    EXPECT_EQ(r.value().a_size(), 1);
    EXPECT_EQ(r.value().a(0), "127.0.0.1");
    EXPECT_EQ(r.value().txt(), "testing");
}

TEST(writeRr, addExisting) {
    TmpDb db;
    string_view zone_fqdn{"example.com"};
    string_view rr_fqdn{"a.b.c.example.com"};

    Zone zone;
    EXPECT_NO_THROW(db->writeZone(zone_fqdn, zone, true));
    {
        auto z = db->getZone(zone_fqdn);
        EXPECT_EQ(z->soa().serial(), 0);
    }

    {
        Rr rr;
        rr.add_a("127.0.0.1");
        db->writeRr(zone_fqdn, rr_fqdn, rr, true);
    }

    {
        Rr rr;
        rr.add_a("127.0.0.2");
        EXPECT_ANY_THROW(db->writeRr(zone_fqdn, rr_fqdn, rr, true));
    }
}

TEST(writeRr, updateExisting) {
    TmpDb db;
    string_view zone_fqdn{"example.com"};
    string_view rr_fqdn{"a.b.c.example.com"};

    Zone zone;
    EXPECT_NO_THROW(db->writeZone(zone_fqdn, zone, true));
    {
        auto z = db->getZone(zone_fqdn);
        EXPECT_EQ(z->soa().serial(), 0);
    }

    {
        Rr rr;
        rr.add_a("127.0.0.1");
        rr.add_aa("0:0:0:0:0:0:0:1");
        rr.set_txt("testing");
        db->writeRr(zone_fqdn, rr_fqdn, rr, true);
    }

    {
        Rr rr;
        rr.add_a("127.0.0.2");
        rr.add_a("127.0.0.3");
        db->writeRr(zone_fqdn, rr_fqdn, rr, false);
    }

    auto r = db->getRr(rr_fqdn);
    EXPECT_EQ(r.has_value(), true);
    EXPECT_EQ(r.value().a_size(), 2);
    EXPECT_EQ(r.value().a(0), "127.0.0.2");
    EXPECT_EQ(r.value().a(1), "127.0.0.3");
    EXPECT_EQ(r.value().aa_size(), 1);
    EXPECT_EQ(r.value().aa(0), "0:0:0:0:0:0:0:1");
    EXPECT_EQ(r.value().txt(), "testing");
}

TEST(writeRr, replaceExisting) {
    TmpDb db;
    string_view zone_fqdn{"example.com"};
    string_view rr_fqdn{"a.b.c.example.com"};

    Zone zone;
    EXPECT_NO_THROW(db->writeZone(zone_fqdn, zone, true));
    {
        auto z = db->getZone(zone_fqdn);
        EXPECT_EQ(z->soa().serial(), 0);
    }

    {
        Rr rr;
        rr.add_a("127.0.0.1");
        rr.add_aa("0:0:0:0:0:0:0:1");
        rr.set_txt("testing");
        db->writeRr(zone_fqdn, rr_fqdn, rr, true);
    }

    {
        Rr rr;
        rr.add_a("127.0.0.2");
        rr.add_a("127.0.0.3");
        db->writeRr(zone_fqdn, rr_fqdn, rr, false, false);
    }

    auto r = db->getRr(rr_fqdn);
    EXPECT_EQ(r.has_value(), true);
    EXPECT_EQ(r.value().a_size(), 2);
    EXPECT_EQ(r.value().a(0), "127.0.0.2");
    EXPECT_EQ(r.value().a(1), "127.0.0.3");
    EXPECT_EQ(r.value().aa_size(), 0);
    EXPECT_EQ(r.value().txt(), "");
}

#endif // 0

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);

    logfault::LogManager::Instance().AddHandler(
                make_unique<logfault::StreamHandler>(clog, logfault::LogLevel::DEBUGGING));
    return RUN_ALL_TESTS();
}
