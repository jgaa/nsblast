
#include <boost/uuid/string_generator.hpp>

#include <grpc/grpc.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>

#include "Grpc.h"
#include "Replication.h"
#include "nsblast/logging.h"
#include "nsblast/util.h"

using namespace std;
using namespace std::string_literals;
using namespace std::chrono_literals;

ostream &operator <<(std::ostream &out, const nsblast::lib::Grpc::SyncClient::State state) {
    static const std::array<std::string_view, 4> names = {
        "CREATED", "WAITING", "ACTIVE", "FAILED"
    };

    return out << names.at(static_cast<size_t>(state));
}

ostream &operator <<(std::ostream &out, const nsblast::lib::Grpc::SyncClient::Op op) {
    static const std::array<std::string_view, 4> names = {
        "CHANNEL", "READ", "WRITE", "DISCONNECT"
    };

    return out << names.at(static_cast<size_t>(op));
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

void Grpc::done(const boost::uuids::uuid &uuid)
{
    lock_guard lock{mutex_};
    clients_.erase(uuid);
}

std::shared_ptr<Grpc::ClientBase> Grpc::get(const boost::uuids::uuid &uuid)
{
    lock_guard lock{mutex_};
    if (auto it = clients_.find(uuid); it != clients_.end()) {
        return it->second;
    }

    return {};
}

void Grpc::done(ClientBase &client)
{
    LOG_TRACE << "Grpc::done - Removing client " << client.uuid();
    lock_guard lock{mutex_};
    clients_.erase(client.uuid());
}

void Grpc::process()
{
    addSyncClient();

    while(svc_) {
        LOG_TRACE << "Grpc::process() - On top of event-loop";
        bool ok = true;
        void *cptr = {};
        cq_->Next(&cptr, &ok);
        if (cptr) {
            auto handle = static_cast<SyncClient::Handle *>(cptr);
            handle->proceed(ok);
        } // TODO: what do we do if not ok?
    }
}

void Grpc::addSyncClient()
{
    auto client = make_shared<SyncClient>(*this);
    LOG_TRACE << "Grpc::addSyncClient() - Created gRPC Sync client instance " << client->uuid();
    {
        lock_guard lock{mutex_};
        clients_[client->uuid()] = client;
    }
    client->start();
}

Grpc::SyncClient::SyncClient(Grpc &grpc)
    : ClientBase(grpc)
{
    LOG_TRACE << "Grpc::Client::Client new instance:" << uuid();
}

void Grpc::SyncClient::start()
{
    // In our case, we need a message from the client before we do anything else.
    grpc_.impl_->RequestSync(&ctx_, &io_, grpc_.cq_.get(), grpc_.cq_.get(), &channel_handle_);
    state_ = State::WAITING;
    ctx_.AsyncNotifyWhenDone(&disconnect_handle_);
}

void Grpc::SyncClient::proceed(Op op, bool ok)
{
    // Process request
    LOG_TRACE << "Grpc::Client::process - Client "
              << uuid() << " [" << state() << "] "
              << (ok ? "" : " FAILED ")
              <<  op << " peer: " << ctx_.peer();

    if (logfault::LogManager::Instance().GetLoglevel() == logfault::LogLevel::TRACE) {
        const auto meta = ctx_.client_metadata();
        LOG_TRACE << "Metadata:";
        for(auto& [key, value] : meta) {
            LOG_TRACE << "  --> " << key << '=' << value;
        }
    }

    if (op == Op::DISCONNECT) {
        LOG_INFO << "gRPC Sync client " << uuid() << " disconnected (done).";
        grpc_.done(*this);
        return;
    }

    switch (state()) {
    case State::WAITING:
        assert(op == Op::CHANNEL);
        grpc_.addSyncClient();
        if (ok) {
            state_ = State::ACTIVE;
            on_trxid_fn_ = grpc_.owner_.replication().addAgent(*this, req_.startafter());
            io_.Read(&req_, &read_handle_);
            LOG_INFO << "gRPC Sync client connected: " << uuid()
                      << ' ' << ctx_.peer();
        } else {
            state_ = State::FAILED;
        }
        break;
    case State::ACTIVE:
        if (op == Op::READ) {
            if (ok) {
                LOG_TRACE << "Grpc::SyncClient::proceed(READ) - request: " << toJson(req_);
                onTrxId(req_.startafter());
                io_.Read(&req_, &read_handle_);
            } else {
                LOG_DEBUG << "Grpc::SyncClient::proceed(READ) - request failed.";
            }
        } else if (op == Op::WRITE) {
            if (ok) {
                lock_guard lock{mutex_};
                current_.reset();
                flush();
            } else {
                LOG_DEBUG << "Grpc::SyncClient::proceed(WRITE) - request failed.";
            }
        } else if (op == Op::CHANNEL){
            LOG_DEBUG << "Grpc::SyncClient::proceed - CHANNEL";
        } else {
            LOG_ERROR << "Grpc::SyncClient::proceed - ***UNKNOWN";
            assert(false);
        }
        break;
    case State::CREATED:
        assert(false);
    case State::FAILED:
        assert(false);
    }
}

bool Grpc::SyncClient::enqueue(update_t &&update)
{
    lock_guard lock{mutex_};
    if (state_ != State::ACTIVE) {
        LOG_TRACE << "Grpc::Client::enqueue = - Client " << uuid()
                  << " is in state " << state()
                  << " and cannot add updates at this time.";
        return false;
    }

    pending_.emplace(std::move(update));
    flush();

    if (pending_.size() > grpc_.owner_.config().repl_agent_max_queue_size) {
        LOG_TRACE << "Grpc::Client::enqueue = - Client " << uuid()
                  << " bumped into the queue limit size of "
                  <<  grpc_.owner_.config().repl_agent_max_queue_size;
        return false;
    }

    return true;
}

void Grpc::SyncClient::onTrxId(uint64_t trxId)
{
    if (on_trxid_fn_) {
        on_trxid_fn_(trxId);
    }
}

void Grpc::SyncClient::flush()
{
    if (!current_ && !pending_.empty()) {
        current_ = std::move(pending_.front());
        pending_.pop();
        io_.Write(*current_, &write_handle_);
    }
}


} //ns
