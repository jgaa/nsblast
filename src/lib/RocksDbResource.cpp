
#include "RocksDbResource.h"
#include "nsblast/logging.h"

using namespace std;
using namespace std::string_literals;
using namespace std::chrono_literals;

using ROCKSDB_NAMESPACE::DB;
using ROCKSDB_NAMESPACE::Options;
using ROCKSDB_NAMESPACE::PinnableSlice;
using ROCKSDB_NAMESPACE::ReadOptions;
using ROCKSDB_NAMESPACE::Status;
using ROCKSDB_NAMESPACE::WriteBatch;
using ROCKSDB_NAMESPACE::WriteOptions;
using ROCKSDB_NAMESPACE::Slice;
//using ROCKSDB_NAMESPACE::Transaction;
using ROCKSDB_NAMESPACE::TransactionDB;
using ROCKSDB_NAMESPACE::TransactionDBOptions;


namespace nsblast::lib {

RocksDbResource::Transaction::Transaction(RocksDbResource &owner)
    : owner_{owner}
{
    LOG_TRACE << "Beginning transaction " << uuid();
    assert(!trx_);

    trx_.reset(owner.db().BeginTransaction({}));

    if (!trx_) {
        LOG_ERROR << "Failed to start transaction " << uuid();
        throw runtime_error{"Failed to start transaction"};
    }

    // Nice to have the same name in rocksdb logs
    trx_->SetName(boost::uuids::to_string(uuid()));
}

RocksDbResource::Transaction::~Transaction()
{
    if (trx_) {
        rollback();
    }
}

bool RocksDbResource::Transaction::exists(string_view fqdn, uint16_t type)
{
    // TODO: Implement
    return false;
}

void RocksDbResource::Transaction::commit()
{
    call_once(once_, [&] {
        LOG_TRACE << "Committing transaction " << uuid();
        auto status = trx_->Commit();
        if (!status.ok()) {
            LOG_ERROR << "Transaction " << uuid() << " failed: " << status.ToString();
            throw runtime_error{"Failed to commit transaction"};
        }
    });
}

void RocksDbResource::Transaction::rollback()
{
    call_once(once_, [&] {
        LOG_TRACE << "Rolling back transaction";
        auto status = trx_->Rollback();
        if (!status.ok()) {
            LOG_ERROR << "Transaction rollback failed: " << status.ToString();
            throw runtime_error{"Failed to rollback transaction"};
        }
    });
}

RocksDbResource::RocksDbResource(const Config &config)
    : config_{config}
{
    rocksdb::ColumnFamilyOptions o;
    cfd_.emplace_back(rocksdb::kDefaultColumnFamilyName, o);
    cfd_.emplace_back("zone", o);
    cfd_.emplace_back("entry", o);
    cfd_.emplace_back("account", o);
}

std::unique_ptr<ResourceIf::TransactionIf> RocksDbResource::transaction()
{
    return make_unique<Transaction>(*this);
}

void RocksDbResource::init()
{
    prepareDirs();
    if (needBootstrap()) {
        bootstrap();
    } else {
        open();
    }
}

void RocksDbResource::prepareDirs()
{
    if (!filesystem::is_directory(config_.db_path)) {
        LOG_INFO << "Creating directory: " << config_.db_path;
        filesystem::create_directories(config_.db_path);
    }
}

void RocksDbResource::open()
{
    LOG_INFO << "Openig RocksDB " << rocksdb::GetRocksVersionAsString()
                << ": " << getDbPath();
    rocksdb::Options options;
    options.create_if_missing = false;

    TransactionDBOptions txn_db_options;
    const auto status = TransactionDB::Open(options, txn_db_options, getDbPath(),
                                          cfd_, &cfh_, &db_);
    if (!status.ok()) {
        LOG_ERROR << "Failed to open database " << config_.db_path
                  << status.ToString();
        throw runtime_error{"Failed to open database"};
    }
}

void RocksDbResource::bootstrap()
{
    LOG_INFO << "Bootstrapping RocksDB " << rocksdb::GetRocksVersionAsString()
             <<": " << getDbPath();

    filesystem::create_directories(getDbPath());
    rocksdb::Options options;
    options.create_if_missing = true;
    TransactionDBOptions txn_db_options;
    const auto status = TransactionDB::Open(options, txn_db_options, getDbPath(), &db_);
    if (!status.ok()) {
        LOG_ERROR << "Failed to open database " << getDbPath()
                  << status.ToString();
        throw runtime_error{"Failed to open database"};
    }

    rocksdb::ColumnFamilyOptions o;

    for(const auto& cf: cfd_) {
        cfh_.emplace_back();

        if (cf.name == "default") {
            continue; // crazyness in action
        }

        LOG_TRACE << "Creating column family " << cf.name;
        const auto r = db_->CreateColumnFamily(cf.options, cf.name, &cfh_.back());
        if (!r.ok()) {
            LOG_ERROR << "Failed to create RocksDN Column Family " << cf.name
                      << ": " << r.ToString();
            throw runtime_error{"Failed to bootstrap RocksDB"};
        }
    }
}

bool RocksDbResource::needBootstrap() const
{
    return !filesystem::is_directory(getDbPath());
}

string RocksDbResource::getDbPath() const
{
    filesystem::path p = config_.db_path;
    p /= "rocksdb";
    return p.string();
}


} // ns
