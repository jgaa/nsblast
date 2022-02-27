#pragma once

#include <memory>

#include "rocksdb/db.h"
#include "rocksdb/utilities/transaction.h"
#include "rocksdb/utilities/transaction_db.h"

#include "nsblast/nsblast.h"
#include "data.pb.h"

namespace nsblast::lib {

/*! Simple wrapper over rocksdb for the zones and their entries */
class Db {
public:
    class Transaction {
    public:
        Transaction(Db& db);
        ~Transaction();

        void rollback();
        void commit();

        auto operator -> () {
            return trx_;
        }

    private:
        std::once_flag once_;
        ROCKSDB_NAMESPACE::Transaction *trx_ = {};
    };

    class AlreadyExistException : public std::runtime_error {
    public:
        AlreadyExistException(const std::string& what) noexcept
            : std::runtime_error(what) {}
    };

    class NotFoundException : public std::runtime_error {
    public:
        NotFoundException(const std::string& what) noexcept
            : std::runtime_error(what) {}
    };

    Db(const Config& config);
    ~Db();

    void init();

    /*! Looks up the information regarding a zone if it exists */
    std::optional<Zone> getZone(std::string_view fqdn);

    void deleteZone(std::string_view fqdn);

    // Overwriting a zone automatically increments it's serial number
    // if `zone->serial() == 0`
    void writeZone(std::string_view fqdn, Zone& zone,
                   std::optional<bool> isNew = {}, bool mergeExisting = true);

    void deleteEntry(std::string_view fqdn);
    void writeEntry(std::string_view fqdn, const Entry& Entry);

    auto getDb() noexcept {
        assert(db_);
        return db_;
    }

    auto zoneHandle() noexcept {
        return cfh_[ZONE];
    }

    auto entryHandle() noexcept {
        return cfh_[ZONE];
    }

private:
    static constexpr size_t ZONE = 1;
    static constexpr size_t ENTRY = 2;

    void prepareDirs();
    void open();
    void bootstrap();
    bool needBootstrap() const;
    std::string getDbPath() const;

    rocksdb::TransactionDB *db_ = {};
    const Config config_;
    std::vector<rocksdb::ColumnFamilyDescriptor> cfd_;
    std::vector<rocksdb::ColumnFamilyHandle *> cfh_;
};


} // ns
