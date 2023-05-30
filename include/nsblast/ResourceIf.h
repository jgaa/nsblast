#pragma once

#include "nsblast/nsblast.h"
#include "nsblast/DnsMessages.h"
#include "nsblast/util.h"

namespace nsblast::lib {

/*! Interface for resource lookups and manipulation
 *
 *  The idea is to use a generic interface in front of the database,
 *  so we can add alternative databases and caches in the future.
 */
class ResourceIf {
public:
    enum class Category {
        DEFAULT,
        MASTER_ZONE,
        ENTRY,
        DIFF,
        ACCOUNT,
        TRXLOG
    };

    /*! Real index key.
     *
     *  For fqdn's, we have to reverse the strings to sort them correctly.
     *
     *  We also add a prefix byte in the key so we can have different data-types/indexes
     *  in the same table/collection.
     */
    class RealKey {
    public:
        enum class Class : char {
            ENTRY,      // class reversed-fqdn
            DIFF,       // class reversed-fqdn serial
            TENANT,     // class tenantId
            USER,       // class loginName
            ROLE,       // unused
            ZONE,       // class reversed-fqdn
            TZONE,      // class tenant-id / fqdn
            TRXID       // class uint64-trxid
        };

        RealKey(span_t key, Class kclass = Class::ENTRY, bool binary = false);
        RealKey(span_t key, uint32_t version, Class kclass = Class::DIFF);
        RealKey(span_t key, span_t postfix, Class kclass);
        RealKey(uint64_t num, Class kclass);

        span_t key() const noexcept;

        size_t size() const noexcept {
            return bytes_.size();
        }

        bool empty() const noexcept;

        static bool isReversed(Class kclass) noexcept;

        Class kClass() const noexcept;

        /*! Return the data-part (not the byte-prefix) as a string.
         *
         *  Typically, this means a fqdn that is re-reversed.
         */
        std::string dataAsString() const;

        auto data() const noexcept {
            return bytes_.data();
        }

        const auto& bytes() const noexcept {
            return bytes_;
        }

        bool isSameFqdn(const RealKey& k) const noexcept;

        bool operator == (const RealKey& k) const noexcept {
            return bytes_ == k.bytes_;
        }

        bool operator != (const RealKey& k) const noexcept {
            return bytes_ != k.bytes_;
        }

        /*! Check if the raw key (from the database) is of the same key class as the instance */
        [[nodiscard]] bool isSameKeyClass(span_t key) const noexcept {
            if (!key.empty() && !bytes_.empty()) {
                return key[0] == bytes_[0];
            }
            return false;
        }

    protected:            
        static std::string init(span_t key, Class kclass, std::optional<uint32_t> version);
        static std::string init(uint64_t value, Class kclass);

        const std::string bytes_;
    };

    class TransactionIf {
    public:
        using key_t = const RealKey&;
        using data_t = boost::span<const char>;

        /// Buffer that can be specialized by derived classes to reduce memory allocations and memcpy's
        struct BufferBase
        {
            data_t data() {
                return data_;
            }

        protected:
            data_t data_;
        };

        using read_ptr_t = std::unique_ptr<BufferBase>;

        /*! Entries retrieved by the Resource
         *
         *  Assume that it owns the underlaying buffer.
         */
        struct EntryWithBuffer : public lib::Entry {
            EntryWithBuffer() = default;
            EntryWithBuffer(const EntryWithBuffer&) = delete;
            EntryWithBuffer(EntryWithBuffer&& v)
                : lib::Entry(std::move(static_cast<lib::Entry&>(v)))
                , buffer_ptr_{std::move(v.buffer_ptr_)}
            {
            }


            EntryWithBuffer& operator = (const EntryWithBuffer&) = delete;
            EntryWithBuffer& operator = (EntryWithBuffer&& v) {
                static_cast<lib::Entry&>(*this) = std::move(static_cast<lib::Entry&>(v));
                buffer_ptr_ = std::move(v.buffer_ptr_);
                return *this;
            }

            EntryWithBuffer(read_ptr_t && buffer)
                : Entry{buffer->data()} {
                buffer_ptr_ = {std::move(buffer)};
            }

            /// Allow if (instance) { }
            explicit operator bool () const noexcept {
                return !Entry::empty();
            }

        protected:
            read_ptr_t  buffer_ptr_;
        };

        /*! Object containing Entry's for the RR matching a key and the entry for the zone
         *
         *  If the RR is the RR for the zone, soa() and rr() will return the same instance.
         */
        struct RrAndSoa {
            RrAndSoa() = default;
            RrAndSoa(const RrAndSoa&) = delete;
            RrAndSoa(RrAndSoa &&) = default;

            RrAndSoa& operator = (const RrAndSoa&) = delete;
            RrAndSoa& operator = (RrAndSoa&&) = default;

            RrAndSoa(EntryWithBuffer && soa)
                : rr_{std::move(soa)} {}

            RrAndSoa(EntryWithBuffer && rr, EntryWithBuffer && soa)
                : rr_{std::move(rr)}, soa_{std::move(soa)} {}

            const lib::Entry& soa() const {
                return soa_.empty() ? rr_ : soa_;
            }

            const lib::Entry& rr() const {
                return rr_;
            }

            /*! True if rr()) and soa() will return the same Entry */
            bool isSame() const noexcept {
                return soa_.empty() && !rr_.empty();
            }

            /*! The key was not found */
            bool empty() const noexcept {
                return soa_.empty() && rr_.empty();
            }

            /*! The key was not found */
            operator bool() const noexcept {
                return !empty();
            }

            bool hasRr() const noexcept {
                return !rr_.empty();
            }

            bool hasSoa() const noexcept {
                return !empty();
            }

        private:
            EntryWithBuffer rr_;
            EntryWithBuffer soa_;
        };

        TransactionIf() = default;
        virtual ~TransactionIf() = default;

        /*! Callback called when iterating over a range of data
         *
         *  \param key The key for this item
         *  \param span The value value (data) of this item
         *
         *  \return true to continue the iteration, false to abort
         */
        using iterator_fn_t = std::function<bool (key_t key, span_t value)>;

        /*! Iterate over all data-items matching (starting with) key */
        virtual void iterate(key_t, iterator_fn_t fn, Category category = Category::ENTRY) = 0;

        /*! Get the Entry with the soa (zone) for a key.
         *
         *  \param fqdn name to query about. For zone "example.com", this may be
         *         "example.com" or "www.example.com". In both cases, the Entry for
         *         "ewxample.com" is returned as soa.
         *
         *  \return RrAndSoa that may or may not be empty. If it is empty,
         *          the zone was not found.
         *
         */
        virtual RrAndSoa lookupEntryAndSoa(std::string_view fqdn) = 0;

        /*! Get the entry for a fqdn
         *
         *  \return EntryWithBuffer that may or may not be empty. If it is empty,
         *          the key was not found.
         */
        virtual EntryWithBuffer lookup(std::string_view fqdn) = 0;

        /*! Check if an RR exists */
        virtual bool exists(std::string_view fqdn, uint16_t type = QTYPE_ALL) = 0;

        /*! Check if a Zone exists */
        virtual bool zoneExists(std::string_view fqdn) {
            return exists(fqdn, TYPE_SOA);
        }

        virtual bool keyExists(key_t key, Category category = Category::ENTRY) = 0;

        /*! Add or update an entry
         *
         *  \param key Binary key
         *  \param data Binary data
         *  \param isNew true if a new entry is written
         *  \param category In what category to execute the command.
         *
         *  \throws AlreadyExistException is isNew is true, and the entry
         *          already exists.
         *  \throws std::runtime_error on errors
         */
        virtual void write(key_t key, data_t data, bool isNew, Category category = Category::ENTRY) = 0;

        /*! Delete an entry
         *
         *  \param key Entry to remove
         *  \param recursive If true, all keys that match key; as
         *         in memcmp(key.data(), other.data(), key.size()) == 0
         *         will be removed. This is primarily to allow
         *         a zone to be deleted.
         *  \param category In what category to execute the command.
         */
        virtual void remove(key_t key, bool recursive = false, Category category = Category::ENTRY) = 0;

        /*! Low level read */
        virtual read_ptr_t read(key_t key, Category category = Category::ENTRY, bool throwIfNoeExixt = true) = 0;
        virtual bool read(key_t key, std::string& buffer, Category category = Category::ENTRY, bool throwIfNoeExixt = true) = 0;

        virtual void commit() = 0;
        virtual void rollback() = 0;

        const auto&  uuid() const noexcept {
            return uuid_;
        }

    private:
        const boost::uuids::uuid uuid_ = lib::newUuid();
    };

//    class AlreadyExistException : public std::runtime_error {
//    public:
//        AlreadyExistException(const std::string& what) noexcept
//            : std::runtime_error(what) {}
//    };

//    class NotFoundException : public std::runtime_error {
//    public:
//        NotFoundException(const std::string& what) noexcept
//            : std::runtime_error(what) {}
//    };

    ResourceIf() = default;
    virtual ~ResourceIf() = default;

    virtual std::unique_ptr<TransactionIf> transaction() = 0;
};

using trx_t = ResourceIf::TransactionIf;

} // ns

std::ostream& operator << (std::ostream& o, const nsblast::lib::ResourceIf::Category& cat);
std::ostream& operator << (std::ostream& o, const nsblast::lib::ResourceIf::RealKey& key);

