#pragma once

#include <memory>

#include "rocksdb/db.h"
#include "rocksdb/utilities/transaction.h"
#include "rocksdb/utilities/transaction_db.h"

#include "nsblast/nsblast.h"

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

    /*! Search for a zone owning the fqdn */
    std::optional<std::tuple<std::string /*fqdn */, Zone>>
    findZone(std::string_view fqdn);

    void deleteZone(std::string_view fqdn);

    // Overwriting a zone automatically increments it's serial number
    // if `zone->serial() == 0`
    void writeZone(std::string_view fqdn, Zone& zone,
                   std::optional<bool> isNew = {}, bool mergeExisting = true);

    // Increments the serial number of the zone
    void incrementZone(Transaction& trx, std::string_view fqdn);

    /*! Looks up the information regarding a resource record if it exists */
    std::optional<Rr> getRr(std::string_view fqdn);

    /*! Delete a resource record
     *
     *  @param zone Zone key. Required to increment serial
     *  @param fqdn key
     *  @param type If set, only the matching resource type (a, aaaa, cname, txt) is
     *      deleted. The key is deleted if there are no remaining resource types set.
     */
    void deleteRr(std::string_view zone, std::string_view fqdn, std::string_view type);

    /*! add/update/replace the rr's.
     *
     *  @param zone - Zone key. Required to increment serial
     *  @param fqdn - key
     *  @param rr - rr's to set
     *  @param isNew - If set, the rr record must be new, or must exist.
     *  @param mergeExisting - If set unset resources in the rr argument are not
     *      changed. Set resources will be replaced. Sanity checks are always
     *      performed, fo example, if a cname is set, and a/aaaa entriues are unset,
     *      existing a/aaaa entries are deleted.
     */

    void writeRr(std::string_view zone, std::string_view fqdn, Rr& rr,
        std::optional<bool> isNew = {}, bool mergeExisting = true);

    auto getDb() noexcept {
        assert(db_);
        return db_;
    }

    auto zoneHandle() noexcept {
        return cfh_[ZONE];
    }

    auto entryHandle() noexcept {
        return cfh_[ENTRY];
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
