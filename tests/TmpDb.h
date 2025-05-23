#pragma once

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/random_generator.hpp>
#include <boost/lexical_cast.hpp>

#include <filesystem>
#include "nsblast/logging.h"
#include "RocksDbResource.h"
#include "AuthMgr.h"
#include "nsblast/Server.h"

//#include "test_res.h"

using ROCKSDB_NAMESPACE::DB;
using ROCKSDB_NAMESPACE::Options;
using ROCKSDB_NAMESPACE::PinnableSlice;
using ROCKSDB_NAMESPACE::ReadOptions;
using ROCKSDB_NAMESPACE::Status;
using ROCKSDB_NAMESPACE::WriteBatch;
using ROCKSDB_NAMESPACE::WriteOptions;
using ROCKSDB_NAMESPACE::Slice;

namespace {

using namespace std;
using namespace nsblast;
using namespace nsblast::lib;

string getUuid() {
    static boost::uuids::random_generator uuid_gen_;
    return boost::lexical_cast<string>(uuid_gen_());
}

class TmpDb {
public:
    TmpDb(bool enableAuth = false)
        : path_{filesystem::temp_directory_path() /= getUuid()}
        , c_{false, path_}
        , db_{make_shared<RocksDbResource>(c_)}
    {
        filesystem::create_directories(path_);
        LOG_TRACE << "Created unique tmp directory: " << path_;
        db_->init();
        c_.dns_enable_notify = false;
        c_.enable_auth = enableAuth;
    }

    auto& operator -> () {
        return db_;
    }

    RocksDbResource& operator * () {
        return *db_;
    }

    ~TmpDb() {
        filesystem::remove_all(path_);
    }

    const auto& config() const {
        return c_;
    }

    auto& config() {
        return c_;
    }

    ResourceIf& resource() {
        return *db_;
    }

    auto& resourcePtr() {
        return db_;
    }

    void reload() {
        db_.reset();
        db_ = make_shared<RocksDbResource>(c_);
        db_->init();
    }

    auto path() {
        return path_;
    }

    void createTestZone(const std::string zone = "example.com",
                        const boost::uuids::uuid tid = nsblast::lib::nsblastTenantUuid) {
        StorageBuilder sb;
        string fqdn = zone;
        string nsname = "ns1."s + zone;
        string rname = "hostmaster."s + zone;
        string mxname = "mail.example."s + zone;
        auto ip1 = boost::asio::ip::make_address_v4("127.0.0.1");
        auto ip2 = boost::asio::ip::make_address_v4("127.0.0.2");
        auto ip3 = boost::asio::ip::make_address_v6("2001:0db8:85a3:0000:0000:8a2e:0370:7334");
        auto ip4 = boost::asio::ip::make_address_v6("2000:0db8:85a3:0000:0000:8a2e:0370:7335");

        // Notice order. Sorting the index must work to iterate in the expected order below
        sb.setTenantId(tid);
        sb.createA(fqdn, 1000, ip1);
        sb.createA(fqdn, 1000, ip3);
        sb.createA(fqdn, 1000, ip2);
        sb.createA(fqdn, 1000, ip4);
        sb.createNs(fqdn, 1000, "ns1."s + zone);
        sb.createNs(fqdn, 1000, "ns2."s + zone);
        sb.createNs(fqdn, 1000, "ns3."s + zone);
        sb.createNs(fqdn, 1000, "ns4."s + zone);
        sb.createSoa(fqdn, 5003, nsname, rname, 1000, 1001, 1002, 1003, 1004);
        sb.createMx(fqdn, 9999, 10, mxname);
        sb.finish();

        auto tx = resource().transaction();
        tx->write({fqdn, key_class_t::ENTRY}, sb.buffer(), true);
        tx->commit();
    }

    void createWwwA() {
        StorageBuilder sb;
        string_view fqdn = "www.example.com";
        auto ip1 = boost::asio::ip::make_address_v4("127.0.0.3");
        auto ip2 = boost::asio::ip::make_address_v4("127.0.0.4");
        auto ip3 = boost::asio::ip::make_address_v6("2003:0db8:85a3:0000:0000:8a2e:0370:7334");
        auto ip4 = boost::asio::ip::make_address_v6("2004:0db8:85a3:0000:0000:8a2e:0370:7335");

        // Notice order. Sorting the index must work to iterate in the expected order below
        sb.createA(fqdn, 1000, ip1);
        sb.createA(fqdn, 1000, ip3);
        sb.createA(fqdn, 1000, ip2);
        sb.createA(fqdn, 1000, ip4);
        sb.setZoneLen(fqdn.size() - 4);
        sb.finish();

        auto tx = resource().transaction();
        tx->write({fqdn, key_class_t::ENTRY}, sb.buffer(), true);
        tx->commit();
    }

    void createFooWithHinfo() {
        StorageBuilder sb;
        string_view fqdn = "foo.example.com";

        // Notice order. Sorting the index must work to iterate in the expected order below
        sb.createHinfo(fqdn, 1000, "awesome", "minix");
        sb.finish();

        auto tx = resource().transaction();
        tx->write({fqdn, key_class_t::ENTRY}, sb.buffer(), true);
        tx->commit();
    }

private:
    filesystem::path path_;
    Config c_;
    shared_ptr<RocksDbResource> db_;
};

class MockServer : public Server {
public:
    MockServer(std::shared_ptr<TmpDb> db = make_shared<TmpDb>())
        : Server(db->config()), db_{db} {
        resource_ = db->resourcePtr();
        auth_ = make_shared<AuthMgr>(*this);
        auth_->bootstrap();
        admin_session_ = make_shared<Session>(*auth_);
    }

    const std::string default_role_name = "default";

    auto& operator -> () {
        return db_;
    }

    auto& getAdminSession() {
        return admin_session_;
    }

    auto getAuthAs(std::string_view user, std::string_view passwd) {
        return auth_->login(user, passwd);
    }

    template <typename PermissionsT = std::initializer_list<std::string>>
    std::string createTenant(std::string tenantId, std::string_view userName, std::string_view passwd
                             , std::function<void(boost::json::object&)> patcher
                             , const PermissionsT& permissions = {"USE_API"}) {

        string user_name;

        if (!tenantId.empty()) {
            tenantId = newUuidStr();
        }

        if (userName.empty()) {
            user_name = std::format("user@{}", tenantId);
        } else {
            user_name = string{userName};
        }

        if (passwd.empty()) {
            passwd = "very$ecret123";
        }

        pb::Tenant tenant;
        tenant.set_id(tenantId);

        {

            auto role = tenant.add_roles();
            role->set_name(default_role_name);

            // Add all defined permissions to allowedPermissions and the "default" role.
            for(const auto& name : permissions) {
                pb::Permission perm = {};
                if (pb::Permission_Parse(name, &perm)) {
                    tenant.add_allowedpermissions(perm);
                    role->add_permissions(perm);
                }
            }

            auto user = tenant.add_users();
            user->set_id(newUuidStr());
            user->set_name(user_name);
            auto auth = user->mutable_auth();
            auth->set_password(std::string{passwd});
            user->set_active(true);
            user->add_roles(default_role_name);
        }

        auth_->createTenant(tenant);

        return user_name;
    }

private:
    std::shared_ptr<TmpDb> db_;
    std::shared_ptr<Session> admin_session_;
};

} // ns
