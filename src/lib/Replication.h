#pragma once

#include <atomic>
#include <mutex>
#include <memory>
#include "nsblast/util.h"
#include "RocksDbResource.h"
#include "GrpcPrimary.h"
#include "GrpcFollow.h"
#include "proto/nsblast.pb.h"

namespace nsblast {

class Server;

namespace lib {


class Replication {
public:

    /*! Replication agent.
     *
     *  This represent a nsblast follower-server that needs to be in sync with us
     */
    class FollowerAgent {
    public:
        enum State {
            ITERATING_DB,
            STREAMING
        };
        
        FollowerAgent(Replication& parent, GrpcPrimary::SyncClient& client, uint64_t fromTrxId);

        /*! Iterate over a number of transactions in the db.
         *
         *  Enqueues the transactions to the client.
         */
        void iterateDb();

        auto state() const noexcept {
            return state_;
        }

        void onTrxId(uint64_t trxId);

        boost::uuids::uuid uuid() const noexcept;

        bool expired() const {
            return client_.expired();
        }

    private:
        State state_ = State::ITERATING_DB;
        uint64_t currentTrx = 0;
        uint64_t lastConfirmedTrx = 0;
        std::weak_ptr<GrpcPrimary::SyncClient> client_;
        std::mutex mutex_;
        Replication& parent_;
    };

    /*! Follower client
     *
     *  This class is used if the server is a follower, and needs to
     *  be in sync with a primary server.
     */

    class PrimaryAgent {
    public:
        PrimaryAgent(Replication& parent);

        auto trxId() const noexcept {
            std::lock_guard lock{mutex_};
            return current_trxid_;
        }

        void init();

        void onTrx(const pb::Transaction& trx);

    private:
        std::weak_ptr<GrpcFollow::SyncFromServer> grpc_sync_;
        uint64_t current_trxid_ = 0; // Last transaction id received from the primary
        Replication& parent_;
        std::optional<std::chrono::steady_clock::time_point> due_;
        mutable std::mutex mutex_;
    };

    void start();

    Replication(Server& server);

    auto& server() {
        return server_;
    }
    
    GrpcPrimary::on_trxid_fn_t addAgent(GrpcPrimary::SyncClient& client, uint64_t fromTrxId);

    template <typename T>
    void applyIf(uint64_t trxId, const T& fn) {
        std::lock_guard lock{mutex_};
        if (server_.db().nextTrxId() == trxId) {
            fn();
        }
    }

private:
    void startTimer();
    void housekeeping();

    Server& server_;
    uint64_t minTrxIdForAllStreamingAgents_ = 0;
    std::map<boost::uuids::uuid, std::shared_ptr<FollowerAgent>> follower_agents_;
    std::shared_ptr<PrimaryAgent> primary_agent_;
    boost::asio::deadline_timer timer_{server_.ctx()};
    mutable std::mutex mutex_;
};

}} // ns
