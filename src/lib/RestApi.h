#pragma once

#include "nsblast/nsblast.h"
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

    RestApi(const Config& config);

    // RequestHandler interface
    yahat::Response onReqest(const yahat::Request &req, const yahat::Auth &auth) override;


    Parsed parse(const yahat::Request &req);
private:

    yahat::Response onZone(const yahat::Request &req, const Parsed& parsed);
    yahat::Response onResourceRecord(const yahat::Request &req, const Parsed& parsed);

    const Config& config_;
};

} // ns
