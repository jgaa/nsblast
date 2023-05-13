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


    std::string createTenant(pb::Tenant& tenant);
    void upsertTenant(std::string_view tenantId, const pb::Tenant& tenant, bool merge);

    /*! Delete an existing tenant
     *
     *  This method will enumerate all resources owned by the tenant,
     *  like zones, users, roles, and delete them all. It will try to
     *  delete everything in one atomic transaction to the database.
     *
     * \param tenantId ID of the tenant
     * \throws Exception if there was an internal error.
     */
    void deleteTenant(std::string_view tenantId);

    void addZone(trx_t& trx, std::string_view fqdn, std::string_view tenant);

private:
    Server& server_;
};

} // ns
