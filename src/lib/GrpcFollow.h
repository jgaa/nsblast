
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

    class SyncFromServer {
    public:
        enum class Op {
            CHANNEL,
            READ,
            WRITE,
            DISCONNECT
        };

        SyncFromServer(GrpcFollow& grpc, const std::string& address,
                       due_t due, on_update_t onUpdate);

        // gRPC can't tell us if an async event was in response to a write
        // or read operation. We use the Handle to keep this state ourself.
        struct Handle {
            SyncFromServer *self_ = {};
            Op op_;

            void proceed(bool ok) {
                assert(self_);
                self_->proceed(op_, ok);
            }

            void timeout() {
                self_->timeout();
            }
        };

        auto& uuid() const noexcept {
            return uuid_;
        }

        void start();
        void proceed(Op op, bool ok);
        void timeout();
        void writeIf();

    private:
        Handle channel_handle_ {this, Op::CHANNEL};
        Handle read_handle_ {this, Op::READ};
        Handle write_handle_ {this, Op::WRITE};
        Handle disconnect_handle_ {this, Op::DISCONNECT};
        GrpcFollow& grpc_;
        grpc::ClientContext ctx_;
        std::shared_ptr<grpc::Channel> channel_;
        std::unique_ptr<grpc::nsblast::pb::NsblastSvc::Stub> stub_;
        std::unique_ptr<::grpc::ClientAsyncReaderWriter< ::grpc::nsblast::pb::SyncRequest, ::grpc::nsblast::pb::SyncUpdate>> rpc_;
        const boost::uuids::uuid uuid_ = newUuid();
        due_t due_;
        on_update_t on_update_;
        grpc::nsblast::pb::SyncRequest req_;
        grpc::nsblast::pb::SyncUpdate update_;
        bool can_write_ = false;
    };

    void start();
    void stop();

    auto& server() noexcept {
        return server_;
    }

    std::shared_ptr<SyncFromServer> createSyncClient(due_t due, on_update_t onUpdate);

private:
    void run();
    Server& server_;
    grpc::CompletionQueue cq_;
    std::shared_ptr<SyncFromServer> sync_client_;
    std::optional<std::thread> grpc_not_so_async_thread_;
    //std::optional<std::chrono::steady_clock::time_point> due_for_update_;
};

} // ns

std::ostream &operator <<(std::ostream &out, const nsblast::lib::GrpcFollow::SyncFromServer::Op op);


