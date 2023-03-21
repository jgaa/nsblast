#pragma once

#include "nsblast/nsblast.h"
#include "nsblast/DnsMessages.h"
#include "nsblast/util.h"

namespace nsblast {

/*! Interface for resource lookups and manipulation
 *
 *  The idea is to use a generic interface in front of the database,
 *  so we can add alternative databases and caches in the future.
 */
class ResourceIf {
public:
    enum class Category {
        ZONE,
        ENTRY
    };


    class TransactionIf {
    public:
        using key_t = boost::span<const char>;
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
        virtual RrAndSoa lookupEntryAndSoa(key_t fqdn) = 0;

        /*! Get the entry for a fqdn
         *
         *  \return EntryWithBuffer that may or may not be empty. If it is empty,
         *          the key was not found.
         */
        virtual EntryWithBuffer lookup(key_t fqdn) = 0;

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
        virtual read_ptr_t read(key_t key, Category category = Category::ENTRY) = 0;
        virtual void read(key_t key, std::string& buffer, Category category = Category::ENTRY) = 0;

        virtual void commit() = 0;
        virtual void rollback() = 0;

        const auto&  uuid() const noexcept {
            return uuid_;
        }

    private:
        const boost::uuids::uuid uuid_ = lib::newUuid();
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

    ResourceIf() = default;
    virtual ~ResourceIf() = default;

    virtual std::unique_ptr<TransactionIf> transaction() = 0;
};


} // ns

std::ostream& operator << (std::ostream& o, const nsblast::ResourceIf::Category& cat);

