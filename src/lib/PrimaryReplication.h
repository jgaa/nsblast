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


class PrimaryReplication {
public:
    using transaction_t = std::unique_ptr<nsblast::pb::Transaction>;

    /*! Replication agent.
     *
     *  This represent a nsblast follower-server that needs to be in sync with us
     */
    class Agent
        : public std::enable_shared_from_this<Agent>
        , public GrpcPrimary::ReplicationInterface {
    public:
        enum State {
            ITERATING_DB,
            STREAMING,
            DONE
        };
        
        Agent(PrimaryReplication& parent,
                       const std::shared_ptr<GrpcPrimary::SyncClientInterface>& client);

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

        /*! Notification about a new db transaction
         *
         *  If the client is in streaming mode and prevTrxId equals the clients
         *  prev trxid, the client will try to enqueue the transaction.
         */
        void onTransaction(uint64_t prevTrxId, const GrpcPrimary::update_t& update);

        /*! Return a future that will be signaled when the agent is idle (streaming or done)
         */
        auto getTestFuture() {
            std::lock_guard lock{mutex_};
            return test_promises_.emplace().get_future();
        }

    private:
        // ReplicationInterface interface
        void onTrxId(uint64_t trxId) override ;
        void onQueueIsEmpty() override;
        void onDone() override;

        bool isStreaming() const noexcept override {
            return state() == State::STREAMING;
        }

        virtual bool isCatchingUp() const noexcept override {
            return state() == State::ITERATING_DB;
        }

        virtual bool isDone() const noexcept override {
            return state() == State::DONE;
        }

        // Expects lock to be held
        void syncLater();

        const boost::uuids::uuid uuid_;
        std::weak_ptr<GrpcPrimary::SyncClientInterface> client_;
        PrimaryReplication& parent_;
        bool is_syncing_ = false;

        std::atomic<State> state_{State::ITERATING_DB};
        std::atomic_uint64_t last_enqueued_trxid_{0};
        std::atomic_uint64_t last_confirmed_trx_{0};
        std::mutex mutex_;
        std::queue<std::promise<void>> test_promises_;
    };

    PrimaryReplication(Server& server);
    
    /*! Add an internal representation of a replication agent for a replica server
     *
     *  This method initiates a remotes server replication with a primary.
     *
     *  The internal agent will re-use the uuid from the the SyncClient (RPC handler)
     *  so that the replication work-flow using these instacnes of the RPC
     *  handler and the replicaion agent are identified by one uuid.
     */
    GrpcPrimary::ReplicationInterface *addAgent(
        const std::shared_ptr<GrpcPrimary::SyncClientInterface>& client);

    /*! Notification about a new db transaction
     *
     * Called after a transaction with replication has been committed to the database.
     */
    void onTransaction(transaction_t&& transaction);

    void start();

    auto& server() noexcept {
        return server_;
    }

private:
    void startTimer();
    void housekeeping();

    Server& server_;
    uint64_t minTrxIdForAllStreamingAgents_ = 0;
    uint64_t last_trxid_ = 0;
    std::map<boost::uuids::uuid, std::shared_ptr<Agent>> follower_agents_;
    boost::asio::deadline_timer timer_{server_.ctx()};
    mutable std::mutex mutex_;
};

}} // ns

std::ostream& operator <<(std::ostream& out, const nsblast::lib::PrimaryReplication::Agent& agent);
std::ostream& operator <<(std::ostream& out, const nsblast::lib::PrimaryReplication::Agent::State& state);

