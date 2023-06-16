#pragma once

#include <atomic>
#include <mutex>
#include <memory>
#include "nsblast/util.h"
#include "RocksDbResource.h"
#include "Grpc.h"
#include "proto/nsblast.pb.h"

namespace nsblast {

class Server;

namespace lib {


class Replication {
public:

    /*! Replication agent.
     *
     *  This represent a nsblast slave-server that needs to be in sync with us
     */
    class Agent {
    public:
        enum State {
            ITERATING_DB,
            STREAMING
        };

        Agent(Replication& parent, Grpc::SyncClient& client, uint64_t fromTrxId);

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

    private:
        State state_ = State::ITERATING_DB;
        uint64_t currentTrx = 0;
        uint64_t lastConfirmedTrx = 0;
        std::weak_ptr<Grpc::SyncClient> client_;
        std::mutex mutex_;
        Replication& parent_;
    };

    void start();

    Replication(Server& server);

    const auto& server() {
        return server_;
    }

    Grpc::on_trxid_fn_t addAgent(Grpc::SyncClient& client, uint64_t fromTrxId);

    template <typename T>
    void applyIf(uint64_t trxId, const T& fn) {
        std::lock_guard lock{mutex_};
        if (server_.db().nextTrxId() == trxId) {
            fn();
        }
    }

private:
    Server& server_;
    uint64_t minTrxIdForAllStreamingAgents_ = 0;
    std::map<boost::uuids::uuid, std::shared_ptr<Agent>> agents_;
    std::mutex mutex_;
};

}} // ns
