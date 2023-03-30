
#include "RocksDbResource.h"
#include "nsblast/logging.h"
#include "nsblast/util.h"

using namespace std;
using namespace std::string_literals;
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

template <typename T>
auto toSlice(const T& v) {
    return  rocksdb::Slice{v.data(), v.size()};
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
        throw runtime_error{"Failed to start transaction"};
    }

    // Nice to have the same name in rocksdb logs
    trx_->SetName(boost::uuids::to_string(uuid()));
}

RocksDbResource::Transaction::~Transaction()
{
    if (trx_) {
        rollback();
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

                    return {move(e)};
                }

                return {move(rr), move(e)};
            }

            auto flags = e.flags();
            const auto zlen = e.header().zonelen;
            assert(zlen > 0);
            assert(zlen <= key.size());

            // Deduce the key to the zone/soa from the zonelen in the RR's header
            key = key.substr(key.size() - zlen);

            if (first) {
                assert(rr.empty());
                // Remember rr, we need to return it when the soa is found.
                rr = move(e);
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
        auto buffer = read(key_t{fqdn});
        return {move(buffer)};
    }  catch (const ResourceIf::NotFoundException&) {
        ;
    }

    return {}; // Not found
}

void RocksDbResource::Transaction::iterate(ResourceIf::TransactionIf::key_t key,
                                           ResourceIf::TransactionIf::iterator_fn_t fn,
                                           Category category)
{
    auto it = make_unique_from(trx_->GetIterator({}, owner_.handle(category)));
    for(it->Seek(toSlice(key)); it->Valid(); it->Next()) {
        if (!fn({it->key(), key.kClass(), true}, it->value())) {
            return;
        }
    }
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
        auto b = read(key_t{fqdn});
        Entry entry{b->data()};

        if (type == TYPE_SOA) {
            return entry.flags().soa;
        }

        for(auto it : entry) {
            if (it.type() == type) {
                return true;
            }
        }

    }  catch (const ResourceIf::NotFoundException&) {
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
        throw runtime_error{"Rocksdb write failed: "s + status.ToString()};
    }

    if (isNew && status.IsOkOverwritten()) { // TODO: don't actually work...
        throw AlreadyExistException{"Key exists"};
    }

    dirty_ = true;
}

ResourceIf::TransactionIf::read_ptr_t
RocksDbResource::Transaction::read(ResourceIf::TransactionIf::key_t key, Category category)
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
        throw NotFoundException{"Key not found"};
    }

    LOG_WARN << "RocksDbResource::Transaction::read - Read from transaction "
              << uuid() << " key: " << key
              << ", category " << category
              << " failed with status: " << status.ToString();

    throw runtime_error{status.ToString()};
}

void RocksDbResource::Transaction::read(ResourceIf::TransactionIf::key_t key, string &buffer, ResourceIf::Category category)
{
    LOG_TRACE << "RocksDbResource::Transaction::read (string) - Read from transaction "
              << uuid() << " key: " << key
              << ", category " << category;

    const auto status = trx_->Get({}, owner_.handle(category), toSlice(key.key()), &buffer);

    if (status.ok()) {
        return;
    }

    if (status.IsNotFound()) {
        throw NotFoundException{"Key not found"};
    }

    LOG_WARN << "RocksDbResource::Transaction::read (string) - Read from transaction "
              << uuid() << " key: " << key
              << ", category " << category
              << " failed with status: " << status.ToString();
    throw runtime_error{status.ToString()};
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
        auto it = trx_->GetIterator(options, owner_.handle(category));
        for(it->Seek(toSlice(key.key())); it->Valid() ; it->Next()) {
            const auto ck = it->key();
            const int extra_len = ck.size() - key.size();
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
        }
    } else {
        LOG_TRACE << "RocksDbResource::Transaction::remove Removing key "
                  << key << ", category " << category;

        trx_->Delete(owner_.handle(category), toSlice(key));
    }

    dirty_ = true;
}

void RocksDbResource::Transaction::commit()
{
    call_once(once_, [&] {
        LOG_TRACE << "Committing transaction " << uuid();
        auto status = trx_->Commit();
        if (!status.ok()) {
            LOG_ERROR << "Transaction " << uuid() << " failed: " << status.ToString();
            throw runtime_error{"Failed to commit transaction"};
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
            throw runtime_error{"Failed to rollback transaction"};
        }
    });
}

RocksDbResource::RocksDbResource(const Config &config)
    : config_{config}
{
    rocksdb::ColumnFamilyOptions o;
    cfd_.emplace_back(rocksdb::kDefaultColumnFamilyName, o);
    cfd_.emplace_back("zone", o);
    cfd_.emplace_back("entry", o);
    cfd_.emplace_back("diff", o);
    cfd_.emplace_back("account", o);
}

std::unique_ptr<ResourceIf::TransactionIf> RocksDbResource::transaction()
{
    return make_unique<Transaction>(*this);
}

void RocksDbResource::init()
{
    prepareDirs();
    if (needBootstrap()) {
        bootstrap();
    } else {
        open();
    }
}

rocksdb::ColumnFamilyHandle *RocksDbResource::handle(const ResourceIf::Category category) {
    const auto ix = static_cast<size_t>(category) + 1;
    if (ix >= ZONE && ix <= DIFF) {
        return cfh_[ix];
    }

    throw runtime_error{"handle: Unknown RocksDB Category "s + std::to_string(ix -1)};
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

void RocksDbResource::bootstrap()
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


} // ns
