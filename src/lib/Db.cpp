
#include <filesystem>
#include <chrono>
#include <thread>

#include "nsblast/logging.h"
#include "Db.h"

using namespace std;
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

namespace {

tuple<rocksdb::Status, string /* data */> validate(string_view what, Db::Transaction& trx,
              rocksdb::ColumnFamilyHandle *handle,
              string_view fqdn, std::optional<bool> isNew) {

    string v;
    const auto status = trx->GetForUpdate({}, handle, fqdn, &v);

    if (isNew) {
        if (*isNew) {
            // Must be new
            if (!status.IsNotFound()) {
                LOG_WARN << what << ' ' << fqdn << " already exist!";
                throw Db::AlreadyExistException{string{fqdn}};
            }
        } else {
            // Must exist
            if (status.IsNotFound()) {
                LOG_WARN << what << ' ' << fqdn << " not found!";
                throw Db::NotFoundException{string{fqdn}};
            }
        }
    }

    if (!status.ok() && !status.IsNotFound()) {
        LOG_ERROR << what << ' '  << fqdn << " error: " << status.ToString();
        throw runtime_error{"Database error"};
    }

    return make_tuple(status, v);
}

} // ns


Db::Db(const Config &config)
    : config_{config}
{
    rocksdb::ColumnFamilyOptions o;
    cfd_.emplace_back(rocksdb::kDefaultColumnFamilyName, o);
    cfd_.emplace_back("zone", o);
    cfd_.emplace_back("entry", o);
}

Db::~Db()
{
    if (db_) {

        db_->DropColumnFamily(zoneHandle());
        db_->DropColumnFamily(entryHandle());

        for(auto& h : cfh_) {
            if (h) {
                 db_->DestroyColumnFamilyHandle(h);
            }
        }

        for(auto s = db_->Close(); !s.ok();) {
            LOG_WARN << "Failed to close the database: " << s.ToString();
            this_thread::sleep_for(100ms);
        }
        delete db_;
        db_ = {};
    }
}

void Db::init()
{
    prepareDirs();
    if (needBootstrap()) {
        bootstrap();
    } else {
        open();
    }
}

std::optional<Zone> Db::getZone(std::string_view fqdn)
{
    LOG_TRACE << "Getting zone " << fqdn;

    string v;
    const auto status = db_->Get(ReadOptions{}, zoneHandle(), fqdn, &v);
    if (!status.ok()) {
        LOG_DEBUG << "Failed to get zone" << fqdn << ": " << status.ToString();
        return {};
    }

    Zone z;
    z.ParseFromString(v);
    return z;
}

std::optional<std::tuple<string, Zone> > Db::findZone(std::string_view fqdn)
{
    auto target = fqdn;

    while(!target.empty()) {
        if (auto z = getZone(target)) {
            return make_tuple(string{target}, *z);
        }

        if (const auto pos = target.find('.') ; pos != string_view::npos) {
            target = target.substr(pos + 1);
        } else {
            break;
        }
    }

    return {};
}

void Db::deleteZone(std::string_view fqdn)
{
    LOG_INFO << "Deleting zone " << fqdn;

    Transaction trx(*this);

    string v;
    auto status = trx->GetForUpdate({}, zoneHandle(), fqdn, &v);
    if (status.IsNotFound()) {
        throw NotFoundException{string{fqdn}};
    }

    status = trx->Delete(zoneHandle(), fqdn);
    if (!status.ok()) {
        LOG_ERROR << "Failed to delete: " << fqdn << ": " << status.ToString();
    }

    trx.commit();
}

void Db::writeZone(string_view fqdn, Zone &zone,
                   std::optional<bool> isNew,
                   bool mergeExisting)
{
    LOG_DEBUG << "Writing zone " << fqdn;

    Transaction trx(*this);
    bool is_update = false;

    {
        const auto [get_status, data] = validate("Zone",
                                         trx,
                                         zoneHandle(),
                                         fqdn,
                                         isNew);

        if (get_status.ok()) {
            is_update = true;
            Zone old;
            old.ParseFromString(data);

            const auto new_serial = zone.soa().serial() ? 0 : old.soa().serial() + 1;

            unique_ptr<Soa> new_soa;

            if (mergeExisting) {
                new_soa = make_unique<Soa>(old.soa());
                new_soa->MergeFrom(zone.soa());

                if (zone.ns().empty() && !old.ns().empty()) {
                    for(size_t i = 0; i < old.ns_size(); ++i) {
                        zone.add_ns()->CopyFrom(old.ns().at(i));
                    }
                }

                if (zone.tns().empty()) {
                    zone.set_tns(old.tns());
                }
            } else {
                new_soa = make_unique<Soa>(zone.soa());
            }

            if (new_serial) {
                // Increment version
                new_soa->set_serial(new_serial);
            }

            zone.set_allocated_soa(new_soa.release());
        }
    }

    string z;
    zone.SerializeToString(&z);
    const auto status = trx->Put(zoneHandle(), fqdn, z);

    if (!is_update && status.IsOkOverwritten()) {
        // Somehow someone else managed to add the key before us, so abort now.
        LOG_WARN << LOG_WARN << "Zone " << fqdn << " already existed!";
        throw AlreadyExistException{string{fqdn}};
    }

    if (!status.ok()) {
        LOG_ERROR << "Failed to save zone" << fqdn << ": " << status.ToString();
        throw runtime_error{"Write error"};
    }

    trx.commit();

    if (!is_update) {
        LOG_INFO << "Added zone " << fqdn;
    }
}

void Db::incrementZone(Transaction& trx, std::string_view fqdn)
{
    // TODO: Put the zone serial in a serapare key as it may
    //       increment frequently
    string data;
    auto status = trx->GetForUpdate({}, zoneHandle(), fqdn, &data);
    if (!status.ok()) {
        LOG_ERROR << "Failed to get (for serial-no update) zone " << fqdn
                  << ": " << status.ToString();
        throw runtime_error{"Zone error"};
    }

    Zone z;
    z.ParseFromString(data);
    auto soa = make_unique<Soa>(z.soa());
    soa->set_serial(soa->serial() + 1);
    z.set_allocated_soa(soa.release());
    z.SerializeToString(&data);
    status = trx->Put(zoneHandle(), fqdn, data);

    if (!status.ok()) {
        LOG_ERROR << "Failed to save zone" << fqdn << ": " << status.ToString();
        throw runtime_error{"Write error"};
    }

    LOG_DEBUG << "Incremented zone " << fqdn << ": " << z.soa().serial();
}

std::optional<Rr> Db::getRr(std::string_view fqdn)
{
    LOG_TRACE << "Getting Rr " << fqdn;

    string v;
    const auto status = db_->Get(ReadOptions{}, entryHandle(), fqdn, &v);
    if (!status.ok()) {
        LOG_DEBUG << "Failed to get Rr" << fqdn << ": " << status.ToString();
        return {};
    }

    Rr r;
    r.ParseFromString(v);
    return r;
}

void Db::deleteRr(std::string_view zone, std::string_view fqdn, std::string_view type)
{
    LOG_TRACE << "Deleting Rr " << fqdn;

    assert(type.empty());

    Transaction trx(*this);

    string v;
    auto status = trx->GetForUpdate({}, entryHandle(), fqdn, &v);
    if (status.IsNotFound()) {
        throw NotFoundException{string{fqdn}};
    }

    status = trx->Delete(entryHandle(), fqdn);
    if (!status.ok()) {
        LOG_ERROR << "Failed to delete Rr: " << fqdn << ": " << status.ToString();
    }

    incrementZone(trx, zone);
    trx.commit();
}

void Db::writeRr(std::string_view zone, std::string_view fqdn, Rr &rr, std::optional<bool> isNew, bool mergeExisting)
{
    LOG_DEBUG << "Writing zone " << fqdn;

    Transaction trx(*this);
    bool is_update = false;

    {
        const auto [get_status, data] = validate("Rr",
                                         trx,
                                         entryHandle(),
                                         fqdn,
                                         isNew);
        if (get_status.ok()) {
            is_update = true;
            Rr old;
            old.ParseFromString(data);

            if (mergeExisting) {
               if (!rr.a_size()) {
                   for (const auto& v : old.a()) {
                       rr.add_a(v);
                   }
               }
               if (!rr.aa_size()) {
                   for (const auto& v : old.aa()) {
                       rr.add_aa(v);
                   }
               }
               if (!rr.has_cname() && old.has_cname()) {
                   rr.set_cname(old.cname());
               }
               if (!rr.has_txt() && old.has_txt()) {
                   rr.set_txt(old.txt());
               }
               if (old.has_mx()) {
                   auto mx = make_unique<Mx>();
                   if (rr.has_mx()) {
                       *mx = rr.mx();
                   }
                   if (!mx->ttl()) {
                       mx->set_ttl(old.mx().ttl());
                   }
                   if (!mx->priority()) {
                       mx->set_priority(old.mx().priority());
                   }
                   if (mx->host().empty()) {
                       mx->set_host(old.mx().host());
                   }

                   rr.set_allocated_mx(mx.release());
               }
            }
        }
    }

    string r;
    rr.SerializeToString(&r);
    const auto status = trx->Put(entryHandle(), fqdn, r);

    if (!is_update && status.IsOkOverwritten()) {
        // Somehow someone else managed to add the key before us, so abort now.
        LOG_WARN << LOG_WARN << "Rr " << fqdn << " already existed!";
        throw AlreadyExistException{string{fqdn}};
    }

    if (!status.ok()) {
        LOG_ERROR << "Failed to save zone" << fqdn << ": " << status.ToString();
        throw runtime_error{"Write error"};
    }

    incrementZone(trx, zone);
    trx.commit();

    if (!is_update) {
        LOG_INFO << "Added Rr " << fqdn << " to zone " << zone;
    }
}

void Db::prepareDirs()
{
    if (!filesystem::is_directory(config_.db_path)) {
        LOG_INFO << "Creating directory: " << config_.db_path;
        filesystem::create_directories(config_.db_path);
    }
}

void Db::open()
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

void Db::bootstrap()
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

bool Db::needBootstrap() const
{
    return !filesystem::is_directory(getDbPath());
}

string Db::getDbPath() const
{
    filesystem::path p = config_.db_path;
    p /= "rocksdb";
    return p.string();
}


Db::Transaction::Transaction(Db &db)
    : trx_{db.db_->BeginTransaction({})}
{
}

Db::Transaction::~Transaction()
{
    rollback();
}

void Db::Transaction::rollback()
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

void Db::Transaction::commit()
{
    call_once(once_, [&] {
        LOG_TRACE << "Committing transaction";
        auto status = trx_->Commit();
        if (!status.ok()) {
            LOG_ERROR << "Transaction failed: " << status.ToString();
            throw runtime_error{"Failed to commit transaction"};
        }
    });
}


} // ns
