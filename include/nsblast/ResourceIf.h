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
    class TransactionIf {
    public:
        using key_t = boost::span<const char>;
        using data_t = boost::span<const char>;

        struct BufferBase
        {
            data_t data() {
                return data_;
            }

        protected:
            data_t data_;
        };

        using read_ptr_t = std::unique_ptr<BufferBase>;

        TransactionIf() = default;
        virtual ~TransactionIf() = default;

        /*! Check if an RR exists */
        virtual bool exists(std::string_view fqdn, uint16_t type = QTYPE_ALL) = 0;

        /*! Check if a Zone exists */
        virtual bool zoneExists(std::string_view fqdn) {
            return exists(fqdn, TYPE_SOA);
        }

        virtual bool keyExists(key_t key) = 0;

        /*! Add or update an entry
         *
         *  \param key Binary key
         *  \param data Binary data
         *  \param isNew true if a new entry is written
         *
         *  \throws AlreadyExistException is isNew is true, and the entry
         *          already exists.
         *  \throws std::runtime_error on errors
         */
        virtual void write(key_t key, data_t data, bool isNew) = 0;

        /*! Delete an entry
         *
         *  \param key Entry to remove
         *  \param recursive If true, all keys that match key; as
         *         in memcmp(key.data(), other.data(), key.size()) == 0
         *         will be removed. This is primarily to allow
         *         a zone to be deleted.
         */
        virtual void remove(key_t key, bool recursive = false) = 0;

        virtual read_ptr_t read(key_t key) = 0;

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
