#pragma once

#include <boost/json.hpp>

#include "nsblast/nsblast.h"
#include "nsblast/ResourceIf.h"
#include "yahat/HttpServer.h"
#include "nsblast/Server.h"
#include "proto/nsblast.pb.h"

namespace nsblast::lib {

class RestApi : public yahat::RequestHandler {
public:
    struct Parsed {
        std::string_view base;
        std::string_view what;
        std::string_view target;
        std::string_view operation;
    };

    RestApi(Server& server);

    RestApi(const Config &config, ResourceIf& resource);


    // RequestHandler interface
    yahat::Response onReqest(const yahat::Request &req) override;


    Parsed parse(const yahat::Request &req);

    // Internal methods.
    static void validateSoa(const boost::json::value& json);
    static boost::json::value parseJson(std::string_view json);
    static void validateZone(const boost::json::value& json);
    static void build(std::string_view fqdn, uint32_t ttl, StorageBuilder& sb,
                      const boost::json::value& json, bool finish = true);

    yahat::Response onTenant(const yahat::Request &req, const Parsed& parsed);
    yahat::Response onRole(const yahat::Request &req, const Parsed& parsed);
    yahat::Response onPermissions(const yahat::Request &req, const Parsed& parsed);
    yahat::Response onUser(const yahat::Request &req, const Parsed& parsed);
    yahat::Response onZone(const yahat::Request &req, const Parsed& parsed);
    yahat::Response onResourceRecord(const yahat::Request &req, const Parsed& parsed);
    yahat::Response onConfigMaster(const yahat::Request &req, const Parsed& parsed);
    yahat::Response onBackup(const yahat::Request &req, const Parsed& parsed);
    yahat::Response onVersion(const yahat::Request &req, const Parsed& parsed);
    void checkSrv(span_t span, ResourceIf::TransactionIf& trx);
    bool hasAccess(const yahat::Request& req, pb::Permission) const noexcept;
    bool hasAccess(const yahat::Request& req, std::string_view lowercaseFqdn, pb::Permission) const noexcept;
    yahat::Response listTenants(const yahat::Request &req, const Parsed& parsed);
    yahat::Response listZones(const yahat::Request &req, const Parsed& parsed);
    yahat::Response listZone(const yahat::Request &req, const Parsed& parsed);
private:
    size_t getPageSize(const yahat::Request &req) const;
    std::string_view getFrom(const yahat::Request &req) const;
    // Forward is true, backwards is false
    bool getDirection(const yahat::Request &req) const;

    Server& server() noexcept {
        assert(server_);
        return *server_;
    }

    const Server& server() const noexcept {
        assert(server_);
        return *server_;
    }

    // Returns false if there was a probem with the replication, including
    // timeout.
    std::optional<bool> waitForReplication(const yahat::Request &req, uint64_t trxid);
    yahat::Response startBackup(const yahat::Request &req, const Parsed& parsed);
    yahat::Response verifyBackup(const yahat::Request &req, const Parsed& parsed);
    yahat::Response listBackups(const yahat::Request &req, const Parsed& parsed);
    yahat::Response deleteBackups(const yahat::Request &req, const Parsed& parsed);

    void refactorZone(boost::json::object& zone);

    const Config& config_;
    ResourceIf& resource_;
    Server *server_ = {};
};

} // ns
