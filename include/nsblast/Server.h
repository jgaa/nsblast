#pragma once
#include <atomic>

#include <boost/unordered/unordered_flat_set.hpp>

#include "nsblast/nsblast.h"
#include "yahat/HttpServer.h"

namespace yahat {
    class HttpServer;
} // yahat ns

namespace nsblast {

namespace lib {
    class ResourceIf;
    class RestApi;
    class SlaveMgr;
    class DnsEngine;
    class Notifications;
    class AuthMgr;
    class RocksDbResource;

#ifdef NSBLAST_CLUSTER
    class Grpc;
    class Replication;
#endif

} // ns lib

/*! Singleton object that owns the vraious components the application using the library depends on
 */
class Server {
public:
    Server(const Config& config);
    ~Server();

    /*! Starts the API and DNS server.
     *
     *  Returns when the servers are done
     */
    void start();

    /*! Resets the admin user and nsblast account to it's initial, default state." */
    void resetAuth();

    void startRocksDb();

    void startIoThreads();

    void startHttpServer();

    void startApi();

    void startSlaveMgr();

    void startDns();

    void startNotifications();

    void startAuth();

#ifdef NSBLAST_CLUSTER
    void StartReplication();

    void startGrpcService();
#endif

    /*! Thread-safe method to request the services to shut down
     *
     *  Returns immediately.
     */
    void stop();

    /*! True after stop() is called. */
    bool isDone() const noexcept {
        return done_.load(std::memory_order_relaxed);
    }

    auto& resource() const noexcept {
        assert(resource_);
        return *resource_;
    }

    lib::RocksDbResource& db() const noexcept;

    auto& notifications() const noexcept {
        assert(notifications_);
        return *notifications_;
    }

    auto& api() const noexcept {
        assert(api_);
        return *api_;
    }

    auto& slave() const noexcept {
        assert(slave_);
        return *slave_;
    }

    auto& dns() const noexcept {
        assert(dns_);
        return *dns_;
    }

    const auto& config() const noexcept {
        return config_;
    }

    auto& ctx() noexcept {
        return ctx_;
    }

    auto& auth() noexcept {
        return *auth_;
    }

#ifdef NSBLAST_CLUSTER
    auto& replication() noexcept {
        return *replication_;
    }

    auto& grpc() noexcept {
        return *grpc_;
    }
#endif

    /*! Get an unused ID for a request */
    uint32_t getNewId();

    /*! Release the id from the repository of active ID's */
    void idDone(uint32_t id);

    bool wasBootstrapped() const noexcept {
        return bootstrapped_;
    }

protected:
    void runWorker(const std::string& name);
    void handleSignals();

    std::atomic_bool done_{false};
    boost::asio::io_context ctx_;
    std::vector<std::thread> workers_;

    std::shared_ptr<lib::ResourceIf> resource_;
    std::shared_ptr<lib::Notifications> notifications_;
    std::shared_ptr<yahat::HttpServer> http_;
    std::shared_ptr<lib::RestApi> api_;
    std::shared_ptr<lib::SlaveMgr> slave_;
    std::shared_ptr<lib::DnsEngine> dns_;
    std::shared_ptr<lib::AuthMgr> auth_;
#ifdef NSBLAST_CLUSTER
    std::shared_ptr<lib::Grpc> grpc_;
    std::shared_ptr<lib::Replication> replication_;
#endif

    const Config& config_;
    boost::unordered_flat_set<uint32_t> current_request_ids_;
    std::mutex ids_mutex_;
    std::once_flag stop_once_;
    std::optional<std::future<void>> ft_http_;
    std::optional<boost::asio::signal_set> signals_;
    bool bootstrapped_ = false;
};


} // ns
