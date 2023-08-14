
#include <boost/uuid/string_generator.hpp>

#include <grpc/grpc.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>

#include "GrpcPrimary.h"
#include "PrimaryReplication.h"
#include "nsblast/logging.h"

using namespace std;
using namespace std::string_literals;
using namespace std::chrono_literals;

namespace nsblast::lib {

namespace {


} // anon ns

GrpcPrimary::GrpcPrimary(Server &server)
    : owner_{server}
{
}

void GrpcPrimary::start()
{
    auto server_address = owner_.config().cluster_server_addr;
    impl_ = make_unique<NsblastSvcImpl>(*this);

    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(impl_.get());
    svc_ = builder.BuildAndStart();
    LOG_INFO << "gRPC (cluster) Server listening on " << server_address;
}

void GrpcPrimary::stop()
{
    if (svc_) {
        LOG_INFO << "Grpc::stop - Shutting down gRPC service.";
        svc_->Shutdown();

        LOG_DEBUG << "Waiting for gRPC to stop...";
        svc_->Wait();
        LOG_DEBUG << "gRPC has stopped.";
        svc_.reset();
    }

    if (impl_) {
        LOG_DEBUG << "Grpc::stop - Shutting down gRPC impl handlers.";
        impl_.reset();
    }
}

std::shared_ptr<GrpcPrimary::SyncClient> GrpcPrimary::get(const boost::uuids::uuid &uuid)
{
    lock_guard lock{mutex_};
    if (auto it = clients_.find(uuid); it != clients_.end()) {
        return it->second;
    }

    return {};
}

void GrpcPrimary::done(SyncClient &client)
{
    LOG_TRACE << "Grpc::done - Removing client " << client.uuid();
    lock_guard lock{mutex_};
    clients_.erase(client.uuid());
}

GrpcPrimary::bidi_sync_stream_t *GrpcPrimary::createSyncClient(grpc::CallbackServerContext *context)
{
    auto client = make_shared<SyncClient>(*this, *context);
    LOG_INFO << "Grpc::addSyncClient() - Created gRPC Sync client instance "
             << client->uuid()
             << " for RPC from peer " << context->peer();

    {
        lock_guard lock{mutex_};
        clients_[client->uuid()] = client;
    }

    return client.get();
}

GrpcPrimary::SyncClient::SyncClient(GrpcPrimary &grpc, grpc::CallbackServerContext &context)
    : grpc_{grpc}, context_{context}
{
    StartRead(&req_);
}

bool GrpcPrimary::SyncClient::enqueue(update_t update)
{
    lock_guard lock{mutex_};
    if (is_done_) {
        LOG_TRACE << "Grpc::Client::enqueue = - Client " << uuid()
                  << " is inactive and cannot add updates at this time.";
        return false;
    }

    pending_.emplace(std::move(update));
    flush();

    if (pending_.size() > grpc_.owner_.config().cluster_repl_agent_max_queue_size) {
        LOG_TRACE << "Grpc::Client::enqueue = - Client " << uuid()
                  << " bumped into the queue limit size of "
                  <<  grpc_.owner_.config().cluster_repl_agent_max_queue_size;
        return false;
    }

    return true;
}

void GrpcPrimary::SyncClient::OnDone()
{
    LOG_DEBUG << "GrpcPrimary::SyncClient::OnDone: RPC request "
              << uuid() << " is done.";

    if (replication_) {
        replication_->onDone();
    }

    grpc_.done(*this);
}

void GrpcPrimary::SyncClient::OnReadDone(bool ok)
{
    if (!ok) [[unlikely]] {
        LOG_DEBUG << "SyncClient::OnReadDone RPC read failed for " << uuid()
                  << ". This RPC request is done.";

        std::lock_guard lock{mutex_};
        is_done_ = true;
        return;
    }

    // I don't think we need a lock here, because we should not be called into again
    // until after we start a new read.
    if (!replication_) [[unlikely]] {
        // The first read sets up the link with replication
        replication_ = grpc_.owner_.primaryReplication().addAgent(shared_from_this());
    }

    assert(replication_);
    replication_->onTrxId(req_.startafter());

    req_.Clear();
    StartRead(&req_);
}

void GrpcPrimary::SyncClient::OnWriteDone(bool ok)
{
    lock_guard lock{mutex_};
    current_.reset();
    flush();
}

void GrpcPrimary::SyncClient::flush()
{
    if (!current_ && !pending_.empty()) {
        has_written_after_empty_queue_ = true;
        current_ = std::move(pending_.front());
        pending_.pop();
        return StartWrite(current_.get());
    }

    // The queue is empty. Nothing to write.
    if (replication_ && has_written_after_empty_queue_) {
        replication_->onQueueIsEmpty();
        has_written_after_empty_queue_ = false;
    }
}

GrpcPrimary::bidi_sync_stream_t *GrpcPrimary::NsblastSvcImpl::Sync(grpc::CallbackServerContext *context)
{
    return grpc_.createSyncClient(context);
}


} //ns
