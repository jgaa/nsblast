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
    using update_t = std::unique_ptr<grpc::nsblast::pb::SyncUpdate>;

    class Client : public std::enable_shared_from_this<Client> {
    public:
        enum class State {
            CREATED,
            REQUEST,
            REPLIES,
            FAILED
        };

        Client(Grpc& grpc);

        const auto& uuid() const noexcept {
            return uuid_;
        }

        void proceed();

        auto state() const noexcept {
            return state_;
        }

        /*! Add one update to the queue.
         *
         *  This method is thread-safe.
         *
         *  \param update Update to send top the client
         *  \return True if the update was enqueued.
         *      False if the queue is full or if the client is disconnected.
         */
        bool enqueue(update_t &&update);

    private:
        void flush();

        State state_ = State::CREATED;
        std::mutex mutex_;
        Grpc& grpc_;
        const boost::uuids::uuid uuid_ = newUuid();
        grpc::nsblast::pb::SyncRequest req_;
        grpc::ServerContext ctx_;
        grpc::ServerAsyncWriter<grpc::nsblast::pb::SyncUpdate> resp_{&ctx_};
        std::queue<std::unique_ptr<grpc::nsblast::pb::SyncUpdate>> pending_;
        std::unique_ptr<grpc::nsblast::pb::SyncUpdate> current_;
    };

    Grpc(Server& server);

    void start();
    void stop();

    void init();
private:
    void process();
    void addClient();

    Server& owner_;
    std::unique_ptr<grpc::ServerCompletionQueue> cq_;
    std::unique_ptr<NsblastSvcImpl> impl_;
    std::unique_ptr<grpc::Server> svc_;
    std::optional<std::thread> grpc_not_so_async_thread_;
    std::map<boost::uuids::uuid, std::shared_ptr<Client>> clients_;
};

}

std::ostream& operator << (std::ostream& out, const nsblast::lib::Grpc::Client::State state);
