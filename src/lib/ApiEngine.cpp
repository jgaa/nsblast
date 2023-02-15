


#include "nsblast/ApiEngine.h"
#include "nsblast/logging.h"

#include "swagger_res.h"

namespace nsblast {

using namespace std;
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



    if (config_.swagger) {
        const string_view swagger_path = "/api/swagger";
        LOG_INFO << "Enabling Swagger at http/https://<fqdn>[:port]" << swagger_path;
        httpServer.addRoute(swagger_path,
                            make_shared<EmbeddedHandler<lib::embedded::resource_t>>(
                                lib::embedded::swagger_files_,
                                "/api/swagger"));
    }

    return httpServer.run();
}



} // ns
