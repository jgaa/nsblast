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
    class BackupMgr;

#ifdef NSBLAST_CLUSTER
    class GrpcPrimary;
    class GrpcFollow;
    class PrimaryReplication;
    class FollowerReplication;
#endif

} // ns lib

/*! Singleton object that owns the vraious components the application using the library depends on
 */
class Server {
public:
    enum class Role {
        NONE,
        CLUSTER_PRIMARY,
        CLUSTER_FOLLOWER
    };

    Server(const Config& config);
    ~Server();

    /*! Starts the API and DNS server.
     *
     *  Returns when the servers are done
     */
    void start();

    /*! Resets the admin user and nsblast account to it's initial, default state." */
    void resetAuth();

    void startRocksDb(bool init = true);

    void startIoThreads();

    void startHttpServer();

    void startApi();

    void startSlaveMgr();

    void startDns();

    void startNotifications();

    void startAuth();

    void startBackupMgr(bool startAutoBackups = true);

#ifdef NSBLAST_CLUSTER
    void StartReplication();

    void startGrpcService();

    void startReplicationAndRpc();
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

//    auto configPtr() const noexcept {
//        return config_;
//    }

    auto& ctx() noexcept {
        return ctx_;
    }

    auto& auth() noexcept {
        return *auth_;
    }

#ifdef NSBLAST_CLUSTER
    void initReplication();

    auto& primaryReplication() noexcept {
        assert(primary_replication_);
        return *primary_replication_;
    }

    auto& followerReplication() noexcept {
        assert(follower_replication_);
        return *follower_replication_;
    }

    auto& grpcPrimary() noexcept {
        assert(grpc_primary_);
        return *grpc_primary_;
    }

    auto& grpcFollow() noexcept {
        assert(grpc_follow_);
        return *grpc_follow_;
    }

    bool isPrimaryReplicationServer() const noexcept {
         return role_ == Role::CLUSTER_PRIMARY;
    }

    bool isReplicationFollower() const noexcept {
         return role_ == Role::CLUSTER_FOLLOWER;
    }

    void startForwardingTransactionsToReplication();

#endif

    /*! Get an unused ID for a request */
    uint32_t getNewId();

    /*! Release the id from the repository of active ID's */
    void idDone(uint32_t id);

    bool wasBootstrapped() const noexcept {
        return bootstrapped_;
    }

    auto role() const noexcept {
        return role_;
    }

    bool isCluster() const noexcept {
        return role_ != Role::NONE;
    }

    auto& backup() {
        assert(backup_);
        return *backup_;
    }

    void restoreBackup(int id);
    void validateBackup(int id);
    void listBackups();

protected:
    void runWorker(const std::string& name);
    void handleSignals();

    std::atomic_bool done_{false};
    boost::asio::io_context ctx_;
    std::vector<std::thread> workers_;
    Role role_ = Role::NONE;

    std::shared_ptr<lib::ResourceIf> resource_;
    std::shared_ptr<lib::Notifications> notifications_;
    std::shared_ptr<yahat::HttpServer> http_;
    std::shared_ptr<lib::RestApi> api_;
    std::shared_ptr<lib::SlaveMgr> slave_;
    std::shared_ptr<lib::DnsEngine> dns_;
    std::shared_ptr<lib::AuthMgr> auth_;
    std::shared_ptr<lib::BackupMgr> backup_;
#ifdef NSBLAST_CLUSTER
    std::shared_ptr<lib::GrpcPrimary> grpc_primary_;
    std::shared_ptr<lib::GrpcFollow> grpc_follow_;
    std::shared_ptr<lib::PrimaryReplication> primary_replication_;
    std::shared_ptr<lib::FollowerReplication> follower_replication_;
#endif

    //std::shared_ptr<Config> config_;
    const Config& config_;
    boost::unordered_flat_set<uint32_t> current_request_ids_;
    std::mutex ids_mutex_;
    std::once_flag stop_once_;
    std::optional<std::future<void>> ft_http_;
    std::optional<boost::asio::signal_set> signals_;
    bool bootstrapped_ = false;
};


} // ns

std::ostream& operator << (std::ostream& out, const nsblast::Server::Role& role);
