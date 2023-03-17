
#include <boost/unordered/unordered_flat_map.hpp>
#include "nsblast/ApiEngine.h"
#include "RocksDbResource.h"
#include "RestApi.h"
#include "SlaveMgr.h"
#include "nsblast/logging.h"

#include "swagger_res.h"

namespace nsblast {

using namespace std;
using namespace yahat;


ApiEngine::ApiEngine(const Config& config)
    : config_{config}
{
}

void ApiEngine::initRocksdb()
{
    auto rdb = make_shared<lib::RocksDbResource>(config_);

    LOG_DEBUG << "Initializing RocksDB";
    rdb->init();

    resource_ = move(rdb);
}

void ApiEngine::run()
{
    if (!resource_) {
        initRocksdb();
    }

    // TODO: Add actual authentication
    yahat::HttpServer httpServer{config_.http, [](const AuthReq& ar) {
            Auth auth;
            LOG_DEBUG << "Authenticating - auth header: " << ar.auth_header;
            auth.access = true;
            auth.account = "nobody";
            return auth;
        }, "nsblast "s + NSBLAST_VERSION};

    // TODO: Move the ownership somewhere it can use the ctx (trhreads) from the DnsEngine
    slave_mgr_ = make_shared<lib::SlaveMgr>(config_, *resource_, httpServer.getCtx());

    httpServer.addRoute("/api/v1", make_shared<lib::RestApi>(*this));

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
