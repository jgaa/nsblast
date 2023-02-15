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
        Transaction(RocksDbResource& owner);
        ~Transaction();

        // TransactionIf interface
        bool exists(std::string_view fqdn, uint16_t type) override;
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

    const Config& config_;
    rocksdb::TransactionDB *db_ = {};
    std::vector<rocksdb::ColumnFamilyDescriptor> cfd_;
    std::vector<rocksdb::ColumnFamilyHandle *> cfh_;
};

} // ns
