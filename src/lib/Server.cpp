
#include <format>

#include <boost/json.hpp>

#include "nsblast/DnsEngine.h"
#include "nsblast/logging.h"
#include "nsblast/util.h"
#include "Notifications.h"
#include "RestApi.h"
#include "SlaveMgr.h"
#include "RocksDbResource.h"
#include "AuthMgr.h"
#include "BackupMgr.h"

#ifdef NSBLAST_CLUSTER
#   include "PrimaryReplication.h"
#   include "FollowerReplication.h"
#   include "GrpcPrimary.h"
#   include "GrpcFollow.h"
#endif

#include "nsblast/Server.h"
#include "yahat/HttpServer.h"

#ifdef NSBLAST_WITH_SWAGGER
#   include "swagger_res.h"
#endif

#ifdef NSBLAST_WITH_UI
#   include "ui_res.h"
#endif

using namespace std;
using namespace std::string_literals;

std::ostream& operator << (std::ostream& out, const nsblast::Server::Role& role) {
    array<string_view, 3> names = {"NONE", "CLUSTER_PRIMARY", "CLUSTER_FOLLOWER"};
    return out << names.at(static_cast<size_t>(role));
}

std::ostream& operator << (std::ostream& out, const nsblast::Server::VersionInfo& v) {
    out << v.appname << ": " << v.nsblast << endl;
    for(const auto& [name, value]: v.components) {
        out << name << ": " << value << endl;
    }
    return out;
}


namespace nsblast {
using namespace ::nsblast::lib;
using namespace yahat;

namespace {
string cppStrandard() {
    if constexpr (__cplusplus == 202302L)
        return "C++23";
    if constexpr (__cplusplus > 202002L && __cplusplus < 202302)
        return "C++23 (partially)";
    if constexpr (__cplusplus == 202002L)
        return "C++20";
    if constexpr (__cplusplus == 201703L)
        return "C++17";

    return std::format("unknown ({})", __cplusplus);
}

// This class works directly with an embedded resource
// generated with mkres. Just point it to the static
// class with the resources.
template <typename T>
class EmbeddedResHandler : public RequestHandler {
public:
    explicit EmbeddedResHandler(std::string prefix)
        : prefix_{std::move(prefix)} {}

    Response onReqest(const Request& req) override {
        // Remove prefix
        auto t = std::string_view{req.target};
        if (t.size() < prefix_.size()) {
            throw std::runtime_error{"Invalid target. Cannot be shorter than prefix!"};
        }

        t = t.substr(prefix_.size());

        while(!t.empty() && t.front() == '/') {
            t = t.substr(1);
        }

        if (t.empty()) {
            t = {"index.html"};
        }

        if (const auto& data = T::get(t); !data.empty()) {
            std::filesystem::path served = prefix_;
            served /= t;
            // TODO: Fix yahat so we can send a lambda to feed it with chunks.
            return {200, "OK", data.toString(), served.string()};
        }

        return {404, "Document not found"};
    }

private:
    const std::string prefix_;
};

} // anon ns

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

Server::VersionInfo Server::getVersionInfo()
{
    VersionInfo v;
    v.appname = NSBLAST_APPNAME;
    v.nsblast = NSBLAST_VERSION;
    v.components.emplace_back("Boost", BOOST_LIB_VERSION);
    v.components.emplace_back("RocksDB", rocksdb::GetRocksVersionAsString());
    v.components.emplace_back("C++ standard", cppStrandard());
    v.components.emplace_back("Platform", BOOST_PLATFORM);
    v.components.emplace_back("Compiler", BOOST_COMPILER);
    v.components.emplace_back("Build date", __DATE__);

    return v;
}

void Server::start()
{
    startRocksDb();
    startAuth();

#ifdef NSBLAST_CLUSTER
    startReplicationAndRpc();
#endif

    startApi();
    startSlaveMgr();
    startHttpServer();
    startIoThreads();
    startNotifications();
    startDns();
    startBackupMgr();

    runWorker("main thread");
}

void Server::resetAuth()
{
    startRocksDb();
    startAuth();

    LOG_WARN << "Resetting the 'admin' user and the 'nsblast' tenant to it's initial, default state.";
    try {
        auth_->deleteTenant("nsblast");
    } catch(const NotFoundException&) {
        ;
    }

    auth_->bootstrap();
}

void Server::startRocksDb(bool init)
{
    auto rdb = make_shared<lib::RocksDbResource>(config());

    if (init) {
        LOG_DEBUG << "Initializing RocksDB";
        rdb->init();

        bootstrapped_ = rdb->wasBootstrapped();
    } else {
        //rdb->prepareDirs();
    }
    resource_ = rdb;
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

    http_ = make_shared<yahat::HttpServer>(config().http, [this](const AuthReq& ar) {
            return auth_->authorize(ar);
    }, "nsblast "s + NSBLAST_VERSION);

    http_->addRoute("/api/v1", api_);
#ifdef NSBLAST_WITH_SWAGGER
    if (config().swagger) {
        const string_view swagger_path = "/api/swagger";
        LOG_INFO << "Enabling Swagger at http/https://<fqdn>[:port]" << swagger_path;

        http_->addRoute(swagger_path,
                        make_shared<EmbeddedResHandler<lib::embedded::Swagger>>("/api/swagger"));
    }
#endif

#ifdef NSBLAST_WITH_UI
    if (config().ui) {
        const string_view ui_path = "/ui";
        LOG_INFO << "Enabling ui at http/https://<fqdn>[:port]" << ui_path;

        http_->addRoute(ui_path,
                        make_shared<EmbeddedResHandler<lib::embedded::Ui>>(ui_path));
    }
#endif

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

    if (wasBootstrapped()) {
        auth_->bootstrap();
    }
}

void Server::startBackupMgr(bool startAutoBackups )
{
    backup_ = make_shared<BackupMgr>(*this);

    if (startAutoBackups) {
        backup_->initAutoBackup();
    }
}

#ifdef NSBLAST_CLUSTER
void Server::initReplication()
{
    if (config_.cluster_role == "primary") {
        role_ = Role::CLUSTER_PRIMARY;
    } else if (config_.cluster_role == "follower") {
        role_ = Role::CLUSTER_FOLLOWER;
    }
}

void Server::StartReplication()
{
    LOG_INFO << "This instances cluster-role is " << role();

    if (isPrimaryReplicationServer()) {
        primary_replication_ = make_shared<PrimaryReplication>(*this);
        primary_replication_->start();
    }

    if (isReplicationFollower()) {
        follower_replication_ = make_shared<FollowerReplication>(*this);
        follower_replication_->start();
    }
}

void Server::startGrpcService()
{

    if (isPrimaryReplicationServer()) {
        grpc_primary_ = make_shared<GrpcPrimary>(*this);
        grpc_primary_->start();
    }

    if (isReplicationFollower()) {
        grpc_follow_ = make_shared<GrpcFollow>(*this);
        grpc_follow_->start();
    }
}

void Server::startReplicationAndRpc()
{
    initReplication();

    if (isPrimaryReplicationServer()) {
        StartReplication();
        grpc_primary_ = make_shared<GrpcPrimary>(*this);
        grpc_primary_->start();

        // In the primary, we enable the transaction callback for the database
        // and link committed transactions to the replication framework.
        startForwardingTransactionsToReplication();
    }

    if (isReplicationFollower()) {
        // work-around for cluster functional test.
        // As of now, if a follower starts sync before the primary is ready,
        // the sync fails.
        // TODO: Add re-try loop in replication.
        std::this_thread::sleep_for(chrono::seconds(4));

        grpc_follow_ = make_shared<GrpcFollow>(*this);
        grpc_follow_->start();
        StartReplication();
    }
}

#endif // NSBLAST_CLUSTER

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
#ifdef NSBLAST_CLUSTER
        if (grpc_primary_) {
            LOG_TRACE << "Server::stop(): Stopping gRPC server...";
            grpc_primary_->stop();
            LOG_TRACE << "Server::stop(): Done stopping gRPC server.";
        }
#endif

        LOG_TRACE << "Server::stop(): Stopping Server worker threads ...";
        ctx_.stop();
        LOG_TRACE << "Server::stop(): Done stopping Server worker threads .";
    });
}

RocksDbResource &Server::db() const noexcept
{
    assert(resource_);
    return dynamic_cast<lib::RocksDbResource&>(*resource_);
}


void Server::startForwardingTransactionsToReplication()
{
    db().setTransactionCallback([this](PrimaryReplication::transaction_t && trx) {
        primaryReplication().onTransaction(std::move(trx));
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

void Server::restoreBackup(int id)
{
    return backup().restoreBackup(id);
}

void Server::validateBackup(int id)
{
    return backup().validateBackup(id);
}

void Server::listBackups()
{
    return backup().listBackups();
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

boost::json::object Server::VersionInfo::toJson() const
{
    boost::json::object vi;
    vi["app"] = appname;
    vi["version"] = nsblast;
    for(const auto& [n, v]: components) {
        vi[n] = v;
    }

    return vi;
}

} // ns
