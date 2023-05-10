#pragma once

#include "nsblast/nsblast.h"
#include "nsblast/ResourceIf.h"
#include "proto/nsblast.pb.h"
#include "nsblast/Server.h"


namespace nsblast::lib {

class AuthMgr {
public:
    AuthMgr(Server& server)
        : server_{server} {}

    // Methods to support the REST api

    /*! Get an existing tenant
     *
     *  \param tenantId ID of the tenant
     *
     *  \returns optional with Tenant object if the tenant was found.
     *  \throws std::runtime_error if there was an internal error.
     */
    std::optional<pb::Tenant> getTenant(std::string_view tenantId) const;


    void createTenant(const pb::Tenant& tenant);
    void upsertTenant(const pb::Tenant& tenant);

    /*! Delete an existing tenant
     *
     *  This method will enumerate all resources owned by the tenant,
     *  like zones, users, roles, and delete them all. It will try to
     *  delete everything in one atomic transaction to the database.
     *
     * \param tenantId ID of the tenant
     * \returns true if the tenant was found and deleted. False if the tenant was not found.
     * \throws std::runtime_error if there was an internal error.
     */
    bool deleteTenant(std::string_view tenantId);

private:
    Server& server_;
};

} // ns
