
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
    , auth_key_{getHashFromKeyInFileOrEnvVar(server.config().cluster_auth_key,
                                             "NSBLAST_CLUSTER_AUTH_KEY")}
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
        LOG_INFO_N << "Shutting down gRPC service.";
        svc_->Shutdown();

        LOG_DEBUG_N << "Waiting for gRPC to stop...";
        svc_->Wait();
        LOG_DEBUG_N << "gRPC has stopped.";
        svc_.reset();
    }

    if (impl_) {
        LOG_DEBUG_N << "Shutting down gRPC impl handlers.";
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
    LOG_TRACE_N << "Removing client " << client.uuid();
    lock_guard lock{mutex_};
    clients_.erase(client.uuid());
}

GrpcPrimary::bidi_sync_stream_t *GrpcPrimary::createSyncClient(grpc::CallbackServerContext *context)
{
    auto client = make_shared<SyncClient>(*this, *context);
    LOG_INFO_N << "Created gRPC Sync client instance "
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
        LOG_TRACE_N << "Client " << uuid()
                  << " is inactive and cannot add updates at this time.";
        return false;
    }

    pending_.emplace(std::move(update));
    flush();

    if (pending_.size() > grpc_.owner_.config().cluster_repl_agent_max_queue_size) {
        LOG_TRACE_N << "Client " << uuid()
                  << " bumped into the queue limit size of "
                  <<  grpc_.owner_.config().cluster_repl_agent_max_queue_size;
        return false;
    }

    return true;
}

void GrpcPrimary::SyncClient::OnDone()
{
    LOG_DEBUG_N << "RPC request " << uuid() << " is done.";

    if (replication_) {
        replication_->onDone();
    }

    grpc_.done(*this);
}

void GrpcPrimary::SyncClient::OnReadDone(bool ok)
{
    if (!ok) [[unlikely]] {
        LOG_DEBUG_N << "RPC read failed for " << uuid()
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

GrpcPrimary::bidi_sync_stream_t *GrpcPrimary::NsblastSvcImpl::Sync(
    grpc::CallbackServerContext *context)
{
    LOG_TRACE_N << "Was called from peer: " << context->peer();

    auto client_hash = context->client_metadata().find("auth-hash");
    auto client_seed = context->client_metadata().find("auth-seed");

    if (client_hash == context->client_metadata().end()) {
        LOG_WARN_N << "Connection from peer " << context->peer()
                   << " denied because the client did not send a auth-hash value";
        return {};
    }

    if (client_seed == context->client_metadata().end()) {
        LOG_WARN_N << "Connection from peer " << context->peer()
                   << " denied because the client did not send a auth-seed value";
        return {};
    }

    HashedKey my_hash;
    try {
        my_hash = getHashFromKeyInFileOrEnvVar(
            grpc_.owner_.config().cluster_auth_key,
            "NSBLAST_CLUSTER_AUTH_KEY",
            string{client_seed->second.begin(), client_seed->second.end()});

    } catch (const exception& ex) {
        LOG_ERROR_N << "Connection from peer " << context->peer()
                    << " denied because I failed to compute the auth-hash value: "
                    << ex.what();
        return {};
    }

    if (string_view{my_hash.hash}
        != string_view{client_hash->second.data(), client_hash->second.size()}) {
        LOG_WARN_N << "Connection from peer " << context->peer()
                   << " denied because the client did not provide a correct hashed auth-key value";
        return {};
    }


    return grpc_.createSyncClient(context);
}


} //ns
