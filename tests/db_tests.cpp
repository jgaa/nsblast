#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/random_generator.hpp>
#include <boost/lexical_cast.hpp>

#include "gtest/gtest.h"
#include "RestApi.h"
#include "Db.h"

#include "test_res.h"

using namespace std;
using namespace nsblast;
using namespace nsblast::lib;

using ROCKSDB_NAMESPACE::DB;
using ROCKSDB_NAMESPACE::Options;
using ROCKSDB_NAMESPACE::PinnableSlice;
using ROCKSDB_NAMESPACE::ReadOptions;
using ROCKSDB_NAMESPACE::Status;
using ROCKSDB_NAMESPACE::WriteBatch;
using ROCKSDB_NAMESPACE::WriteOptions;
using ROCKSDB_NAMESPACE::Slice;


namespace {

string getUuid() {
    static boost::uuids::random_generator uuid_gen_;
    return boost::lexical_cast<string>(uuid_gen_());
}



class TmpDb {
public:
    TmpDb()
        : path_{filesystem::temp_directory_path() /= getUuid()}
        , c_{path_}
        , db_{c_}
    {
        filesystem::create_directories(path_);
        LOG_TRACE << "Created unique tmp directory: " << path_;
        db_.init();
    }

    auto operator -> () {
        return &db_;
    }

    ~TmpDb() {
        filesystem::remove_all(path_);
    }

private:
    filesystem::path path_;
    Config c_;
    Db db_;
};

} // ns

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


int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
