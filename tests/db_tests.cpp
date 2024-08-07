
#include "gtest/gtest.h"

#include "TmpDb.h"

#include "nsblast/DnsMessages.h"
#include "nsblast/errors.h"

using namespace std;
using namespace nsblast;
using namespace nsblast::lib;

TEST(DbWriteZone, newZone) {
    TmpDb db;
    {

        const string_view fqdn = "example.com";
        StorageBuilder sb;
        sb.createSoa(fqdn, 1000, "hostmaster.example.com", "ns1.example.com", 1,
                     1001, 1002, 1003, 1004);
        sb.finish();

        {
            auto tx = db->transaction();
            EXPECT_NO_THROW(tx->write({fqdn, key_class_t::ENTRY}, sb.buffer(), true));
            EXPECT_NO_THROW(tx->commit());
        }
    }
}

// Fails - does not throw
TEST(DbWriteZone, newZoneOnExisting) {
    TmpDb db;
    {
        const string_view fqdn = "example.com";
        StorageBuilder sb;
        sb.createSoa(fqdn, 1000, "hostmaster.example.com", "ns1.example.com", 1,
                     1001, 1002, 1003, 1004);
        sb.finish();

        {
            auto tx = db->transaction();
            EXPECT_FALSE(tx->keyExists({fqdn, key_class_t::ENTRY}));
            EXPECT_NO_THROW(tx->write({fqdn, key_class_t::ENTRY}, sb.buffer(), true));
            EXPECT_NO_THROW(tx->commit());
        }

        {
            auto tx = db->transaction();
            EXPECT_TRUE(tx->keyExists({fqdn, key_class_t::ENTRY}));
            EXPECT_THROW(tx->write({fqdn, key_class_t::ENTRY}, sb.buffer(), true), AlreadyExistException);
            tx->commit();
        }
    }
}

TEST(DbDeleteZone, existing) {
    TmpDb db;
    {
        const string_view fqdn = "example.com";
        StorageBuilder sb;
        sb.createSoa(fqdn, 1000, "hostmaster.example.com", "ns1.example.com", 1,
                     1001, 1002, 1003, 1004);
        sb.finish();

        {
            auto tx = db->transaction();
            EXPECT_NO_THROW(tx->write({fqdn, key_class_t::ENTRY}, sb.buffer(), true));
            EXPECT_NO_THROW(tx->commit());
        }
        {
            auto tx = db->transaction();
            EXPECT_NO_THROW(tx->remove({fqdn, key_class_t::ENTRY}));
            EXPECT_NO_THROW(tx->commit());
        }
    }
}

TEST(DbDeleteZone, existingRecursive) {
    TmpDb db;
    {
        const string_view fqdn = "example.com";
        StorageBuilder sb;
        sb.createSoa(fqdn, 1000, "hostmaster.example.com", "ns1.example.com", 1,
                     1001, 1002, 1003, 1004);
        sb.finish();

        {
            auto tx = db->transaction();
            EXPECT_NO_THROW(tx->write({fqdn, key_class_t::ENTRY}, sb.buffer(), true));
            EXPECT_NO_THROW(tx->commit());
        }
        {
            auto tx = db->transaction();
            EXPECT_NO_THROW(tx->remove({fqdn, key_class_t::ENTRY}, true));
            EXPECT_NO_THROW(tx->commit());
        }
    }
}

TEST(DbDeleteZone, nonexisting) {
    TmpDb db;
    {
        const string_view fqdn = "example.com";

        auto tx = db->transaction();
        EXPECT_NO_THROW(tx->remove({fqdn, key_class_t::ENTRY}));
        tx->commit();
    }
}

TEST(DbReadZone, exists) {
    TmpDb db;
    {
        const string_view fqdn = "example.com";
        StorageBuilder sb;
        sb.createSoa(fqdn, 1000, "hostmaster.example.com", "ns1.example.com", 1,
                     1001, 1002, 1003, 1004);
        sb.finish();

        {
            auto tx = db->transaction();
            EXPECT_NO_THROW(tx->write({fqdn, key_class_t::ENTRY}, sb.buffer(), true));
            EXPECT_NO_THROW(tx->commit());
        }

        {
            auto tx = db->transaction();
            EXPECT_TRUE(tx->keyExists({fqdn, key_class_t::ENTRY}));
            EXPECT_TRUE(tx->zoneExists(fqdn));
        }
    }
}

TEST(DbReadZone, notExists) {
    TmpDb db;
    {
        const string_view fqdn = "example.com";

        auto tx = db->transaction();
        EXPECT_FALSE(tx->zoneExists(fqdn));
    }
}

TEST(DbReadZone, read) {
    TmpDb db;
    {
        const string_view fqdn = "example.com";
        StorageBuilder sb;
        sb.createSoa(fqdn, 1000, "hostmaster.example.com", "ns1.example.com", 1,
                     1001, 1002, 1003, 1004);
        sb.finish();

        {
            auto tx = db->transaction();
            EXPECT_NO_THROW(tx->write({fqdn, key_class_t::ENTRY}, sb.buffer(), true));
            EXPECT_NO_THROW(tx->commit());
        }

        {
            auto tx = db->transaction();
            auto b = tx->read({fqdn, key_class_t::ENTRY});
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
}

TEST(DbReadZone, readNoExist) {
    TmpDb db;
    {
        const string_view fqdn = "example.com";

        {
            auto tx = db->transaction();
            EXPECT_THROW(tx->read({fqdn, key_class_t::ENTRY}), NotFoundException);
        }
    }
}

TEST(DbLookup, ok) {
    TmpDb db;
    {
        // Setup
        const string_view fqdn = "example.com";
        StorageBuilder sb;
        sb.createSoa(fqdn, 1000, "hostmaster.example.com", "ns1.example.com", 1,
                     1001, 1002, 1003, 1004);
        sb.createNs(fqdn, 1000, "ns1.example.com");
        sb.createNs(fqdn, 1000, "ns2.example.com");
        sb.finish();

        {
            auto tx = db->transaction();
            tx->write({fqdn, key_class_t::ENTRY}, sb.buffer(), true);
            tx->commit();
        }

        // Test
        auto tx = db->transaction();
        auto entry = tx->lookup(fqdn);

        EXPECT_TRUE(entry);
        EXPECT_FALSE(entry.empty());
        EXPECT_FALSE(entry.begin() == entry.end());
        EXPECT_EQ(entry.begin()->type(), TYPE_SOA);
        EXPECT_EQ(entry.begin()->labels().string(), fqdn);
    }
}

TEST(DbLookup, notFound) {
    TmpDb db;
    {
        // Setup
        const string_view fqdn = "example.com";
        StorageBuilder sb;
        sb.createSoa(fqdn, 1000, "hostmaster.example.com", "ns1.example.com", 1,
                     1001, 1002, 1003, 1004);
        sb.createNs(fqdn, 1000, "ns1.example.com");
        sb.createNs(fqdn, 1000, "ns2.example.com");
        sb.finish();

        {
            auto tx = db->transaction();
            tx->write({fqdn, key_class_t::ENTRY}, sb.buffer(), true);
            tx->commit();
        }

        // Test
        auto tx = db->transaction();
        auto entry = tx->lookup("ns1.example.com");

        EXPECT_TRUE(entry.empty());
        EXPECT_TRUE(entry.begin() == entry.end());
        EXPECT_FALSE(entry);
    }
}

TEST(lookupEntryAndSoa, sameOk) {
    TmpDb db;

    {
        // Setup
        const string_view fqdn = "example.com";
        {
            StorageBuilder sb;
            sb.createSoa(fqdn, 1000, "hostmaster.example.com", "ns1.example.com", 1,
                         1001, 1002, 1003, 1004);
            sb.createNs(fqdn, 1000, "ns1.example.com");
            sb.createNs(fqdn, 1000, "ns2.example.com");
            sb.finish();

            auto tx = db->transaction();
            tx->write({fqdn, key_class_t::ENTRY}, sb.buffer(), true);
            tx->commit();
        }

        // Test
        auto tx = db->transaction();
        auto entry = tx->lookupEntryAndSoa(fqdn);

        EXPECT_TRUE(entry);
        EXPECT_FALSE(entry.empty());
        EXPECT_FALSE(entry.soa().begin() == entry.soa().end());
        EXPECT_FALSE(entry.rr().begin() == entry.rr().end());
        EXPECT_TRUE(entry.isSame());

        EXPECT_EQ(entry.soa().begin()->labels().string(), fqdn);
        EXPECT_EQ(entry.soa().begin()->type(), TYPE_SOA);
        EXPECT_EQ(entry.rr().begin()->type(), TYPE_SOA);
        EXPECT_EQ(entry.rr().begin()->labels().string(), fqdn);
    }
}

TEST(lookupEntryAndSoa, notSameOk) {
    TmpDb db;

    {
        // Setup
        const string_view fqdn = "example.com";
        const string_view www = "www.example.com";
        {
            StorageBuilder sb;
            sb.createSoa(fqdn, 1000, "hostmaster.example.com", "ns1.example.com", 1,
                         1001, 1002, 1003, 1004);
            sb.createNs(fqdn, 1000, "ns1.example.com");
            sb.createNs(fqdn, 1000, "ns2.example.com");
            sb.finish();

            auto tx = db->transaction();
            tx->write({fqdn, key_class_t::ENTRY}, sb.buffer(), true);
            tx->commit();
        }

        {
            StorageBuilder sb;
            sb.createA(www, 1000, string_view{"127.0.0.1"});
            sb.setZoneLen(fqdn.size());
            sb.finish();

            auto tx = db->transaction();
            tx->write({www, key_class_t::ENTRY}, sb.buffer(), true);
            tx->commit();
        }

        // Test
        auto tx = db->transaction();
        auto entry = tx->lookupEntryAndSoa(www);

        EXPECT_TRUE(entry);
        EXPECT_FALSE(entry.empty());
        EXPECT_FALSE(entry.soa().begin() == entry.soa().end());
        EXPECT_FALSE(entry.rr().begin() == entry.rr().end());
        EXPECT_FALSE(entry.isSame());

        EXPECT_EQ(entry.soa().count(), 3);
        EXPECT_EQ(entry.soa().begin()->labels().string(), fqdn);
        EXPECT_EQ(entry.soa().begin()->type(), TYPE_SOA);
        EXPECT_EQ(entry.rr().count(), 1);
        EXPECT_EQ(entry.rr().begin()->labels().string(), www);
        EXPECT_EQ(entry.rr().begin()->type(), TYPE_A);
    }
}

TEST(lookupEntryAndSoa, noRrOk) {
    TmpDb db;

    {
        // Setup
        const string_view fqdn = "example.com";
        const string_view www = "www.example.com";
        {
            StorageBuilder sb;
            sb.createSoa(fqdn, 1000, "hostmaster.example.com", "ns1.example.com", 1,
                         1001, 1002, 1003, 1004);
            sb.createNs(fqdn, 1000, "ns1.example.com");
            sb.createNs(fqdn, 1000, "ns2.example.com");
            sb.finish();

            auto tx = db->transaction();
            tx->write({fqdn, key_class_t::ENTRY}, sb.buffer(), true);
            tx->commit();
        }

        // Test
        auto tx = db->transaction();
        auto entry = tx->lookupEntryAndSoa(www);

        EXPECT_TRUE(entry);
        EXPECT_FALSE(entry.empty());
        EXPECT_FALSE(entry.soa().begin() == entry.soa().end());
        EXPECT_TRUE(entry.rr().begin() == entry.rr().end());
        EXPECT_FALSE(entry.isSame());

        EXPECT_EQ(entry.soa().count(), 3);
        EXPECT_EQ(entry.soa().begin()->labels().string(), fqdn);
        EXPECT_EQ(entry.soa().begin()->type(), TYPE_SOA);
        EXPECT_EQ(entry.rr().count(), 0);
        EXPECT_TRUE(entry.hasSoa());
        EXPECT_FALSE(entry.hasRr());
    }
}

TEST(Rocksdb, canReload) {
    TmpDb db;

    EXPECT_TRUE(db->wasBootstrapped());

    const string_view fqdn = "example.com";
    StorageBuilder sb;
    sb.createSoa(fqdn, 1000, "hostmaster.example.com", "ns1.example.com", 1,
                 1001, 1002, 1003, 1004);
    sb.finish();

    {
        auto tx = db->transaction();
        EXPECT_FALSE(tx->keyExists({fqdn, key_class_t::ENTRY}));
        EXPECT_NO_THROW(tx->write({fqdn, key_class_t::ENTRY}, sb.buffer(), true));
        EXPECT_NO_THROW(tx->commit());
    }

    {
        auto tx = db->transaction();
        EXPECT_TRUE(tx->keyExists({fqdn, key_class_t::ENTRY}));
    }

    EXPECT_NO_THROW(db.reload());
    EXPECT_FALSE(db->wasBootstrapped());

    {
        auto tx = db->transaction();
        EXPECT_TRUE(tx->keyExists({fqdn, key_class_t::ENTRY}));
        EXPECT_TRUE(db->currentTrxId() > 0);
    }
}

TEST(RocksdbBackup, backup) {
    TmpDb db;

    auto backup_path = db.path() /= "backup";

    db.createTestZone();
    EXPECT_NO_THROW(db->backup(backup_path));
}

TEST(RocksdbBackup, listBackups) {
    TmpDb db;

    auto backup_path = db.path() /= "backup";

    db.createTestZone();
    EXPECT_NO_THROW(db->backup(backup_path));
    db.createWwwA();
    EXPECT_NO_THROW(db->backup(backup_path));

    boost::json::object meta;
    db->listBackups(meta, backup_path);

    EXPECT_EQ(meta["backups"].as_array().size(), 2);
    EXPECT_EQ(meta["backups"].as_array()[0].as_object()["id"], 1);
    EXPECT_EQ(meta["backups"].as_array()[1].as_object()["id"], 2);
}

TEST(RocksdbBackup, verifyBackups) {
    TmpDb db;

    auto backup_path = db.path() /= "backup";

    db.createTestZone();
    EXPECT_NO_THROW(db->backup(backup_path));
    db.createWwwA();
    EXPECT_TRUE(db->verifyBackup(1, backup_path));

    EXPECT_NO_THROW(db->backup(backup_path));

    EXPECT_TRUE(db->verifyBackup(1, backup_path));
    EXPECT_TRUE(db->verifyBackup(2, backup_path));
    EXPECT_FALSE(db->verifyBackup(3, backup_path));
}

TEST(RocksdbBackup, restore) {
    TmpDb db;

    const auto backup_path = db.path() /= "backup";
    const std::string_view zone = "example.com";
    const std::string_view www = "www.example.com";

    db.createTestZone();
    {
        auto trx = db->dbTransaction();
        EXPECT_TRUE(trx->keyExists({zone, key_class_t::ENTRY}));
    }
    EXPECT_NO_THROW(db->backup(backup_path)); // #1
    db.createWwwA();
    {
        auto trx = db->dbTransaction();
        EXPECT_TRUE(trx->keyExists({zone, key_class_t::ENTRY}));
        EXPECT_TRUE(trx->keyExists({www, key_class_t::ENTRY}));
    }
    EXPECT_NO_THROW(db->backup(backup_path)); // #2

    {
        auto trx = db->dbTransaction();
        EXPECT_TRUE(trx->keyExists({zone, key_class_t::ENTRY}));
        EXPECT_TRUE(trx->keyExists({www, key_class_t::ENTRY}));
    }

    db->close();
    db->restoreBackup(1, backup_path);
    db->init();

    {
        auto trx = db->dbTransaction();
        EXPECT_TRUE(trx->keyExists({zone, key_class_t::ENTRY}));
        EXPECT_FALSE(trx->keyExists({www, key_class_t::ENTRY}));
    }

    db->close();
    db->restoreBackup(2, backup_path);
    db->init();

    {
        auto trx = db->dbTransaction();
        EXPECT_TRUE(trx->keyExists({zone, key_class_t::ENTRY}));
        EXPECT_TRUE(trx->keyExists({www, key_class_t::ENTRY}));
    }
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);

    logfault::LogManager::Instance().AddHandler(
                make_unique<logfault::StreamHandler>(clog, logfault::LogLevel::TRACE));
    return RUN_ALL_TESTS();
}
