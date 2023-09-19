
#include <chrono>

#include "rocksdb/db.h"

#include "RocksDbResource.h"
#include "nsblast/logging.h"
#include "nsblast/util.h"
#include "nsblast/errors.h"

using namespace std;
using namespace std::string_literals;
using namespace std::chrono_literals;

using ROCKSDB_NAMESPACE::Options;
using ROCKSDB_NAMESPACE::PinnableSlice;
using ROCKSDB_NAMESPACE::ReadOptions;
using ROCKSDB_NAMESPACE::Slice;
//using ROCKSDB_NAMESPACE::Transaction;
using ROCKSDB_NAMESPACE::TransactionDB;
using ROCKSDB_NAMESPACE::TransactionDBOptions;


namespace nsblast::lib {

namespace {


class DbLogger : public rocksdb::Logger {
public:
    DbLogger(rocksdb::InfoLogLevel logLevel ) : Logger(logLevel) {}


    // Logger interface
    void Logv(const rocksdb::InfoLogLevel log_level, const char *format, va_list ap) override {
        array<char, 1024> buffer = {0};
        string vbuffer;
        string_view message;
        auto len = vsnprintf(buffer.data(), buffer.size(), format, ap);
        if (len <= 0) {
            return;
        }

        if (len >= buffer.size()) {
            // Buffer was too small. Try again with the correct size
            vbuffer.resize(static_cast<size_t>(len) + 1);
            len = vsnprintf(vbuffer.data(), vbuffer.size(), format, ap);
            if (len > 0) {
                message = {vbuffer};
            }
        } else {
            message = {buffer.data(), static_cast<size_t>(len)};
        }

        LOGFAULT_LOG__(conv(log_level)) << "[RocksDB] " << message;
    }

    static logfault::LogLevel conv(rocksdb::InfoLogLevel level) {
        /*
          DEBUG_LEVEL = 0,
          INFO_LEVEL,
          WARN_LEVEL,
          ERROR_LEVEL,
          FATAL_LEVEL,
          HEADER_LEVEL,
          NUM_INFO_LOG_LEVELS,
        */
        static const array<logfault::LogLevel, 6> levels =
            {logfault::LogLevel::DEBUGGING,
            logfault::LogLevel::INFO,
            logfault::LogLevel::WARN,
            logfault::LogLevel::ERROR,
            logfault::LogLevel::ERROR,
            logfault::LogLevel::INFO};

        return levels.at(static_cast<size_t>(level));
    }

    static rocksdb::InfoLogLevel conv(logfault::LogLevel level) {
        /* DISABLED, ERROR, WARN, NOTICE, INFO, DEBUGGING, TRACE */

        static const array<rocksdb::InfoLogLevel, 7> levels = {
            rocksdb::InfoLogLevel::FATAL_LEVEL,
            rocksdb::InfoLogLevel::ERROR_LEVEL,
            rocksdb::InfoLogLevel::WARN_LEVEL,
            rocksdb::InfoLogLevel::INFO_LEVEL,
            rocksdb::InfoLogLevel::INFO_LEVEL,
            rocksdb::InfoLogLevel::DEBUG_LEVEL,
            rocksdb::InfoLogLevel::DEBUG_LEVEL
        };

        return levels.at(static_cast<size_t>(level));
    }
};

DbLogger &dbLogger() {
    static DbLogger logger{DbLogger::conv(logfault::LogManager::Instance().GetLoglevel())};

    return logger;
}

template <typename T = rocksdb::BackupEngine>
[[nodiscard]] auto getBackupEngine(const std::filesystem::path &backupDir, std::mutex& mutex) {
    using namespace rocksdb;

    unique_lock lock(mutex, try_to_lock);
    if (!lock.owns_lock()) {
        LOG_ERROR << "Failed to aquire backup mutex. A backup related operation is already in progress.";
        throw runtime_error{"Backup related operation already in progress"};
    }

    BackupEngineOptions opts{backupDir.string(),
                             nullptr, true, &dbLogger()};
    T* backup_engine = {};

    auto status = T::Open(opts, Env::Default(), &backup_engine);
    if (!status.ok()) {
        LOG_ERROR << "Failed to open backup with path: " << backupDir
                  << ": " << status.ToString();
        throw runtime_error{format("Failed to open backup: {}", status.ToString())};
    }

    return make_pair(makeSharedFrom(backup_engine), std::move(lock)); // RAAI object for backup_engine
}


} // anon ns

RocksDbResource::Transaction::Transaction(RocksDbResource &owner)
    : owner_{owner}
{
    LOG_TRACE << "Beginning transaction " << uuid();
    assert(!trx_);

    trx_.reset(owner.db().BeginTransaction({}));

    if (!trx_) {
        LOG_ERROR << "Failed to start transaction " << uuid();
        throw InternalErrorException{"Failed to start transaction", "Database error/transaction"};
    }

    // Nice to have the same name in rocksdb logs
    trx_->SetName(boost::uuids::to_string(uuid()));
    ++owner_.transaction_count_;
}

RocksDbResource::Transaction::~Transaction()
{
    LOG_TRACE << "Ending " << (trx_ ? "actual" : "closed/failed")  << " transaction " << uuid();
    if (trx_) {
        try {
            rollback();
        }  catch (const runtime_error& ex) {
            LOG_WARN << "RocksDbResource::Transaction::~Transaction - Caught exception from rollback(): "
                     << ex.what();
        }

        trx_.reset();
        --owner_.transaction_count_;
    }
}

ResourceIf::TransactionIf::RrAndSoa
RocksDbResource::Transaction::lookupEntryAndSoa(string_view fqdn)
{
    EntryWithBuffer rr;
    string_view key = fqdn;
    bool first = true;
    while(!key.empty()) {
        LOG_TRACE << "lookupEntryAndSoa: key=" << key;
        if (auto e = lookup(key)) {
            if (e.flags().soa) {
                if (first) {
                    // This is an exact match. RR and Soa is the same
                    assert(rr.empty());

                    return {std::move(e)};
                }

                return {std::move(rr), std::move(e)};
            }

            const auto zlen = e.header().zonelen;
            assert(zlen > 0);
            assert(zlen <= key.size());

            // Deduce the key to the zone/soa from the zonelen in the RR's header
            key = key.substr(key.size() - zlen);

            if (first) {
                assert(rr.empty());
                // Remember rr, we need to return it when the soa is found.
                rr = std::move(e);
            }
        } else if (auto pos = key.find('.'); pos != string_view::npos) {
            // Try the next level
            assert(!key.empty());
            key = key.substr(pos + 1);
        } else {
            break; // Nowhere left to go...
        }
        first = false;
    } // while key

    LOG_TRACE << "lookupEntryAndSoa: Not found";
    return {}; // Not found
}

ResourceIf::TransactionIf::EntryWithBuffer
RocksDbResource::Transaction::lookup(std::string_view fqdn)
{
    try {
        auto buffer = read(RealKey{fqdn, key_class_t::ENTRY});
        return {std::move(buffer)};
    }  catch (const NotFoundException&) {
        ;
    }

    return {}; // Not found
}

void RocksDbResource::Transaction::iterate(ResourceIf::TransactionIf::key_t key,
                                           ResourceIf::TransactionIf::iterator_fn_t fn,
                                           Category category)
{
    iterateT(key, category, fn);
}

bool RocksDbResource::Transaction::keyExists(ResourceIf::TransactionIf::key_t key, Category category)
{
    rocksdb::PinnableSlice ps;

    const auto status = trx_->Get({}, owner_.handle(category), toSlice(key.key()), &ps);

    if (status.ok()) {
        return true;
    }

    if (status.IsNotFound()) {
        return false;
    }

    LOG_WARN << "RocksDbResource::Transaction::keyExists: " << status.ToString();

    throw runtime_error{status.ToString()};
}

bool RocksDbResource::Transaction::exists(string_view fqdn, uint16_t type)
{
    try {
        auto b = read(key_t{fqdn, key_class_t::ENTRY});
        Entry entry{b->data()};

        if (type == TYPE_SOA) {
            return entry.flags().soa;
        }

        for(auto it : entry) {
            if (it.type() == type) {
                return true;
            }
        }

    }  catch (const NotFoundException&) {
        ;
    }

    return false;
}

void RocksDbResource::Transaction::write(ResourceIf::TransactionIf::key_t key,
                                         ResourceIf::TransactionIf::data_t data,
                                         bool isNew, Category category)
{
    LOG_TRACE << "RocksDbResource::Transaction::write - Write to transaction "
              << uuid() << " key: " <<  key
              << ", category " << category;

    if (isNew && keyExists(key)) {
        throw AlreadyExistException{"Key exists"};
    }
    const auto status = trx_->Put(owner_.handle(category), toSlice(key.key()), toSlice(data));

    if (!status.ok()) {
        throw InternalErrorException{"Rocksdb write failed: "s + status.ToString(), "Database error"};
    }

    if (isNew && status.IsOkOverwritten()) { // TODO: don't actually work...
        throw AlreadyExistException{"Key exists"};
    }

    if (!disable_trxlog_ && owner_.config_.db_log_transactions) {
        if (category == Category::ENTRY) {
            if (!trxlog_) {
                trxlog_ = make_unique<pb::Transaction>();
            }
            auto part = trxlog_->add_parts();
            part->set_key(key.data(), key.size());
            part->set_columnfamilyix(static_cast<int32_t>(category));
            part->set_value(data.data(), data.size());
        }
    }

    dirty_ = true;
}

ResourceIf::TransactionIf::read_ptr_t
RocksDbResource::Transaction::read(ResourceIf::TransactionIf::key_t key, Category category, bool throwIfNoeExixt)
{
    LOG_TRACE << "RocksDbResource::Transaction::read - Read from transaction "
              << uuid() << " key: " << key
              << ", category " << category;

    auto rval = make_unique<BufferImpl>();

    const auto status = trx_->Get({}, owner_.handle(category), toSlice(key.key()), &rval->ps_);

    if (status.ok()) {
        rval->prepare();
        return rval;
    }

    if (status.IsNotFound()) {
        if (throwIfNoeExixt) {
            throw NotFoundException{"Key not found"};
        }

        return {};
    }

    LOG_WARN << "RocksDbResource::Transaction::read - Read from transaction "
              << uuid() << " key: " << key
              << ", category " << category
              << " failed with status: " << status.ToString();

    throw InternalErrorException{status.ToString(), "Database error"};
}

bool RocksDbResource::Transaction::read(ResourceIf::TransactionIf::key_t key, string &buffer,
                                        ResourceIf::Category category, bool throwIfNoeExixt)
{
    LOG_TRACE << "RocksDbResource::Transaction::read (string) - Read from transaction "
              << uuid() << " key: " << key
              << ", category " << category;

    const auto status = trx_->Get({}, owner_.handle(category), toSlice(key.key()), &buffer);

    if (status.ok()) {
        return true;
    }

    if (status.IsNotFound()) {
        if (throwIfNoeExixt) {
            throw NotFoundException{"Key not found"};
        }
        return false;
    }

    LOG_WARN << "RocksDbResource::Transaction::read (string) - Read from transaction "
              << uuid() << " key: " << key
              << ", category " << category
              << " failed with status: " << status.ToString();
    throw InternalErrorException{status.ToString(), "Database error"};
}

void RocksDbResource::Transaction::remove(ResourceIf::TransactionIf::key_t key,
                                          bool recursive, Category category)
{
    if (recursive) {
        LOG_TRACE << "RocksDbResource::Transaction::remove Removing key "
                  << key << ", category " << category
                  << " recursively.";

        // Assumption: The iterator starts at key, and we can do a memcmp to
        // figure out if we are still iterating the same zone
        //
        // Note that DeleteRange is probably a better option for large zones.
        // Unfortunately, it's not yet available in transactions
        rocksdb::ReadOptions options = {};
        auto it = makeUniqueFrom(trx_->GetIterator(options, owner_.handle(category)));
        for(it->Seek(toSlice(key.key())); it->Valid() ; it->Next()) {
            const auto ck = it->key();
            const auto extra_len = static_cast<int>(ck.size() - key.size());
            if (extra_len < 0) {
                break;
            }
            if (extra_len > 0) {
                // Validate that ck is a desecndent of key
                assert(ck.size() > key.size() - 1);
                if (ck[ck.size() - key.size() - 1] != '.') {
                    break;
                }
                if (extra_len > 1) {
                    // Corner-case - escaped dot in the same location as the zone-dot, but for a different zone
                    if (ck[ck.size() - key.size() - 2] == '\\') {
                        break;
                    }
                }
            }

            assert(extra_len >= 0);
            // Check that the zone-part of ck is equal to the zone (that we have not skipped beoynd the zone)
            if (memcmp(ck.data() + extra_len, key.data(), key.size()) != 0) {
                break;
            }
            trx_->Delete(owner_.handle(category), it->key());
            addDeletedToTrxlog(key, category);
        }
    } else {
        LOG_TRACE << "RocksDbResource::Transaction::remove Removing key "
                  << key << ", category " << category;

        trx_->Delete(owner_.handle(category), toSlice(key));
        addDeletedToTrxlog(key, category);
    }

    dirty_ = true;
}

void RocksDbResource::Transaction::commit()
{
    call_once(once_, [&] {
        handleTrxLog();
        LOG_TRACE << "Committing transaction " << uuid();
        auto status = trx_->Commit();
        if (!status.ok()) {
            LOG_ERROR << "Transaction " << uuid() << " failed: " << status.ToString();
            throw runtime_error{"Failed to commit transaction"};
        }

        if (trxlog_ && owner_.on_trx_cb_) {
            try {
                owner_.on_trx_cb_(std::move(trxlog_));
            } catch(const exception& ex) {
                LOG_ERROR
                    << "RocksDbResource::Transaction::commit: "
                    << "Caught exception from transaction callback: "
                    << ex.what();
            }
        }
    });
}

void RocksDbResource::Transaction::rollback()
{
    call_once(once_, [&] {
        if (dirty_) {
            LOG_TRACE << "Rolling back transaction " << uuid();
        } else {
            LOG_TRACE << "Closing clean transaction " << uuid();
        }
        auto status = trx_->Rollback();
        if (!status.ok()) {
            LOG_ERROR << "Transaction rollback failed " << uuid() << " : " << status.ToString();
            throw InternalErrorException{"Failed to rollback transaction", "Database error/rollback"};
        }
    });
}

string RocksDbResource::Transaction::getRocksdbVersion()
{
    return rocksdb::GetRocksVersionAsString();
}

void RocksDbResource::Transaction::handleTrxLog()
{
    if (trxlog_ && trxlog_->parts_size()) {
        trxlog_->set_node(owner_.config_.node_name);
        trxlog_->set_uuid(uuid().begin(), uuid().size());
        trxlog_->set_time(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());

        // The ordering may not be exactely right, as we can't control the
        // order of transactions execution without serializing them, but we can assume that
        // no conflicting transactions are committed where the transaxtion-id may
        // be in the wrong order (dirty writes).
        // Holes in the sequence, if a commit failes, are OK.
        replication_id_ = owner_.createNewTrxId();
        trxlog_->set_id(replication_id_);

        const RealKey key{trxlog_->id(), RealKey::Class::TRXID};

        LOG_TRACE << "RocksDbResource::Transaction::handleTrxLog - Saving transaction "
                  << key;

        string val;
        trxlog_->SerializeToString(&val);
        write(key, val, false, Category::TRXLOG);
    } else if (trxlog_) {
        // We have an object, but it is empty.
        trxlog_.reset();
    }
}

void RocksDbResource::Transaction::addDeletedToTrxlog(span_t key, Category category)
{
    if (!disable_trxlog_ && owner_.config_.db_log_transactions) {
        if (category == Category::ENTRY) {
            if (!trxlog_) {
                trxlog_ = make_unique<pb::Transaction>();
            }
            auto part = trxlog_->add_parts();
            part->set_key(key.data(), key.size());
            part->set_columnfamilyix(static_cast<int32_t>(category));
        }
    }
}

RocksDbResource::RocksDbResource(const Config &config)
    : config_{config}
{
    rocksdb::ColumnFamilyOptions o = rocksdb_options_;
    cfd_.emplace_back(rocksdb::kDefaultColumnFamilyName, o);
    cfd_.emplace_back("masterZone", o);
    cfd_.emplace_back("entry", o);
    cfd_.emplace_back("diff", o);
    cfd_.emplace_back("account", o);
    cfd_.emplace_back("trxlog", o);
}

RocksDbResource::~RocksDbResource()
{
    close();
}

std::unique_ptr<ResourceIf::TransactionIf> RocksDbResource::transaction()
{
    return make_unique<Transaction>(*this);
}

void RocksDbResource::init()
{
    rocksdb_options_.db_write_buffer_size = config_.rocksdb_db_write_buffer_size;

    if (config_.rocksdb_optimize_for_small_db) {
        LOG_INFO << "RocksDbResource::init - OptimizeForSmallDb";
        rocksdb_options_.OptimizeForSmallDb();
    }

    if (config_.rocksdb_background_threads) {
        LOG_INFO << "RocksDbResource::init - IncreaseParallelism("
                 << config_.rocksdb_background_threads << ")";
        rocksdb_options_.IncreaseParallelism(config_.rocksdb_background_threads);
    }

    prepareDirs();
    if (needBootstrap()) {
        bootstrap();
    } else {
        open();
        loadTrxId();
    }
}

void RocksDbResource::close()
{
    // https://stackoverflow.com/questions/56173836/rocksdb-assertion-last-ref-failed-when-closing-db-while-using-unique-pointers

    LOG_INFO << "Closing RocksDB. " << transaction_count_ << " active transactions.";

    // Make sure any ongoing backup is aborted before we shut down the DD engine.
    std::optional<thread> backup_thd;

    {
        lock_guard lock{mutex_};

        if (auto backup = active_backup_.lock()) {
            LOG_INFO_N << "Aborting ongoing backup " << active_backup_uuid_;
            backup->StopBackup();
        }

        if (backup_thread_) {
            backup_thd = std::move(backup_thread_);
            backup_thread_.reset();
        }
    }

    if (backup_thd) {
        LOG_INFO_N << "Joining backup-thread...";
        backup_thd->join();
    }

    if (db_) {
        LOG_TRACE << "RocksDbResource::~RocksDbResource - Removing ColumnFamilyHandle ...";
        for(auto fh : cfh_) {
            if (fh) {
                LOG_TRACE << "RocksDbResource::~RocksDbResource - ... " << fh->GetName();
                auto result = db_->DestroyColumnFamilyHandle(fh);
                if (!result.ok()) {
                    LOG_ERROR << "Failed to destroy ColumnFamilyHandle " << fh->GetName();
                }
            }
        }


        auto result = db_->SyncWAL();
        if (!result.ok()) {
            LOG_ERROR << "RocksDbResource::~RocksDbResource - Failed to sync WAL: "
                      << result.ToString();
        }

        LOG_TRACE << "RocksDbResource::~RocksDbResource - Closing db_";
        result = db_->Close();
        if (!result.ok()) {
            LOG_ERROR << "RocksDbResource::~RocksDbResource - Failed to close the database: "
                      << result.ToString();
        }

        LOG_TRACE << "RocksDbResource::~RocksDbResource - deleting db_";
        delete db_;
        db_ = {};
    }
}

uint64_t RocksDbResource::getLastCommittedTransactionId()
{
    ReadOptions o;
    auto it = makeUniqueFrom(db_->NewIterator(o, handle(Category::TRXLOG)));
    it->SeekToLast();
    if (it->Valid()) {
        span_t key = it->key();
        const RealKey kkey{key, key_class_t::ENTRY};
        auto txid = getValueAt<uint64_t>(key, 1);
        LOG_DEBUG << "RocksDbResource::loadTrxId - Last committed TRANSACTION-log: " << kkey
                  << ". trx-id is " << txid;
        return txid;
    }

    return 0;
}

void RocksDbResource::backup(std::filesystem::path backupDir,
                             bool syncFirst, boost::uuids::uuid uuid)
{
    using namespace rocksdb;

    backupDir = getBackupPath(backupDir);

    auto [handle, lock] = getBackupEngine(backupDir, backup_mutex_);

    const auto uuid_str = toLower(boost::uuids::to_string(uuid));

    CreateBackupOptions backup_opts;
    backup_opts.flush_before_backup = syncFirst;
    BackupID id = {};

    LOG_INFO << "Starting database backup " << uuid_str << " to path " << backupDir;
    assert(db_);

    {
        lock_guard lock2{mutex_};
        active_backup_ = handle;
        active_backup_uuid_ = uuid;
    }

    auto status = handle->CreateNewBackupWithMetadata(backup_opts, db_, uuid_str, &id);
    if (!status.ok()) {
        LOG_ERROR_N << "Failed to backup to path: " << backupDir
                    << ": " << status.ToString();
        throw runtime_error{format("Failed to backup: {}", status.ToString())};
    }
    LOG_INFO << "Successfully backup up database. Backup " << uuid_str
             << " #" << id << " on path " << backupDir;
}

void RocksDbResource::startBackup(const std::filesystem::path &backupDir,
                                  bool syncFirst, boost::uuids::uuid uuid)
{
    unique_lock lock(backup_mutex_, try_to_lock);
    if (!lock.owns_lock()) {
        LOG_ERROR_N << "Failed to aquire backup mutex. A backup related operation is already in progress.";
        throw runtime_error{"Backup related operation already in progress"};
    }


    lock_guard lock2{mutex_};
    active_backup_.reset();
    active_backup_uuid_ = {};

    if (backup_thread_) {
        LOG_INFO_N << "Joining stale backup-thread...";
        backup_thread_->join();
        backup_thread_.reset();
    }

    backup_thread_.emplace([backupDir, uuid, syncFirst, this] {
        LOG_DEBUG << "Starting async backup " << uuid;
        try {
            backup(backupDir, syncFirst, uuid);
        } catch(const exception& ex) {
            LOG_ERROR << "Caught exception from backup " << uuid <<": " << ex.what();
        }

        LOG_DEBUG << "Async backup " << uuid << " is done.";
    });
}

void RocksDbResource::listBackups(boost::json::object &meta, std::filesystem::path backupDir)
{
    using namespace rocksdb;

    backupDir = getBackupPath(backupDir);

    vector<BackupInfo> bi;
    {
        LOG_DEBUG_N << "Getting backup info...";
        auto [handle, lock] = getBackupEngine(backupDir, backup_mutex_);
        handle->GetBackupInfo(&bi);
    }

    meta["backups"] = boost::json::array{};

    for(const auto& b : bi) {
        boost::json::object o;
        o["id"] = b.backup_id;
        if (!b.app_metadata.empty()) {
            o["uuid"] = b.app_metadata;
        }
        o["timestamp"] = b.timestamp;
        o["size"] = b.size;
        o["number_files"] = b.number_files;

        // Informative; for users reading the payload as text.
        o["date"] = format("{} UTC", std::chrono::system_clock::from_time_t(b.timestamp));

        meta["backups"].as_array().emplace_back(std::move(o));
    }

    meta["num_backups"] = meta["backups"].as_array().size();

    LOG_TRACE_N << "Backup Info: " << boost::json::serialize(meta);
}

void RocksDbResource::restoreBackup(unsigned int backupId, const std::filesystem::path &backupDir)
{
    using namespace rocksdb;
    assert(!db_);

    LOG_DEBUG_N << "Restoring backup " << backupId << " at " << backupDir;
    auto [handle, lock] = getBackupEngine<BackupEngineReadOnly>(backupDir, backup_mutex_);


    const auto db_path = getDbPath();
    auto status = handle->RestoreDBFromBackup(backupId, db_path, db_path);
    if (status.ok()) {
        LOG_DEBUG_N << "Restore of backup #" << backupId << " at " << backupDir << " was successful.";
        return;
    }

    LOG_WARN_N << "Restore of backup #" << backupId << " at " << backupDir
               << " failed: " << status.ToString();
}

bool RocksDbResource::verifyBackup(unsigned int backupId, std::filesystem::path backupDir,
                                   string* message)
{
    using namespace rocksdb;

    backupDir = getBackupPath(backupDir);

    LOG_DEBUG_N << "Verifying backup " << backupId << " at " << backupDir;
    auto [handle, lock] = getBackupEngine(backupDir, backup_mutex_);
    auto status = handle->VerifyBackup(backupId, true);
    if (status.ok()) {
        LOG_DEBUG_N << "Backup #" << backupId << " at " << backupDir << " is OK ";
        return true;
    }

    LOG_WARN_N << "Backup #" << backupId << " at " << backupDir
               << " failed verification: " << status.ToString();

    if (message) {
        *message = status.ToString();
    }

    return false;
}

void RocksDbResource::purgeBackups(int numToKeep, std::filesystem::path backupDir)
{
    using namespace rocksdb;

    backupDir = getBackupPath(backupDir);

    LOG_INFO_N << "Purging backups in " << backupDir
                << ", keeping " << numToKeep << " newest.";

    auto [handle, lock] = getBackupEngine(backupDir, backup_mutex_);
    auto status = handle->PurgeOldBackups(numToKeep);

    if (status.ok()) {
        LOG_DEBUG_N << "Purge of backups in " << backupDir << " is OK ";
        return;
    }

    LOG_WARN_N << "Purge of backups in " << backupDir << " failed: "
               << status.ToString();

    throw runtime_error{"Purge of backups failed"};
}

bool RocksDbResource::deleteBackup(int id, std::filesystem::path backupDir)
{
    using namespace rocksdb;

    backupDir = getBackupPath(backupDir);

    LOG_INFO_N << "Deleting backup " << id << " in " << backupDir;

    auto [handle, lock] = getBackupEngine(backupDir, backup_mutex_);
    auto status = handle->DeleteBackup(id);

    if (status.ok()) {
        LOG_DEBUG_N << "Delete of backup " << id << " in " << backupDir << " is OK ";
        return true;
    }

    if (status.IsNotFound()) {
        return false;
    }

    LOG_WARN_N << "Delete of backup in " << backupDir << " failed: "
               << status.ToString();

    throw runtime_error{"Delete of backup failed"};
}

rocksdb::ColumnFamilyHandle *RocksDbResource::handle(const ResourceIf::Category category) {
    const auto ix = static_cast<size_t>(category);
    assert(ix <= cfh_.size());
    if (ix < cfh_.size()) {
        return cfh_[ix];
    }

    throw runtime_error{"handle: Unknown RocksDB Category "s + std::to_string(ix)};
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

    rocksdb_options_.create_if_missing = false;
    rocksdb_options_.create_missing_column_families = true;

    TransactionDBOptions txn_db_options;
    const auto status = TransactionDB::Open(rocksdb_options_, txn_db_options, getDbPath(), cfd_, &cfh_, &db_);
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
    rocksdb_options_.create_if_missing = true;
    rocksdb_options_.create_missing_column_families = true;
    TransactionDBOptions txn_db_options;
    const auto status = TransactionDB::Open(rocksdb_options_, txn_db_options, getDbPath(), cfd_, &cfh_, &db_);
    if (!status.ok()) {
        LOG_ERROR << "Failed to create database " << getDbPath()
                  << status.ToString();
        throw runtime_error{"Failed to create the database"};
    }

    bootstrapped_ = true;
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

void RocksDbResource::loadTrxId()
{
    trx_id_ = getLastCommittedTransactionId();
    LOG_DEBUG << "RocksDbResource::loadTrxId - trx_id is set to " << trx_id_;
}

std::filesystem::path RocksDbResource::getBackupPath(std::filesystem::path path) const
{
    if (path.empty()) {
        path = config().db_path;
        path /= "backup";
    }
    return path;
}


} // ns
