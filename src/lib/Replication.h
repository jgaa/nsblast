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
    using transaction_t = std::unique_ptr<nsblast::pb::Transaction>;

    /*! Replication agent.
     *
     *  This represent a nsblast follower-server that needs to be in sync with us
     */
    class FollowerAgent
        : public std::enable_shared_from_this<FollowerAgent>
        , public GrpcPrimary::ReplicationInterface {
    public:
        enum State {
            ITERATING_DB,
            STREAMING,
            DONE
        };
        
        FollowerAgent(Replication& parent, GrpcPrimary::SyncClient& client);

        /*! Iterate over a number of transactions in the db.
         *
         *  Enqueues the transactions to the client.
         */
        void iterateDb();

        State state() const noexcept {
            return state_;
        }

        /*! Change the state
         *
         *  If the state is `State::DONE`, the new state will be ignored.
         *
         *  \param state State to change to.
         *  \param doLock false if we already hold the lock when we call this method.
         */
        void setState(State state, bool doLock = true);

        boost::uuids::uuid uuid() const noexcept;

        bool expired() const {
            return client_.expired();
        }

        bool isStreaming() const noexcept {
            return state() == State::STREAMING;
        }

        /*! Notification about a new db transaction
         *
         *  If the client is in streaming mode and prevTrxId equals the clients
         *  prev trxid, the client will try to enqueue the transaction.
         */
        void onTransaction(uint64_t prevTrxId, const GrpcPrimary::update_t& update);

    private:
        // ReplicationInterface interface
        void onTrxId(uint64_t trxId) override ;
        void onQueueIsEmpty() override;
        void onDone() override;

        // Expects lock to be held
        void syncLater();

        const boost::uuids::uuid uuid_;
        std::weak_ptr<GrpcPrimary::SyncClient> client_;
        Replication& parent_;

        std::atomic<State> state_{State::ITERATING_DB};
        std::atomic_uint64_t last_enqueued_trxid_{0};
        std::atomic_uint64_t last_confirmed_trx_{0};
        std::mutex mutex_;
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
    
    /*! Add an internal representation of a replication agent for a replica server
     *
     *  This method initiates a remotes server replication with a primary.
     *
     *  The internal agent will re-use the uuid from the the SyncClient (RPC handler)
     *  so that the replication work-flow using these instacnes of the RPC
     *  handler and the replicaion agent are identified by one uuid.
     */
    GrpcPrimary::ReplicationInterface *addAgent(GrpcPrimary::SyncClient& client);

    /*! Notification about a new db transaction
     *
     * Called after a transaction with replication has been committed to the database.
     */
    void onTransaction(transaction_t&& transaction);

private:
    void startTimer();
    void housekeeping();

    Server& server_;
    uint64_t minTrxIdForAllStreamingAgents_ = 0;
    uint64_t last_trxid_ = 0;
    std::map<boost::uuids::uuid, std::shared_ptr<FollowerAgent>> follower_agents_;
    std::shared_ptr<PrimaryAgent> primary_agent_;
    boost::asio::deadline_timer timer_{server_.ctx()};
    mutable std::mutex mutex_;
};

}} // ns

std::ostream& operator <<(std::ostream& out, const nsblast::lib::Replication::FollowerAgent& agent);
std::ostream& operator <<(std::ostream& out, const nsblast::lib::Replication::FollowerAgent::State& state);

