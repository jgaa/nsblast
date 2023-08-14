//#include <boost/uuid/string_generator.hpp>

#include <grpc/grpc.h>

#include "GrpcFollow.h"
#include "nsblast/logging.h"
#include "nsblast/util.h"

using namespace std;
using namespace std::string_literals;
using namespace std::chrono_literals;

ostream &operator <<(std::ostream &out, const nsblast::lib::GrpcFollow::SyncFromServer::Op op) {
    static const std::array<std::string_view, 4> names = {
        "CHANNEL", "READ", "WRITE", "DISCONNECT"
    };

    return out << names.at(static_cast<size_t>(op));
}


namespace nsblast::lib {

GrpcFollow::GrpcFollow(Server &server)
    : server_{server}
{

}

void GrpcFollow::start()
{
//    sync_client_ = createSyncClient(server_.config().cluster_server_addr);
//    sync_client_->start();
    grpc_not_so_async_thread_.emplace([this] {
        LOG_DEBUG << "GrpcSlave::start() - Worker-thread for gRPC slave is starting.";
        try {
            run();
        } catch (const exception& ex) {
            LOG_ERROR << "GrpcSlave::start() - Caught exception from run(): " << ex.what();
        }
        LOG_DEBUG << "GrpcSlave::start() - Worker-thread for gRPC slave is ending.";
    });
}

std::shared_ptr<GrpcFollow::SyncFromServer> GrpcFollow::createSyncClient(due_t due, on_update_t onUpdate)
{

    LOG_DEBUG << "GrpcFollow::createSyncClient - Setting up sync from primary: "
              << server().config().cluster_server_addr;

    sync_client_ = make_shared<SyncFromServer>(
        *this,
        server().config().cluster_server_addr,
        std::move(due), std::move(onUpdate));

    sync_client_->start();
    return sync_client_;
}

void GrpcFollow::run()
{
    while(true) {
        bool ok = true;
        void * tag = {};
        // TODO: Figure out how to use stable clock!
        const auto deadline = std::chrono::system_clock::now() + std::chrono::milliseconds(1000);
        auto status = cq_.AsyncNext(&tag, &ok, deadline);
        switch(status) {
            case grpc::CompletionQueue::NextStatus::TIMEOUT:
                LOG_TRACE << "GrpcSlave::run()- TIMEOUT from Next()";
                if (sync_client_) {
                    sync_client_->timeout();
                }
                continue;
            case grpc::CompletionQueue::NextStatus::GOT_EVENT:
                if (auto handle = static_cast<SyncFromServer::Handle *>(tag)) {
                    handle->proceed(ok);
                } else {
                    LOG_WARN << "GrpcSlave::run(): No tag from Next()!";
                }
                break;
            case grpc::CompletionQueue::NextStatus::SHUTDOWN:
                LOG_DEBUG << "GrpcSlave::run() - SHUTDOWN. Tearing down the gRPC slave connection(s) ";
                sync_client_.reset();
                return;
                break;
        } // switch
    }
}

GrpcFollow::SyncFromServer::SyncFromServer(GrpcFollow &grpc,
                                           const std::string &address,
                                           due_t due,
                                           on_update_t onUpdate)
    : grpc_{grpc}, due_{std::move(due)}, on_update_{std::move(onUpdate)}
{
    channel_ = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
}

void GrpcFollow::SyncFromServer::start()
{
    assert(channel_);
    stub_ = grpc::nsblast::pb::NsblastSvc::NewStub(channel_);
    rpc_ = stub_->AsyncSync(&ctx_, &grpc_.cq_, &channel_handle_);
}

void GrpcFollow::SyncFromServer::proceed(Op op, bool ok)
{
    LOG_TRACE << "GrpcSlave::SyncFromServer::proceed - "
              << uuid()
              << ' '
              << op
              << ' ' << (ok ? "OK" : "FAIL");

    switch(op) {
        case Op::CHANNEL:
            LOG_TRACE << "GrpcSlave::SyncFromServer::proceed Connected.";
            // Send initial request
        case Op::WRITE:
            // We can write again
            can_write_ = true;
            if (!ok) {
                LOG_WARN << "GrpcFollow::SyncFromServer::proceed - Failed to write. This connection is now unusable!";
                // TODO: Shut down this connection and re-try.
                break;
            }
            writeIf();
            break;
        case Op::READ:
            // Handle the transaction
            if (ok) {
                try {
                    on_update_(update_);
                } catch(const exception& ex) {
                LOG_ERROR << "GrpcFollow::SyncFromServer::proceed - Caught exception while forwarding transaction #"
                          << update_.trx().id()
                          << ": " << ex.what();
                }
            } else {
                LOG_WARN << "GrpcFollow::SyncFromServer::proceed - Failed to read.";
                break;
            }
            break;
        case Op::DISCONNECT:
            // quit
            ;
            break;
    } // switch op
}

void GrpcFollow::SyncFromServer::timeout()
{
    writeIf();
}

void GrpcFollow::SyncFromServer::writeIf()
{
    if (can_write_) {
        if (auto ack = due_()) {
            req_.set_level(grpc::nsblast::pb::SyncLevel::ENTRIES);
            req_.set_startafter(*ack);
            can_write_ = false;
            LOG_TRACE << "GrpcFollow::SyncFromServer::writeIf - Asking for transactions from #" << *ack;
            rpc_->Write(req_, &write_handle_);
        }
    }
}


} // ns
