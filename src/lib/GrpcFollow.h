
#pragma once

#include <queue>

#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>

#include "nsblast/Server.h"
#include "nsblast/util.h"
#include "proto_util.h"
#include "proto/nsblast-grpc.grpc.pb.h"

namespace nsblast::lib {

/*! GRPC service that receives data from the master server
 */
class GrpcFollow {
public:
    using due_t = std::function<std::optional<uint64_t>()>; // Called to check if we should send an update
    using on_update_t = std::function<void(const grpc::nsblast::pb::SyncUpdate& update)>;

    GrpcFollow(Server& server);

    class SyncFromServer : public std::enable_shared_from_this<SyncFromServer>
                         , grpc::ClientBidiReactor<grpc::nsblast::pb::SyncRequest,
                                                   grpc::nsblast::pb::SyncUpdate> {
    public:
        SyncFromServer(GrpcFollow& grpc, const std::string& address);

        auto& uuid() const noexcept {
            return uuid_;
        }

        void start();
        void stop();
        void timeout();
        void writeIf();
        void ping();
        bool isDone() const noexcept {
            return done_;
        }

    private:
        /*! Callback event when a write operation is complete */
        void OnWriteDone(bool ok) override {
            writeIf();
        }

        /*! Callback event when a read operation is complete */
        void OnReadDone(bool ok) override;

        /*! Callback event when the RPC is complete */
        void OnDone(const grpc::Status& s) override;

        GrpcFollow& grpc_;
        grpc::ClientContext ctx_;
        std::shared_ptr<grpc::Channel> channel_;
        const boost::uuids::uuid uuid_ = newUuid();
        grpc::nsblast::pb::SyncRequest req_;
        grpc::nsblast::pb::SyncUpdate update_;
        bool can_write_ = false;
        std::shared_ptr<SyncFromServer> self_;
        std::atomic_bool done_{false};
        std::mutex mutex_;
    };

    void start();
    void stop();

    auto& server() noexcept {
        return server_;
    }

    void createSyncClient(due_t due, on_update_t onUpdate);

    const auto& agent() {
        return follower_;
    }


private:
    void scheduleNextTimer();
    void startFollower();
    void onTimer();

    Server& server_;
    on_update_t on_update_;
    due_t due_;
    boost::asio::deadline_timer timer_{server_.ctx()};
    std::unique_ptr<grpc::nsblast::pb::NsblastSvc::Stub> stub_;
    std::shared_ptr<SyncFromServer> follower_;
    bool stopped_ = true;
    //std::optional<std::chrono::steady_clock::time_point> due_for_update_;
};

} // ns


