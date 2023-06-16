#pragma once

#include <queue>

#include <grpcpp/server.h>

#include "nsblast/Server.h"
#include "nsblast/util.h"
#include "proto_util.h"
#include "proto/nsblast-grpc.grpc.pb.h"

namespace nsblast::lib {

class NsblastSvcImpl : public grpc::nsblast::pb::NsblastSvc::AsyncService {


    // Service interface
public:
    NsblastSvcImpl() = default;

};

/*! GRPC service for cluster communication and replication
 */
class Grpc {
public:
    using update_t = std::shared_ptr<grpc::nsblast::pb::SyncUpdate>;
    using on_trxid_fn_t = std::function<void(uint64_t trxId)>;

    class ClientBase {
    public:
        enum class Op {
            CHANNEL,
            READ,
            WRITE,
            DISCONNECT
        };

        // gRPC can't tell us if an async event was in response to a write
        // or read operation. We use the Handle to keep this state ourself.
        struct Handle {
            ClientBase *self_ = {};
            Op op_;

            void proceed(bool ok) {
                assert(self_);
                self_->proceed(op_, ok);
            }
        };

        ClientBase(Grpc& grpc) : grpc_{grpc} {}
        virtual ~ClientBase() = default;

        virtual void start() = 0;
        virtual void proceed(Op op, bool ok) = 0;

        const auto& uuid() const noexcept {
            return uuid_;
        }

    protected:
        Handle channel_handle_ {this, Op::CHANNEL};
        Handle read_handle_ {this, Op::READ};
        Handle write_handle_ {this, Op::WRITE};
        Handle disconnect_handle_ {this, Op::DISCONNECT};
        Grpc& grpc_;
        const boost::uuids::uuid uuid_ = newUuid();
        grpc::ServerContext ctx_;
    };

    class SyncClient : public std::enable_shared_from_this<SyncClient>, public ClientBase {
    public:
        enum class State {
            CREATED,
            WAITING,
            ACTIVE,
            FAILED
        };

        SyncClient(Grpc& grpc);

        void start() override;
        void proceed(Op op, bool ok) override;


        /*! Add one update to the queue.
         *
         *  This method is thread-safe.
         *
         *  \param update Update to send top the client
         *  \return True if the update was enqueued.
         *      False if the queue is full or if the client is disconnected.
         */
        bool enqueue(update_t &&update);

        void onTrxId(uint64_t trxId);

        auto state() const noexcept {
            return state_;
        }

    private:
        void flush();
        State state_ = State::CREATED;
        std::mutex mutex_;
        ::grpc::ServerAsyncReaderWriter< ::grpc::nsblast::pb::SyncUpdate, ::grpc::nsblast::pb::SyncRequest> io_{&ctx_};
        ::grpc::nsblast::pb::SyncRequest req_;
        std::queue<update_t> pending_;
        update_t current_;
        on_trxid_fn_t on_trxid_fn_;
    };

    Grpc(Server& server);

    void start();
    void stop();

    void done(const boost::uuids::uuid& uuid);

    std::shared_ptr<ClientBase> get(const boost::uuids::uuid& uuid);
    void done(ClientBase& client);

    void init();
private:
    void process();
    void addSyncClient();

    Server& owner_;
    std::unique_ptr<grpc::ServerCompletionQueue> cq_;
    std::unique_ptr<NsblastSvcImpl> impl_;
    std::unique_ptr<grpc::Server> svc_;
    std::optional<std::thread> grpc_not_so_async_thread_;
    std::map<boost::uuids::uuid, std::shared_ptr<ClientBase>> clients_;
    std::mutex mutex_;
};

}

std::ostream &operator <<(std::ostream &out, const nsblast::lib::Grpc::SyncClient::Op op);
std::ostream& operator << (std::ostream& out, const nsblast::lib::Grpc::SyncClient::State state);
