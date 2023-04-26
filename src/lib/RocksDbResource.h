#pragma once

#include "nsblast/nsblast.h"
#include "nsblast/ResourceIf.h"

#include "rocksdb/db.h"
#include "rocksdb/utilities/transaction.h"
#include "rocksdb/utilities/transaction_db.h"

namespace nsblast::lib {

class RocksDbResource : public ResourceIf {
public:
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
        read_ptr_t read(key_t key, Category category = Category::ENTRY) override;
        void read(key_t key, std::string& buffer, Category category = Category::ENTRY) override;
        void remove(key_t key, bool recursive, Category category = Category::ENTRY) override;
        void commit() override;
        void rollback() override;

        /*! Search for a key and return the range of keys that match
         *
         *  Used for example for zone transfers, where the key would be the
         *  fqdn to the zone, and the range would be all entries starting
         *  with that key.
         *
         *  Interlally a rocksdb iterator is opened for the key, and the user
         *  can iterate over the range as it is lazily fetched from the database.
         *
         *  The range should be closed or disposed over as quick as possible to
         *  not keep the rocksdb iterator open longer than required.
         */
        //Range search(key_t key);

        auto operator -> () {
            return trx_.get();
        }

    private:
        RocksDbResource& owner_;
        std::once_flag once_;
        std::unique_ptr<ROCKSDB_NAMESPACE::Transaction> trx_;
        bool dirty_ = false;

        // TransactionIf interface
    public:
    };

    RocksDbResource(const Config& config);
    ~RocksDbResource();

    // ResourceIf interface
    std::unique_ptr<TransactionIf> transaction() override;

    void init();

    auto& db() {
        assert(db_);
        return *db_;
    }

private:
    static constexpr size_t DEFAULT = 0;
    static constexpr size_t ZONE = 1;
    static constexpr size_t ENTRY = 2;
    static constexpr size_t DIFF = 3;
    static constexpr size_t ACCOUNT = 4;

    rocksdb::ColumnFamilyHandle * handle(const Category category);

    void prepareDirs();
    void open();
    void bootstrap();
    bool needBootstrap() const;
    std::string getDbPath() const;

    const Config& config_;
    rocksdb::TransactionDB *db_ = {};
    std::vector<rocksdb::ColumnFamilyDescriptor> cfd_;
    std::vector<rocksdb::ColumnFamilyHandle *> cfh_;
};

} // ns
