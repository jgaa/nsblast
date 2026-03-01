
#include <any>
#include <fstream>
#include <filesystem>
#include <format>
#include <ranges>

#include "gtest/gtest.h"

#include "TmpDb.h"

#include "nsblast/errors.h"
#include "nsblast/util.h"
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

TEST(AuthMgr, migrateStorageWritesVersionKey) {
    MockServer ms;

    const std::string key_name = "data_schema_version";
    const ResourceIf::RealKey version_key{key_name, ResourceIf::RealKey::Class::META};
    string raw;

    {
        auto trx = ms->resource().transaction();
        EXPECT_FALSE(trx->read(version_key, raw, ResourceIf::Category::ACCOUNT, false));
    }

    ms.auth().migrateStorage();

    {
        auto trx = ms->resource().transaction();
        EXPECT_TRUE(trx->read(version_key, raw, ResourceIf::Category::ACCOUNT, false));
        ASSERT_EQ(raw.size(), sizeof(uint32_t));
        EXPECT_EQ(get32bValueAt(raw, 0), CURRENT_DATA_SCHEMA_VERSION);
    }
}

TEST(AuthMgr, migrateStorageBackfillsDynipPermission) {
    MockServer ms;

    pb::Tenant tenant;
    tenant.set_id(newUuidStr());
    tenant.set_name(std::format("legacy-{}", tenant.id()));
    tenant.set_active(true);
    tenant.set_root("");
    tenant.add_allowedpermissions(pb::Permission::USE_API);

    auto* role = tenant.add_roles();
    role->set_name("legacy-role");
    role->add_permissions(pb::Permission::USE_API);

    auto* user = tenant.add_users();
    user->set_id(newUuidStr());
    user->set_name(std::format("legacy-user-{}", tenant.id()));
    user->set_active(true);
    user->add_roles(role->name());
    auto* auth = user->mutable_auth();
    auth->set_password("secret");

    const auto tenant_id = ms.auth().createTenant(tenant);

    auto before = ms.auth().getTenant(tenant_id);
    ASSERT_TRUE(before);
    EXPECT_TRUE(std::ranges::find(before->allowedpermissions(), pb::Permission::DYNIP)
                == before->allowedpermissions().end());

    ms.auth().migrateStorage();

    auto after = ms.auth().getTenant(tenant_id);
    ASSERT_TRUE(after);
    EXPECT_TRUE(std::ranges::find(after->allowedpermissions(), pb::Permission::DYNIP)
                != after->allowedpermissions().end());
    EXPECT_EQ(ms.auth().dataSchemaVersion(), CURRENT_DATA_SCHEMA_VERSION);
}

TEST(AuthMgr, ensureAdminTenantRoleConsistencyPreservesAdministratorRoleAndAdminAssignment) {
    MockServer ms;
    static constexpr std::string_view admin_user_name = "admin";
    static constexpr std::string_view admin_role_name = "Administrator";

    const auto system_tenant_id = boost::uuids::to_string(nsblast::lib::nsblastTenantUuid);
    auto tenant = ms.auth().getTenant(system_tenant_id);
    ASSERT_TRUE(tenant);

    for (auto it = tenant->mutable_allowedpermissions()->begin();
         it != tenant->mutable_allowedpermissions()->end();) {
        if (*it == pb::Permission::DYNIP) {
            it = tenant->mutable_allowedpermissions()->erase(it);
        } else {
            ++it;
        }
    }

    auto* administrator_role = [&]() -> pb::Role* {
        for (auto& role : *tenant->mutable_roles()) {
            if (compareCaseInsensitive(role.name(), admin_role_name)) {
                return &role;
            }
        }
        return nullptr;
    }();
    ASSERT_TRUE(administrator_role);
    administrator_role->clear_permissions();
    administrator_role->add_permissions(pb::Permission::USE_API);
    administrator_role->mutable_filter()->set_fqdn("example.com");
    administrator_role->mutable_filter()->set_recursive(false);

    auto* admin_user = [&]() -> pb::User* {
        for (auto& user : *tenant->mutable_users()) {
            if (compareCaseInsensitive(user.name(), admin_user_name)) {
                return &user;
            }
        }
        return nullptr;
    }();

    ASSERT_TRUE(admin_user);
    admin_user->clear_roles();
    admin_user->add_roles("administrator");
    admin_user->add_roles("ADMINISTRATOR");

    ms.auth().upsertTenant(tenant->id(), *tenant, false);
    EXPECT_TRUE(ms.auth().ensureAdminTenantRoleConsistency(true));

    auto repaired = ms.auth().getTenant(system_tenant_id);
    ASSERT_TRUE(repaired);

    EXPECT_TRUE(std::ranges::find(repaired->allowedpermissions(), pb::Permission::DYNIP)
                != repaired->allowedpermissions().end());

    const auto* repaired_admin_role = [&]() -> const pb::Role* {
        for (const auto& role : repaired->roles()) {
            if (compareCaseInsensitive(role.name(), admin_role_name)) {
                return &role;
            }
        }
        return nullptr;
    }();
    ASSERT_TRUE(repaired_admin_role);
    EXPECT_EQ(repaired_admin_role->name(), admin_role_name);
    for (int i = pb::Permission_MIN; i <= pb::Permission_MAX; ++i) {
        if (!pb::Permission_IsValid(i)) {
            continue;
        }
        EXPECT_TRUE(std::ranges::find(repaired_admin_role->permissions(),
                                      static_cast<pb::Permission>(i))
                    != repaired_admin_role->permissions().end());
    }
    EXPECT_EQ(PB_GET(repaired_admin_role->filter(), fqdn, ""), "");
    EXPECT_TRUE(PB_GET(repaired_admin_role->filter(), recursive, true));
    EXPECT_EQ(PB_GET(repaired_admin_role->filter(), regex, ""), "");

    const auto* repaired_admin_user = [&]() -> const pb::User* {
        for (const auto& user : repaired->users()) {
            if (compareCaseInsensitive(user.name(), admin_user_name)) {
                return &user;
            }
        }
        return nullptr;
    }();
    ASSERT_TRUE(repaired_admin_user);
    EXPECT_EQ(repaired_admin_user->roles().size(), 1);
    EXPECT_EQ(repaired_admin_user->roles(0), admin_role_name);

    filesystem::path pwd_file_path = ms->config().db_path;
    pwd_file_path /= "password.txt";
    ifstream in{pwd_file_path};
    string passwd;
    in >> passwd;
    ASSERT_FALSE(passwd.empty());

    const auto auth = ms.getAuthAs(admin_user_name, passwd);
    ASSERT_TRUE(auth.access);
    const auto session = any_cast<std::shared_ptr<Session>>(auth.extra);
    ASSERT_TRUE(session);
    EXPECT_TRUE(session->isAllowed(pb::Permission::LIST_ROLES));
    EXPECT_TRUE(session->isAllowed(pb::Permission::GET_SELF_USER));
    EXPECT_TRUE(session->isAllowed(pb::Permission::SHOW_LOG));
    EXPECT_TRUE(session->isAllowed(pb::Permission::VARS_LIST));
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);

    logfault::LogManager::Instance().AddHandler(
                make_unique<logfault::StreamHandler>(clog, logfault::LogLevel::INFO));
    return RUN_ALL_TESTS();
}
