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
        read_ptr_t read(key_t key, Category category = Category::ENTRY, bool throwIfNoeExixt = true) override;
        bool read(key_t key, std::string& buffer, Category category = Category::ENTRY, bool throwIfNoeExixt = true) override;
        void remove(key_t key, bool recursive, Category category = Category::ENTRY) override;
        void commit() override;
        void rollback() override;

        auto operator -> () {
            return trx_.get();
        }

        template <typename fnT>
        void iterateFromPrevT(key_t& key,  Category category, fnT& fn) {
            auto it = makeUniqueFrom(trx_->GetIterator({}, owner_.handle(category)));
            it->SeekForPrev(toSlice(key));
            if (it->Valid()) {
                // Skip the 'last' key.
                it->Next();
            }

            for(; it->Valid(); it->Next()) {
                const auto& k = it->key();
                if (!key.isSameKeyClass(k)) [[unlikely]] {
                    return;
                }

                if (!fn({k, key.kClass(), true}, it->value())) {
                    return;
                }
            }
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
                if (!fn({k, key.kClass(), true}, it->value())) {
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

    bool wasBootstrapped() const noexcept {
        return bootstrapped_;
    }

private:
    static constexpr size_t DEFAULT = 0;
    static constexpr size_t MASTER_ZONE = 1;
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
    bool bootstrapped_ = false;
    std::atomic_int transaction_count_{0};
    rocksdb::Options rocksdb_options_;
};

} // ns
