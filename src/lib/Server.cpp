
#include <format>
#include <filesystem>

#include <boost/json.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ip/udp.hpp>

#include "nsblast/DnsEngine.h"
#include "nsblast/logging.h"
#include "nsblast/util.h"
#include "Notifications.h"
#include "RestApi.h"
#include "SlaveMgr.h"
#include "RocksDbResource.h"
#include "AuthMgr.h"
#include "BackupMgr.h"
#include "Metrics.h"

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
const auto process_started_at = std::chrono::steady_clock::now();

std::string formatUptime(std::uint64_t total_seconds) {
    const auto days = total_seconds / 86400;
    total_seconds %= 86400;
    const auto hours = total_seconds / 3600;
    total_seconds %= 3600;
    const auto minutes = total_seconds / 60;
    const auto seconds = total_seconds % 60;

    std::string out;
    auto append = [&out](std::uint64_t value, std::string_view unit) {
        if (!out.empty()) {
            out += " ";
        }
        out += std::to_string(value);
        out += unit;
    };

    if (days > 0) {
        append(days, "d");
    }
    if (hours > 0 || days > 0) {
        append(hours, "h");
    }
    if (minutes > 0 || hours > 0 || days > 0) {
        append(minutes, "m");
    }
    append(seconds, "s");

    return out;
}

string_view trim(std::string_view in) {
    constexpr auto ws = " \t\n\r\f\v";
    const auto begin = in.find_first_not_of(ws);
    if (begin == string_view::npos) {
        return {};
    }
    const auto end = in.find_last_not_of(ws);
    return in.substr(begin, end - begin + 1);
}

std::string_view unbracket(std::string_view host) {
    host = trim(host);
    if (host.size() >= 2 && host.front() == '[' && host.back() == ']') {
        host.remove_prefix(1);
        host.remove_suffix(1);
    }
    return host;
}

bool isLoopbackHost(std::string_view endpoint) {
    endpoint = unbracket(endpoint);
    if (endpoint.empty()) {
        return false;
    }

    if (endpoint == "localhost" || endpoint == "127.0.0.1" || endpoint == "::1") {
        return true;
    }

    boost::system::error_code ec;
    const auto addr = boost::asio::ip::make_address(std::string{endpoint}, ec);
    return !ec && addr.is_loopback();
}

bool isWildcardHost(std::string_view endpoint) {
    endpoint = unbracket(endpoint);
    if (endpoint.empty() || endpoint == "*" || endpoint == "0.0.0.0" || endpoint == "::") {
        return true;
    }

    boost::system::error_code ec;
    const auto addr = boost::asio::ip::make_address(std::string{endpoint}, ec);
    return !ec && addr.is_unspecified();
}

std::optional<boost::asio::ip::address> getPrimaryLocalAddress() {
    boost::system::error_code ec;
    boost::asio::io_context io;

    {
        boost::asio::ip::udp::socket socket{io};
        socket.open(boost::asio::ip::udp::v4(), ec);
        if (!ec) {
            socket.connect({boost::asio::ip::make_address_v4("8.8.8.8"), 53}, ec);
            if (!ec) {
                auto addr = socket.local_endpoint(ec).address();
                if (!ec && !addr.is_unspecified() && !addr.is_loopback()) {
                    return addr;
                }
            }
        }
    }

    {
        boost::asio::ip::udp::socket socket{io};
        socket.open(boost::asio::ip::udp::v6(), ec);
        if (!ec) {
            socket.connect({boost::asio::ip::make_address_v6("2001:4860:4860::8888"), 53}, ec);
            if (!ec) {
                auto addr = socket.local_endpoint(ec).address();
                if (!ec && !addr.is_unspecified() && !addr.is_loopback()) {
                    return addr;
                }
            }
        }
    }

    return {};
}

string urlHost(const boost::asio::ip::address& addr) {
    if (addr.is_loopback()) {
        return "localhost";
    }
    if (addr.is_v6()) {
        return format("[{}]", addr.to_string());
    }
    return addr.to_string();
}

string urlHostFromEndpoint(const string& endpoint, const string& port) {
    if (isLoopbackHost(endpoint)) {
        return "localhost";
    }

    if (isWildcardHost(endpoint)) {
        if (const auto primary = getPrimaryLocalAddress()) {
            return urlHost(*primary);
        }
        return "localhost";
    }

    boost::system::error_code ec;
    const auto ip = boost::asio::ip::make_address(string{unbracket(endpoint)}, ec);
    if (!ec) {
        return urlHost(ip);
    }

    boost::asio::io_context io;
    boost::asio::ip::tcp::resolver resolver{io};
    auto resolved = resolver.resolve(endpoint, port, ec);
    if (!ec) {
        for (const auto& e : resolved) {
            const auto addr = e.endpoint().address();
            if (!addr.is_unspecified()) {
                return urlHost(addr);
            }
        }
    }

    return endpoint;
}

string getHttpBaseUrl(const yahat::HttpConfig& cfg) {
    const bool https = !cfg.http_tls_key.empty() && !cfg.http_tls_cert.empty();
    const string_view scheme = https ? "https" : "http";
    const string default_port = https ? "443" : "80";
    const string port = cfg.http_port.empty() ? default_port : cfg.http_port;
    string base = format("{}://{}", scheme, urlHostFromEndpoint(cfg.http_endpoint, port));
    if (port != default_port) {
        base += ":" + port;
    }
    return base;
}

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
    explicit EmbeddedResHandler(std::string prefix, bool enable_spa_fallback = false)
        : prefix_{std::move(prefix)}
        , route_dir_{routeLeaf(prefix_)}
        , enable_spa_fallback_{enable_spa_fallback} {}

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

        if (const auto query_pos = t.find_first_of("?#"); query_pos != std::string_view::npos) {
            t = t.substr(0, query_pos);
        }

        if (t.empty()) {
            t = {"index.html"};
        }

        if (const auto& data = getResource(t); !data.empty()) {
            std::filesystem::path served = prefix_;
            served /= t;
            // TODO: Fix yahat so we can send a lambda to feed it with chunks.
            return {200, "OK", data.toString(), served.string()};
        }

        if (enable_spa_fallback_ && isSpaRoute(t)) {
            if (const auto& index = getResource("index.html"); !index.empty()) {
                std::filesystem::path served = prefix_;
                served /= "index.html";
                return {200, "OK", index.toString(), served.string()};
            }
        }

        return {404, "Document not found"};
    }

private:
    static std::string routeLeaf(std::string_view prefix) {
        while (!prefix.empty() && prefix.back() == '/') {
            prefix.remove_suffix(1);
        }
        if (const auto pos = prefix.find_last_of('/'); pos != std::string_view::npos) {
            prefix.remove_prefix(pos + 1);
        }
        return std::string{prefix};
    }

    auto getResource(std::string_view relPath) const {
        if (const auto data = T::get(relPath); !data.empty()) {
            return data;
        }

        const std::string dotted = "./" + std::string{relPath};
        if (const auto data = T::get(dotted); !data.empty()) {
            return data;
        }

        if (!route_dir_.empty()) {
            const std::string rooted = route_dir_ + "/" + std::string{relPath};
            if (const auto data = T::get(rooted); !data.empty()) {
                return data;
            }

            const std::string dotted_rooted = "./" + rooted;
            if (const auto data = T::get(dotted_rooted); !data.empty()) {
                return data;
            }
        }

        return T::get(relPath);
    }

    static bool hasExtension(const std::string_view path) {
        const auto filename_start = path.find_last_of('/');
        const auto filename = filename_start == std::string_view::npos
            ? path
            : path.substr(filename_start + 1);
        return filename.find('.') != std::string_view::npos;
    }

    static bool isSpaRoute(const std::string_view path) {
        if (path.empty()) {
            return true;
        }

        if (hasExtension(path)) {
            return false;
        }

        return true;
    }

    const std::string prefix_;
    const std::string route_dir_;
    const bool enable_spa_fallback_ = false;
};

} // anon ns

Server::Server(const Config &config)
    : config_{config}
{
// Macro to detect compiler and version
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#if defined(__clang__)
#define COMPILER_NAME "Clang"
#define COMPILER_VERSION TOSTRING(__clang_major__ ) "." TOSTRING(__clang_minor__) "." TOSTRING(__clang_patchlevel__)
#elif defined(__GNUC__)
#define COMPILER_NAME "GCC"
#define COMPILER_VERSION TOSTRING(__GNUC__) "." TOSTRING(__GNUC_MINOR__) "." TOSTRING(__GNUC_PATCHLEVEL__)
#elif defined(_MSC_VER)
#define COMPILER_NAME "MSVC"
#define COMPILER_VERSION TOSTRING(_MSC_VER)
#else
#define COMPILER_NAME "Unknown Compiler"
#define COMPILER_VERSION "Unknown Version"
#endif

    metrics_ = make_shared<lib::Metrics>(*this);
    metrics_->metrics().AddInfo("nsblast_build", "Build information", {}, {
        {"version", NSBLAST_VERSION},
        {"build_date", __DATE__},
        {"build_time", __TIME__},
        {"platform", BOOST_PLATFORM},
        {"compiler", COMPILER_NAME},
        {"compiler_version", COMPILER_VERSION},
        {"cpp_standard", cppStrandard()},
        {"rocksdb", rocksdb::GetRocksVersionAsString()},
        {"branch", GIT_BRANCH}
        });
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
    v.uptime_seconds = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - process_started_at).count();
    v.uptime = formatUptime(v.uptime_seconds);

    return v;
}

void Server::start()
{
    LOG_DEBUG_N << "Starting Rocksdb...";
    startRocksDb();
    LOG_DEBUG_N << "Starting Auth...";
    startAuth();

#ifdef NSBLAST_CLUSTER
    LOG_DEBUG_N << "Starting Replication and RPC...";
    startReplicationAndRpc();
#endif

    {
        bool apply_admin_fixes = true;
#ifdef NSBLAST_CLUSTER
        apply_admin_fixes = !isReplicationFollower();
#endif
        auth_->ensureAdminTenantRoleConsistency(apply_admin_fixes);
    }

    if (!config_.disable_http) {
        LOG_DEBUG_N << "Starting Api...";
        startApi();
    }

    LOG_DEBUG_N << "Starting Follower mgr...";
    startSlaveMgr();

    if (config_.disable_http) {
        LOG_INFO << "HTTP server (including the API server) is dsabled.";
    } else {
        LOG_DEBUG_N << "Starting HTTP server...";
        startHttpServer();
    }

    LOG_DEBUG_N << "Starting IO threads...";
    startIoThreads();

    LOG_DEBUG_N << "Starting Notifications...";
    startNotifications();

    LOG_DEBUG_N << "Starting Dns...";
    startDns();

    LOG_DEBUG_N << "Starting Backup manager...";
    startBackupMgr();

    LOG_DEBUG_N << "Main thread joining the thread-pool...";
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

void Server::startRocksDb(bool init, bool allow_bootstrap)
{
    auto rdb = make_shared<lib::RocksDbResource>(*this);

    if (init) {
        std::filesystem::path db_path = config_.db_path;
        db_path /= "rocksdb";

        if (allow_bootstrap) {
            if (std::filesystem::exists(db_path)) {
                throw runtime_error{
                    format("Refusing to bootstrap existing database at {}", db_path.string())
                };
            }
        } else if (!std::filesystem::is_directory(db_path)) {
            throw runtime_error{
                format("Database is not initialized at {}. Run `nsblast bootstrap` first.",
                       db_path.string())
            };
        }

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
    }, metrics_->metrics(), "nsblast "s + NSBLAST_VERSION);

    http_->addRoute("/api/v1", api_);
    http_->addRoute("/nic", api_);

#ifdef NSBLAST_WITH_SWAGGER
    if (config().swagger) {
        const string_view swagger_path = "/api/swagger";
        LOG_INFO << "Enabling Swagger at " << getHttpBaseUrl(config().http) << swagger_path;

        http_->addRoute(swagger_path,
                        make_shared<EmbeddedResHandler<lib::embedded::Swagger>>("/api/swagger"));
    }
#endif

#ifdef NSBLAST_WITH_UI
    if (config().ui) {
        const string ui_path = "/ui";
        LOG_INFO << "Enabling UI at " << getHttpBaseUrl(config().http) << ui_path;

        http_->addRoute(ui_path,
                        make_shared<EmbeddedResHandler<lib::embedded::Ui>>(ui_path, true));
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

    auth_->migrateStorage();
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

#ifdef NSBLAST_CLUSTER
void Server::startForwardingTransactionsToReplication()
{
    db().setTransactionCallback([this](PrimaryReplication::transaction_t && trx) {
        primaryReplication().onTransaction(std::move(trx));
    });
}

bool Server::followerInSync() const noexcept
{
    if (!follower_replication_) {
        return false;
    }
    return follower_replication_->isInSync();
}
#endif // NSBLAST_CLUSTER

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
    lib::Metrics::gauge_scoped_t thread_scope;
    if (haveMetrics()) {
        thread_scope = metrics().asio_worker_threads().scoped();
    }
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
    vi["uptime"] = uptime;
    vi["uptime_seconds"] = static_cast<std::int64_t>(uptime_seconds);
    for(const auto& [n, v]: components) {
        vi[n] = v;
    }

    return vi;
}

} // ns
