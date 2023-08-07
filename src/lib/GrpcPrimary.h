#pragma once

#include <queue>

#include <grpcpp/server.h>

#include "nsblast/Server.h"
#include "nsblast/util.h"
#include "proto_util.h"
#include "proto/nsblast-grpc.grpc.pb.h"

namespace nsblast::lib {

/*! GRPC service for cluster communication and replication
 *
 *  This class handles the server-side
 */
class GrpcPrimary {
public:
    using update_t = std::shared_ptr<grpc::nsblast::pb::SyncUpdate>;
    using on_trxid_fn_t = std::function<void(uint64_t trxId)>;
    using bidi_sync_stream_t = ::grpc::ServerBidiReactor< ::grpc::nsblast::pb::SyncRequest, ::grpc::nsblast::pb::SyncUpdate>;

    class SyncClient
        : public std::enable_shared_from_this<SyncClient>
        , public bidi_sync_stream_t {
    public:

        SyncClient(GrpcPrimary& grpc, ::grpc::CallbackServerContext& context);

        /*! Add one update to the queue.
         *
         *  This method is thread-safe.
         *
         *  \param update Update to send top the client
         *  \return True if the update was enqueued.
         *      False if the queue is full or if the client is disconnected.
         */
        bool enqueue(update_t &&update);

        auto uuid() const noexcept {
            return uuid_;
        }

        void onTrxId(uint64_t trxId);

        bool isWriting() const noexcept {
            std::lock_guard lock{mutex_};
            return current_ != nullptr;
        }

    private:
        /*! Callback event when the RPC is complete */
        void OnDone() override;

        /*! Callback event when a read operation is complete */
        void OnReadDone(bool ok) override;

        /*! Callback event when a write operation is complete */
        void OnWriteDone(bool ok) override ;

        void flush();

        const boost::uuids::uuid uuid_ = newUuid();
        GrpcPrimary& grpc_;

        bool is_done_ = false;

        mutable std::mutex mutex_;
        ::grpc::nsblast::pb::SyncRequest req_;
        std::queue<update_t> pending_;
        update_t current_;
        on_trxid_fn_t on_trxid_fn_;
        ::grpc::CallbackServerContext& context_;
    };

    class NsblastSvcImpl: public grpc::nsblast::pb::NsblastSvc::CallbackService {
    public:
        NsblastSvcImpl(GrpcPrimary& grpc)
            : grpc_{grpc} {}

    private:
        // Override of the gRPC callback to hande a new incoming RPC
        bidi_sync_stream_t* Sync(
            ::grpc::CallbackServerContext* context) override;

        GrpcPrimary& grpc_;
    };

    GrpcPrimary(Server& server);

    void start();
    void stop();

    std::shared_ptr<SyncClient> get(const boost::uuids::uuid& uuid);
    void done(SyncClient& client);

    bidi_sync_stream_t *createSyncClient(::grpc::CallbackServerContext* context);

private:
    void init();

    Server& owner_;
    std::unique_ptr<NsblastSvcImpl> impl_;
    std::unique_ptr<grpc::Server> svc_;
    std::map<boost::uuids::uuid, std::shared_ptr<SyncClient>> clients_;
    std::mutex mutex_;
};

} // ns

