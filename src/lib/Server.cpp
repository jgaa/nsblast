
#include "nsblast/DnsEngine.h"
#include "nsblast/ResourceIf.h"
#include "nsblast/logging.h"
#include "nsblast/util.h"
#include "Notifications.h"
#include "RestApi.h"
#include "SlaveMgr.h"
#include "RocksDbResource.h"
#include "AuthMgr.h"

#include "nsblast/Server.h"
#include "yahat/HttpServer.h"

#include "swagger_res.h"

using namespace std;
using namespace std::string_literals;

namespace nsblast {
using namespace ::nsblast::lib;
using namespace yahat;

Server::Server(const Config &config)
    : config_{config}
{
}

Server::~Server()
{
    LOG_DEBUG << "~Server(): Waiting for workers to end...";
    for(auto& thd : workers_) {
        thd.join();
    }
    LOG_DEBUG << "~Server(): Workers have ended.";

    if (ft_http_) {
        LOG_DEBUG << "~Server(): Waiting for HTTP server to end...";
        try {
            ft_http_->get();
        } catch(const exception& ex) {
            LOG_ERROR << "Server::~Server: Got exception from http server: "
                      << ex.what();
        }
        LOG_DEBUG << "~Server(): Done Waiting for HTTP server.";
    }
}

void Server::start()
{
    startRocksDb();
    startAuth();
    startApi();
    startSlaveMgr();
    startHttpServer();
    startIoThreads();
    startNotifications();
    startDns();

    runWorker("main thread");
}

void Server::startRocksDb()
{
    auto rdb = make_shared<lib::RocksDbResource>(config());

    LOG_DEBUG << "Initializing RocksDB";
    rdb->init();

    resource_ = move(rdb);
}

void Server::startIoThreads()
{
    handleSignals();
    for(size_t i = 1; i < config().num_dns_threads; ++i) {
        workers_.emplace_back([this, i] {
            runWorker("worker thread #"s + to_string(i));
        });
    }
}

void Server::startHttpServer()
{
    assert(api_);

     // TODO: Add actual authentication
    http_ = make_shared<yahat::HttpServer>(config().http, [](const AuthReq& ar) {
        Auth auth;
        LOG_DEBUG << "Authenticating - auth header: " << ar.auth_header;
        auth.access = true;
        auth.account = "nobody";
        return auth;
    }, "nsblast "s + NSBLAST_VERSION);

    http_->addRoute("/api/v1", api_);
    if (config_.swagger) {
        const string_view swagger_path = "/api/swagger";
        LOG_INFO << "Enabling Swagger at http/https://<fqdn>[:port]" << swagger_path;

        http_->addRoute(swagger_path,
                            make_shared<EmbeddedHandler<lib::embedded::resource_t>>(
                                lib::embedded::swagger_files_,
                                "/api/swagger"));
    }

    assert(!ft_http_);
    ft_http_ = http_->start();
}

void Server::startApi()
{
    api_ = make_shared<RestApi>(*this);
}

void Server::startSlaveMgr()
{
    assert(resource_);
    slave_ = make_shared<SlaveMgr>(*this);
    slave_->init();
}

void Server::startDns()
{
    assert(resource_);
    dns_ = make_shared<DnsEngine>(*this);
    dns_->start();
}

void Server::startNotifications()
{
    notifications_ = make_shared<Notifications>(*this);
}

void Server::startAuth()
{
    auth_ = make_shared<AuthMgr>(*this);
}

void Server::stop()
{
    LOG_DEBUG << "Server::stop() is called.";
    call_once(stop_once_, [this]{
        LOG_INFO << "Server::stop(): Service is now stopping.";
        done_ = true;
        if (dns_) {
            dns_->stop();
        }
        if (http_) {
            LOG_TRACE << "Server::stop(): Stopping HTTP server...";
            http_->stop();
            LOG_TRACE << "Server::stop(): Done stopping HTTP server.";
        }
        LOG_TRACE << "Server::stop(): Stopping Server worker threads ...";
        ctx_.stop();
        LOG_TRACE << "Server::stop(): Done stopping Server worker threads ...";
    });
}

uint32_t Server::getNewId()
{
    lock_guard<mutex> lock{ids_mutex_};
    for(auto i = 0; i < 4096; ++i) {
        auto id = getRandomNumber32();
        auto [_, added] = current_request_ids_.emplace(id);
        if (added) {
            return id;
        }
    }

    LOG_WARN << "Server::getNewId(): Failed to aquire an unused ID";
    throw runtime_error{"Server::getNewId: Failed to aquire an unused ID"};
}

void Server::idDone(uint32_t id)
{
    lock_guard<mutex> lock{ids_mutex_};
    current_request_ids_.erase(id);
}

void Server::runWorker(const string &name)
{
    LOG_DEBUG << "Server " << name  << " is joining the primary thread-pool.";
    try {
        ctx_.run();
    } catch(const exception& ex) {
        LOG_ERROR << "Server " << name
                  << " caught exception: "
                  << ex.what();
    } catch(...) {
        ostringstream estr;
#ifdef __unix__
        estr << " of type : " << __cxxabiv1::__cxa_current_exception_type()->name();
#endif
        LOG_ERROR << "Server " << name
                  << " caught unknow exception" << estr.str();
    }
    LOG_DEBUG << "Server " << name << " left the primary thread-pool.";
}

void Server::handleSignals()
{
    if (isDone()) {
        return;
    }

    if (!signals_) {
        signals_.emplace(ctx(), SIGINT, SIGQUIT);
        signals_->add(SIGUSR1);
        signals_->add(SIGHUP);
    }

    signals_->async_wait([this](const boost::system::error_code& ec, int signalNumber) {

        if (ec) {
            if (ec == boost::asio::error::operation_aborted) {
                LOG_TRACE << "Server::handleSignals: Handler aborted.";
                return;
            }
            LOG_WARN << "Server::handleSignals Received error: " << ec.message();
            return;
        }

        LOG_INFO << "Server::handleSignals: Received signal #" << signalNumber;
        if (signalNumber == SIGHUP) {
            LOG_WARN << "Server::handleSignals: Ignoring SIGHUP. Note - config is not re-loaded.";
        } else if (signalNumber == SIGQUIT || signalNumber == SIGINT) {
            if (!isDone()) {
                LOG_INFO << "Server::handleSignals: Stopping the services.";
                stop();
            }
            return;
        } else {
            LOG_WARN << "Server::handleSignals: Ignoring signal #" << signalNumber;
        }

        handleSignals();
    });
}

} // ns
