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

    ResourceIf& resource() {
        return db_;
    }

private:
    filesystem::path path_;
    Config c_;
    RocksDbResource db_;
};

} // ns
