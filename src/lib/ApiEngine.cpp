
#include "nsblast/ApiEngine.h"
#include "nsblast/logging.h"

namespace nsblast {

using namespace yahat;

ApiEngine::ApiEngine(const Config& config)
    : config_{config}
{

}

void ApiEngine::run()
{
    // TODO: Add actual authentication
    yahat::HttpServer httpServer{config_.http, [](const AuthReq& ar) {
            Auth auth;
            LOG_DEBUG << "Authenticating - auth header: " << ar.auth_header;
            auth.access = true;
            auth.account = "nobody";
            return auth;
        }};

    return httpServer.run();
}



} // ns
