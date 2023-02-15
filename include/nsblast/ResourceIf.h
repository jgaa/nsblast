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
        TransactionIf() = default;
        virtual ~TransactionIf() = default;

        /*! Check if an RR exists */
        virtual bool exists(std::string_view fqdn, uint16_t type = QTYPE_ALL) = 0;

        /*! Check if a Zone exists */
        virtual bool zoneExists(std::string_view fqdn) {
            return exists(fqdn, TYPE_SOA);
        }

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
