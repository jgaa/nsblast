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
        RrAndSoa lookupEntryAndSoa(key_t fqdn) override;
        EntryWithBuffer lookup(key_t fqdn) override;
        uint32_t incrementVersionInSoa(key_t zoneFqdn) override;
        bool keyExists(key_t key) override;
        bool exists(std::string_view fqdn, uint16_t type) override;
        void write(key_t key, data_t data, bool isNew) override;
        read_ptr_t read(key_t key) override;
        void remove(key_t key, bool recursive) override;
        void commit() override;
        void rollback() override;

        auto operator -> () {
            return trx_.get();
        }

    private:
        RocksDbResource& owner_;
        std::once_flag once_;
        std::unique_ptr<ROCKSDB_NAMESPACE::Transaction> trx_;
    };

    RocksDbResource(const Config& config);

    // ResourceIf interface
    std::unique_ptr<TransactionIf> transaction() override;

    void init();

    auto& db() {
        assert(db_);
        return *db_;
    }

private:
    static constexpr size_t ZONE = 1;
    static constexpr size_t ENTRY = 2;
    static constexpr size_t ACCOUNT = 3;

    void prepareDirs();
    void open();
    void bootstrap();
    bool needBootstrap() const;
    std::string getDbPath() const;

    auto zoneHandle() noexcept {
        return cfh_[ZONE];
    }

    auto entryHandle() noexcept {
        return cfh_[ENTRY];
    }

    auto accountHandle() noexcept {
        return cfh_[ACCOUNT];
    }

    const Config& config_;
    rocksdb::TransactionDB *db_ = {};
    std::vector<rocksdb::ColumnFamilyDescriptor> cfd_;
    std::vector<rocksdb::ColumnFamilyHandle *> cfh_;
};

} // ns
