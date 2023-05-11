#include "gtest/gtest.h"

#include "TmpDb.h"

#include "nsblast/errors.h"
#include "proto/nsblast.pb.h"
#include "proto_util.h"
#include "test_res.h"

using namespace std;
using namespace nsblast;
using namespace nsblast::lib;


TEST(AuthMgr, createAndGetTenant) {

    MockServer ms;
    {
        pb::Tenant tenant;
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

TEST(AuthMgr, upsertTenantNoId) {

    MockServer ms;
    {
        pb::Tenant tenant;
        EXPECT_THROW(ms.auth().upsertTenant(tenant, false), nsblast::MissingIdException);
    }
}

TEST(AuthMgr, replaceTenant) {

    MockServer ms;
    {
        pb::Tenant tenant;
        auto id = ms.auth().createTenant(tenant);

        tenant.Clear();
        tenant.set_id(id);
        tenant.set_active(false);
        ms.auth().upsertTenant(tenant, false);

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

        tenant.Clear();
        tenant.set_id(id);
        {
            auto p = tenant.add_properties();
            p->set_key("kind");
            p->set_value("Dog");
        }
        ms.auth().upsertTenant(tenant, true);

        auto nt = ms.auth().getTenant(id);
        EXPECT_TRUE(nt);
        if (nt) {
            EXPECT_EQ(id, nt->id());
            EXPECT_TRUE(nt->has_active());
            EXPECT_FALSE(nt->active());
            EXPECT_EQ(nt->properties_size(), 1);
            EXPECT_EQ(nt->properties(0).key(), "kind");
            EXPECT_EQ(nt->properties(0).value(), "Dog");
        }
    }
}

TEST(AuthMgr, deleteTenant) {

    MockServer ms;
    {
        pb::Tenant tenant;
        auto id = ms.auth().createTenant(tenant);
        auto nt = ms.auth().getTenant(id);
        EXPECT_TRUE(nt);
        ms.auth().deleteTenant(id);
        nt = ms.auth().getTenant(id);
        EXPECT_FALSE(nt);
    }
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);

    logfault::LogManager::Instance().AddHandler(
                make_unique<logfault::StreamHandler>(clog, logfault::LogLevel::INFO));
    return RUN_ALL_TESTS();
}
