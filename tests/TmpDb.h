#pragma once

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/random_generator.hpp>
#include <boost/lexical_cast.hpp>

#include <filesystem>
#include "nsblast/logging.h"
#include "RocksDbResource.h"

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
    TmpDb()
        : path_{filesystem::temp_directory_path() /= getUuid()}
        , c_{false, path_}
        , db_{c_}
    {
        filesystem::create_directories(path_);
        LOG_TRACE << "Created unique tmp directory: " << path_;
        db_.init();
    }

    auto operator -> () {
        return &db_;
    }

    RocksDbResource& operator * () {
        return db_;
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
        return db_;
    }

    void createTestZone() {
        StorageBuilder sb;
        string_view fqdn = "example.com";
        string_view nsname = "ns1.example.com";
        string_view rname = "hostmaster@example.com";
        string_view mxname = "mail.example.com";
        auto ip1 = boost::asio::ip::address_v4::from_string("127.0.0.1");
        auto ip2 = boost::asio::ip::address_v4::from_string("127.0.0.2");
        auto ip3 = boost::asio::ip::address_v6::from_string("2001:0db8:85a3:0000:0000:8a2e:0370:7334");
        auto ip4 = boost::asio::ip::address_v6::from_string("2000:0db8:85a3:0000:0000:8a2e:0370:7335");

        // Notice order. Sorting the index must work to iterate in the expected order below
        sb.createA(fqdn, 1000, ip1);
        sb.createA(fqdn, 1000, ip3);
        sb.createA(fqdn, 1000, ip2);
        sb.createA(fqdn, 1000, ip4);
        sb.createNs(fqdn, 1000, "ns1.example.com");
        sb.createNs(fqdn, 1000, "ns2.example.com");
        sb.createNs(fqdn, 1000, "ns3.example.com");
        sb.createNs(fqdn, 1000, "ns4.example.com");
        sb.createSoa(fqdn, 5003, nsname, rname, 1000, 1001, 1002, 1003, 1004);
        sb.createMx(fqdn, 9999, 10, mxname);
        sb.finish();

        auto tx = db_.transaction();
        tx->write(fqdn, sb.buffer(), true);
        tx->commit();
    }

    void createWwwA() {
        StorageBuilder sb;
        string_view fqdn = "www.example.com";
        auto ip1 = boost::asio::ip::address_v4::from_string("127.0.0.3");
        auto ip2 = boost::asio::ip::address_v4::from_string("127.0.0.4");
        auto ip3 = boost::asio::ip::address_v6::from_string("2003:0db8:85a3:0000:0000:8a2e:0370:7334");
        auto ip4 = boost::asio::ip::address_v6::from_string("2004:0db8:85a3:0000:0000:8a2e:0370:7335");

        // Notice order. Sorting the index must work to iterate in the expected order below
        sb.createA(fqdn, 1000, ip1);
        sb.createA(fqdn, 1000, ip3);
        sb.createA(fqdn, 1000, ip2);
        sb.createA(fqdn, 1000, ip4);
        sb.finish();

        auto tx = db_.transaction();
        tx->write(fqdn, sb.buffer(), true);
        tx->commit();
    }

    void createFooWithHinfo() {
        StorageBuilder sb;
        string_view fqdn = "foo.example.com";

        // Notice order. Sorting the index must work to iterate in the expected order below
        sb.createHinfo(fqdn, 1000, "awesome", "minix");
        sb.finish();

        auto tx = db_.transaction();
        tx->write(fqdn, sb.buffer(), true);
        tx->commit();
    }

private:
    filesystem::path path_;
    Config c_;
    RocksDbResource db_;
};

} // ns
