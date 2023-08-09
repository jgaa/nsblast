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
    using bidi_sync_stream_t = ::grpc::ServerBidiReactor< ::grpc::nsblast::pb::SyncRequest, ::grpc::nsblast::pb::SyncUpdate>;

    /*! Interface to the replication agent to allow passing events to it
     *
     * The event-methods must return immediately. If they need to do some work,
     * they myst schedule that on a worker-thread.
     *
     * Ownership: The Replication class(es) own an instance. The instance
     *      will not be deleted until after onDone() is called (but potentially
     *      in onDone()).
     *
     * Guarantee: onDone() will always be called once, unless the server crash.
     *      After onDone() is called, an instance will never be used by GrpcPrimary.
     */
    class ReplicationInterface {
    public:
        virtual ~ReplicationInterface() = default;

        /*! The replica server confirmes that it as committed transactions up to `trxId` */
        virtual void onTrxId(uint64_t trxId) = 0;

        /*! The sending queue for the replica server is empty.
         *
         *  If the replicatin is catching up, the replication agnet should read events from the
         *  database and fill the queue.
         */
        virtual void onQueueIsEmpty() = 0;

        /*! The syncronization stream with the replica server serverf is shut down. */
        virtual void onDone() = 0;
    };

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
        bool enqueue(update_t update);

        auto uuid() const noexcept {
            return uuid_;
        }

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

        /*! Send the next queued transaction
         *
         *  Signals the replication agent if the queue is empty
         */
        void flush();

        const boost::uuids::uuid uuid_ = newUuid();
        GrpcPrimary& grpc_;
        bool is_done_ = false;

        // optimization to not call the empty queue callback if it has already been called
        // since the last write operation was initiated.
        bool has_written_after_empty_queue_ = true;

        ::grpc::nsblast::pb::SyncRequest req_;
        std::queue<update_t> pending_;
        update_t current_;
        ReplicationInterface *replication_ = {};
        ::grpc::CallbackServerContext& context_;
        mutable std::mutex mutex_;
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
    std::map<boost::uuids::uuid, std::shared_ptr<SyncClient>> clients_;
    std::unique_ptr<NsblastSvcImpl> impl_;
    std::unique_ptr<grpc::Server> svc_;

    std::mutex mutex_;
};

} // ns

