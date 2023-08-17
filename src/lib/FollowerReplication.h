#pragma once

#include <atomic>
#include <mutex>
#include <memory>
//#include "nsblast/util.h"
//#include "RocksDbResource.h"
//#include "GrpcPrimary.h"
#include "GrpcFollow.h"
#include "proto/nsblast.pb.h"

namespace nsblast {

class Server;

namespace lib {


class FollowerReplication {
public:
    using transaction_t = std::unique_ptr<nsblast::pb::Transaction>;

    /*! Follower agent
     *
     *  This class is used if the server is a follower, and needs to
     *  be in sync with a primary server.
     */

    class Agent {
    public:
        Agent(FollowerReplication& parent);

        auto trxId() const noexcept {
            std::lock_guard lock{mutex_};
            return current_trxid_;
        }

        void init();

        void onTrx(const pb::Transaction& trx);

    private:
        std::weak_ptr<GrpcFollow::SyncFromServer> grpc_sync_;
        uint64_t current_trxid_ = 0; // Last transaction id received from the primary
        FollowerReplication& parent_;
        std::optional<std::chrono::steady_clock::time_point> due_;
        mutable std::mutex mutex_;
    };

    FollowerReplication(Server& server);

    void start();

    auto& server() {
        return server_;
    }

private:

    Server& server_;
    uint64_t last_trxid_ = 0;
    std::shared_ptr<Agent> primary_agent_;
    boost::asio::deadline_timer timer_{server_.ctx()};
    mutable std::mutex mutex_;
};

}} // ns


