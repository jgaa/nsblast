#pragma once

#include <boost/json.hpp>

#include "nsblast/nsblast.h"
#include "nsblast/ResourceIf.h"
#include "yahat/HttpServer.h"

namespace nsblast::lib {

class RestApi : public yahat::RequestHandler {
public:
    struct Parsed {
        std::string_view base;
        std::string_view what;
        std::string_view fqdn;
        std::string_view operation;
    };

    RestApi(const Config& config, ResourceIf& resource);

    // RequestHandler interface
    yahat::Response onReqest(const yahat::Request &req, const yahat::Auth &auth) override;


    Parsed parse(const yahat::Request &req);

    // Internal methods.
    static void validateSoa(const boost::json::value& json);
    static boost::json::value parseJson(std::string_view json);
    static void validateZone(const boost::json::value& json);
    static void build(std::string_view fqdn, uint32_t ttl, StorageBuilder& sb,
                      const boost::json::value json, bool finish = true);

    yahat::Response onZone(const yahat::Request &req, const Parsed& parsed);
    yahat::Response onResourceRecord(const yahat::Request &req, const Parsed& parsed);
private:

    const Config& config_;
    ResourceIf& resource_;
};

} // ns
