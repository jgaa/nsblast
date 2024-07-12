
#include <filesystem>

#include "gtest/gtest.h"

#include "TmpDb.h"

#include "nsblast/errors.h"
#include "proto/nsblast.pb.h"
#include "proto_util.h"

using namespace std;
using namespace nsblast;
using namespace nsblast::lib;


TEST(AuthMgr, createAndGetTenant) {

    MockServer ms;
    {
        pb::Tenant tenant;
        tenant.set_root("example.com");
        auto id = ms.auth().createTenant(tenant);
        auto nt = ms.auth().getTenant(id);
        EXPECT_TRUE(nt);
        if (nt) {
            EXPECT_EQ(id, nt->id());
            EXPECT_TRUE(PB_GET(nt.value(), active, true));
        }
    }
}

TEST(AuthMgr, getNoTenant) {

    MockServer ms;
    {
        auto nt = ms.auth().getTenant("baad");
        EXPECT_FALSE(nt);
    }
}

TEST(AuthMgr, upsertConstraintId) {

    MockServer ms;
    {
        pb::Tenant tenant;
        tenant.set_id("e0147650-676f-11ee-bd36-d71ea8293c52");
        tenant.set_root("example.com");
        EXPECT_THROW(ms.auth().upsertTenant("5fa48ac2-6770-11ee-b55f-d37f23f3956d", tenant, false), nsblast::ConstraintException);
    }
}

TEST(AuthMgr, replaceTenant) {

    MockServer ms;
    {
        pb::Tenant tenant;
        tenant.set_root("example.com");
        auto id = ms.auth().createTenant(tenant);

        tenant.Clear();
        tenant.set_id(id);
        tenant.set_active(false);
        ms.auth().upsertTenant(id, tenant, false);

        auto nt = ms.auth().getTenant(id);
        EXPECT_TRUE(nt);
        if (nt) {
            EXPECT_EQ(id, nt->id());
            EXPECT_TRUE(nt->has_active());
            EXPECT_FALSE(nt->active());
        }
    }
}

TEST(AuthMgr, mergeTenant) {

    // Keep inactive state from the original tenant.

    MockServer ms;
    {
        pb::Tenant tenant;
        tenant.set_root("example.com");
        tenant.set_active(false);

        {
            auto p = tenant.add_properties();
            p->set_key("kind");
            p->set_value("Cat");

            p = tenant.add_properties();
            p->set_key("kind");
            p->set_value("Horse");
        }

        auto id = ms.auth().createTenant(tenant);

        pb::Tenant tenant2;
        tenant2.set_id(id);
        {
            auto p = tenant2.add_properties();
            p->set_key("kind");
            p->set_value("Dog");
        }
        ms.auth().upsertTenant(id, tenant2, true);

        auto nt = ms.auth().getTenant(id);
        EXPECT_TRUE(nt);
        if (nt) {
            EXPECT_EQ(id, nt->id());
            EXPECT_TRUE(nt->has_active());
            EXPECT_FALSE(nt->active());
            EXPECT_EQ(nt->properties_size(), 3);
            EXPECT_EQ(nt->properties(0).key(), "kind");
            EXPECT_EQ(nt->properties(0).value(), "Cat");
            EXPECT_EQ(nt->properties(1).key(), "kind");
            EXPECT_EQ(nt->properties(1).value(), "Horse");
            EXPECT_EQ(nt->properties(2).key(), "kind");
            EXPECT_EQ(nt->properties(2).value(), "Dog");
        }
    }
}

TEST(AuthMgr, deleteTenant) {

    MockServer ms;
    {
        pb::Tenant tenant;
        tenant.set_root("example.com");
        auto id = ms.auth().createTenant(tenant);
        auto nt = ms.auth().getTenant(id);
        EXPECT_TRUE(nt);
        ms.auth().deleteTenant(id);
        nt = ms.auth().getTenant(id);
        EXPECT_FALSE(nt);
    }
}

TEST(AuthMgr, createZone) {

    string tname = "ares";
    string fqdn = "example.com";
    MockServer ms;
    {
        pb::Tenant tenant;
        tenant.set_root(fqdn);
        tenant.set_id(tname);
        auto id = ms.auth().createTenant(tenant);
        auto nt = ms.auth().getTenant(id);
        EXPECT_TRUE(nt);
        EXPECT_EQ(nt->id(), tname);

        auto trx = ms->resource().transaction();

        ms.auth().addZone(*trx, fqdn, tname);

        ResourceIf::RealKey key_zone{fqdn, ResourceIf::RealKey::Class::ZONE};
        ResourceIf::RealKey key_tzone{tname, fqdn, ResourceIf::RealKey::Class::TZONE};
        EXPECT_TRUE(trx->keyExists(key_zone, ResourceIf::Category::ACCOUNT));
        EXPECT_TRUE(trx->keyExists(key_tzone, ResourceIf::Category::ACCOUNT));
    }
}


TEST(AuthMgr, deleteZone) {

    string tname = "ares";
    string fqdn = "example.com";
    MockServer ms;
    {
        pb::Tenant tenant;
        tenant.set_root(fqdn);
        tenant.set_id(tname);
        auto id = ms.auth().createTenant(tenant);
        auto trx = ms->resource().transaction();
        ms.auth().addZone(*trx, fqdn, tname);
        ms.auth().deleteZone(*trx, fqdn, tname);


        ResourceIf::RealKey key_zone{fqdn, ResourceIf::RealKey::Class::ZONE};
        ResourceIf::RealKey key_tzone{tname, fqdn, ResourceIf::RealKey::Class::TZONE};
        EXPECT_FALSE(trx->keyExists(key_zone, ResourceIf::Category::ACCOUNT));
        EXPECT_FALSE(trx->keyExists(key_tzone, ResourceIf::Category::ACCOUNT));
    }
}

TEST(AuthMgr, bootstrap) {
    MockServer ms;
    //ms.auth().bootstrap();
    string admin = "admin";

    auto trx = ms->resource().transaction();

    const ResourceIf::RealKey key{admin, ResourceIf::RealKey::Class::USER};
    EXPECT_TRUE(trx->keyExists(key, ResourceIf::Category::ACCOUNT));

    filesystem::path pwd_file_path = ms->config().db_path;
    pwd_file_path /= "password.txt";
    EXPECT_TRUE(filesystem::is_regular_file(pwd_file_path));
    EXPECT_EQ(filesystem::file_size(pwd_file_path), 42);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);

    logfault::LogManager::Instance().AddHandler(
                make_unique<logfault::StreamHandler>(clog, logfault::LogLevel::INFO));
    return RUN_ALL_TESTS();
}
