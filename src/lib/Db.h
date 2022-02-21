#pragma once

#include <memory>

#include "rocksdb/db.h"

#include "nsblast/nsblast.h"
#include "data.pb.h"

namespace nsblast::lib {

/*! Simple wrapper over rocksdb for the zones and their entries */
class Db {
public:
    Db(const Config& config);
    ~Db();

    void init();

    /*! Looks up the information regarding a zone if it exists */
    std::optional<Zone> lookup(std::string_view fqdn);

    void deleteZone(std::string_view fqdn);
    void deleteEntry(std::string_view fqdn);
    void writeZone(std::string_view fqdn, const Zone& Zone);
    void writeEntry(std::string_view fqdn, const Entry& Entry);

    auto getDb() noexcept {
        assert(db_);
        return db_;
    }

private:
    void prepareDirs();
    void open();
    void bootstrap();
    bool needBootstrap() const;
    std::string getDbPath() const;

    rocksdb::DB *db_ = {};
    const Config config_;
    std::vector<rocksdb::ColumnFamilyDescriptor> cfd_;
    std::vector<rocksdb::ColumnFamilyHandle *> cfh_;
};


} // ns
