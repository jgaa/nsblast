#pragma once

#include <boost/json.hpp>

#include "nsblast/nsblast.h"
#include "nsblast/ResourceIf.h"
#include "proto/nsblast.pb.h"

#include "rocksdb/db.h"
#include "rocksdb/utilities/backup_engine.h"
#include "rocksdb/utilities/transaction.h"
#include "rocksdb/utilities/transaction_db.h"

namespace nsblast::lib {

class RocksDbResource : public ResourceIf {
public:
    using on_trx_cb_t = std::function<void(std::unique_ptr<nsblast::pb::Transaction>)>;

    class Transaction : public ResourceIf::TransactionIf {
    public:
        struct BufferImpl : public BufferBase {

            void prepare() {
                data_ = {ps_.data(), ps_.size()};
            }

            rocksdb::PinnableSlice ps_;
        };

        Transaction(RocksDbResource& owner);
        ~Transaction();

        // TransactionIf interface
        RrAndSoa lookupEntryAndSoa(std::string_view fqdn) override;
        EntryWithBuffer lookup(std::string_view fqdn) override;
        void iterate(key_t, iterator_fn_t fn, Category category = Category::ENTRY) override;
        bool keyExists(key_t key, Category category = Category::ENTRY) override;
        bool exists(std::string_view fqdn, uint16_t type) override;
        void write(key_t key, data_t data, bool isNew, Category category = Category::ENTRY) override;
        read_ptr_t read(key_t key, Category category = Category::ENTRY, bool throwIfNoeExixt = true) override;
        bool read(key_t key, std::string& buffer, Category category = Category::ENTRY, bool throwIfNoeExixt = true) override;
        void remove(key_t key, bool recursive, Category category = Category::ENTRY) override;
        void commit() override;
        void rollback() override;
        uint64_t replicationId() const noexcept override {
            return replication_id_;
        }

        auto operator -> () {
            return trx_.get();
        }

        /*! Iterate from the item following `key`.
         *
         *  The iteration starts at the first object that logically follows after
         *  key.
         */
        template <typename fnT>
        void iterateFromPrevT(key_t& key,  Category category, fnT& fn) {
            auto it = makeUniqueFrom(trx_->GetIterator({}, owner_.handle(category)));
            it->SeekForPrev(toSlice(key));
            if (it->Valid()) {
                // Skip the 'last' key.
                it->Next();
            } else {
                // The key pointed potentially to an item prior to the first key ion the data.
                // Fall back to searching for the first key.
                it->SeekToFirst();
            }

            for(; it->Valid(); it->Next()) {
                const auto& k = it->key();
                if (!key.isSameKeyClass(k)) [[unlikely]] {
                    return;
                }

                if (!fn({RealKey::Binary{k}}, it->value())) {
                    return;
                }
            }

//            assert(!it->Valid());
//            // Signal that we are EOF
//            static const rocksdb::Slice empty_slice = {};
//            fn({RealKey::Binary{empty_slice}}, empty_slice);
        }

        template <typename fnT>
        void iterateT(key_t& key, Category category, fnT& fn)
        {
            auto it = makeUniqueFrom(trx_->GetIterator({}, owner_.handle(category)));
            for(it->Seek(toSlice(key)); it->Valid(); it->Next()) {
                const auto& k = it->key();
                if (!key.isSameKeyClass(k)) [[unlikely]] {
                    return;
                }
                if (!fn({RealKey::Binary{k}}, it->value())) {
                    return;
                }
            }
        }

        template <typename fnT>
        void iterateT(Category category, RealKey::Class kclass, fnT& fn)
        {
            auto it = makeUniqueFrom(trx_->GetIterator({}, owner_.handle(category)));
            for(it->SeekToFirst(); it->Valid(); it->Next()) {
                if (!fn({it->key(), kclass, true}, it->value())) {
                    return;
                }
            }
        }

        template <typename T>
        rocksdb::Slice toSlice(const T& v) {
            return  rocksdb::Slice{v.data(), v.size()};
        }

        void disableTrxlog() {
            disable_trxlog_ = true;
        }

        static std::string getRocksdbVersion();

    private:
        void handleTrxLog();
        void addDeletedToTrxlog(span_t key, Category category);

        RocksDbResource& owner_;
        std::once_flag once_;
        std::unique_ptr<ROCKSDB_NAMESPACE::Transaction> trx_;
        bool dirty_ = false;
        bool disable_trxlog_ = false;
        std::unique_ptr<pb::Transaction> trxlog_;
        uint64_t replication_id_ = 0;

        // TransactionIf interface
    public:
    };

    RocksDbResource(const Config& config);
    ~RocksDbResource();

    // ResourceIf interface
    std::unique_ptr<TransactionIf> transaction() override;

    auto dbTransaction() {
        return std::make_unique<Transaction>(*this);
    }

    void init();

    /*! Closes the database.
     *
     *  Must not be called if the REST API is active, as this may
     *  keep transactions open or cause race-conditions with
     *  backups.
     */
    void close();

    auto& db() {
        assert(db_);
        return *db_;
    }

    bool wasBootstrapped() const noexcept {
        return bootstrapped_;
    }

    const auto& config() const noexcept {
        return config_;
    }

    uint64_t createNewTrxId() noexcept {
        return ++trx_id_;
    }

    uint64_t currentTrxId() const noexcept {
        return trx_id_;
    }

    uint64_t getLastCommittedTransactionId();

    void setTransactionCallback(on_trx_cb_t && cb) {
        assert(!on_trx_cb_);
        on_trx_cb_ = std::move(cb);
    }

    /*! Backup the database.
     *
     *  Only one backup can be active at any time. If this method is
     *  called when a backup is active, a std::runtime exception is thrown.
     *
     *  \param backupDir Destination directory for backups
     *
     *  \throws std::runtime_error on errors
     */
    void backup(std::filesystem::path backupDir, bool syncFirst = true, boost::uuids::uuid uuid=newUuid());

    /*! Start a backup in a worker thread.
     *
     *  Returns immediately.
     */
    void startBackup(const std::filesystem::path& backupDir, bool syncFirst = true, boost::uuids::uuid uuid=newUuid());

    /*! List backups.
     *
     *  \param meta Metadata about backups
     *  \param backupDir Destination directory for backups
     */
    void listBackups(boost::json::object& meta, std::filesystem::path backupDir);

    /*! Restore a backup
     *
     *  The database can not be open during restore.
     *
     *  \param backupDir Destination directory for backups
     *  \param backupId ID of the backup to restore
     *
     *  \throws std::runtime_error on errors
     */
    void restoreBackup(unsigned backupId, const std::filesystem::path& backupDir);

    /*! verify a backup
     *
     *  The database can not be open during restore.
     *
     *  \param backupDir Destination directory for backups
     *  \param backupId ID of the backup to verify
     *
     *  \return true if the backup was OK
     *  \throws std::runtime_error on errors
     */
    bool verifyBackup(unsigned backupId, std::filesystem::path backupDir, std::string* message = nullptr);

    void purgeBackups(int numToKeep, std::filesystem::path backupDir);

    /*! Delete a specific backup
     *
     *  \param id ID of backup to delete
     *  \param backupDir optional path to backup dir
     *  \return true if the backup was deleted, false if it was not found.
     *
     *  \throws std::runtime_error if some other error occur
     */
    bool deleteBackup(int id, std::filesystem::path backupDir);

private:
    static constexpr size_t DEFAULT = 0;
    static constexpr size_t MASTER_ZONE = 1;
    static constexpr size_t ENTRY = 2;
    static constexpr size_t DIFF = 3;
    static constexpr size_t ACCOUNT = 4;
    static constexpr size_t TRXLOG = 5;

    rocksdb::ColumnFamilyHandle * handle(const Category category);

    void prepareDirs();
    void open();
    void bootstrap();
    bool needBootstrap() const;
    std::string getDbPath() const;
    void loadTrxId();
    std::filesystem::path getBackupPath(std::filesystem::path path) const;

    const Config& config_;
    rocksdb::TransactionDB *db_ = {};
    std::vector<rocksdb::ColumnFamilyDescriptor> cfd_;
    std::vector<rocksdb::ColumnFamilyHandle *> cfh_;
    bool bootstrapped_ = false;
    std::atomic_int transaction_count_{0};
    rocksdb::Options rocksdb_options_;
    std::atomic_uint64_t trx_id_{0};
    on_trx_cb_t on_trx_cb_;
    std::weak_ptr<rocksdb::BackupEngine> active_backup_;
    std::optional<std::thread> backup_thread_;
    boost::uuids::uuid active_backup_uuid_;
    std::mutex backup_mutex_;
    std::mutex mutex_;
};

} // ns
