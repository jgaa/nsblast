
#include <grpc/grpc.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>

#include "Grpc.h"
#include "nsblast/logging.h"
#include "nsblast/util.h"

using namespace std;
using namespace std::string_literals;
using namespace std::chrono_literals;

ostream &operator <<(std::ostream &out, const nsblast::lib::Grpc::Client::State state) {
    static const std::array<std::string_view, 4> names = {
        "CREATED", "REQUEST", "REPLIES", "FAILED"
    };

    return out << names.at(static_cast<size_t>(state));
}


namespace nsblast::lib {

namespace {


} // anon ns

Grpc::Grpc(Server &server)
    : owner_{server}
{
}

void Grpc::start()
{
    auto server_address = owner_.config().grpc_server_addr;
    impl_ = make_unique<NsblastSvcImpl>();

    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(impl_.get());
    cq_= builder.AddCompletionQueue();
    svc_ = builder.BuildAndStart();
    LOG_INFO << "gRPC (cluster) Server listening on " << server_address;

    grpc_not_so_async_thread_.emplace([this]{
        try {
            process();
        } catch(const exception& ex) {
            LOG_ERROR << "Caught exception in grpc thread: " << ex.what();
        }
    });
}

void Grpc::stop()
{
    if (svc_) {
        LOG_INFO << "Grpc::stop - Shutting down gRPC service.";
        svc_->Shutdown();
        svc_.reset();
    }

    if (impl_) {
        LOG_DEBUG << "Grpc::stop - Shutting down gRPC impl handlers.";
        impl_.reset();
    }
}

void Grpc::process()
{
    addClient();

    while(svc_) {
        LOG_TRACE << "Grpc::process() - On top of event-loop";
        bool ok = true;
        void *cptr = {};
        cq_->Next(&cptr, &ok);
        assert(ok);
        assert(cptr);
        auto client = static_cast<Client *>(cptr);
        LOG_TRACE << "Grpc::process() - processing client " << client->uuid()
                  << " [" << client->state() << "]";
        client->proceed();
    }
}

void Grpc::addClient()
{
    auto client = make_shared<Client>(*this);
    LOG_TRACE << "Grpc::addClient() - Created gRPC client instance " << client->uuid();
    clients_[client->uuid()] = client;
    client->proceed();
}

Grpc::Client::Client(Grpc &grpc)
    : grpc_{grpc}
{
    LOG_TRACE << "Grpc::Client::Client new instance:" << uuid();
}

void Grpc::Client::proceed()
{
    switch (state()) {
    case State::CREATED:
        grpc_.impl_->RequestSync(&ctx_, &req_, &resp_, grpc_.cq_.get(), grpc_.cq_.get(), this);
        state_ = State::REQUEST;
        break;
    case State::REQUEST:
        grpc_.addClient();

        // Process request
        LOG_DEBUG << "Grpc::Client::process - Client "
                  << uuid() << "[ " << state() << "] "
                  << " request: " << toJson(req_)
                  << " peer: " << ctx_.peer();

        if (logfault::LogManager::Instance().GetLoglevel() == logfault::LogLevel::TRACE) {
            const auto meta = ctx_.client_metadata();
            LOG_TRACE << "Metadata:";
            for(auto& [key, value] : meta) {
                LOG_TRACE << "  --> " << key << '=' << value;
            }
        }

        state_ = State::REPLIES;
        // Fall trough
    case State::REPLIES:
        flush();
        break;
    case State::FAILED:
        assert(false);
    }
}

void Grpc::Client::flush()
{
    if (!current_ && !pending_.empty()) {
        current_ = std::move(pending_.front());
        pending_.pop();

        resp_.Write(*current_, this);
    }
}

} //ns
