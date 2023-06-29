//#include <boost/uuid/string_generator.hpp>

#include <grpc/grpc.h>

#include "GrpcFollow.h"
#include "Replication.h"
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

std::unique_ptr<GrpcFollow::SyncFromServer> GrpcFollow::createSyncClient(const std::string &address)
{
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
                continue;
            case grpc::CompletionQueue::NextStatus::GOT_EVENT: {
                if (auto handle = static_cast<SyncFromServer::Handle *>(tag)) {
                    handle->proceed(ok);
                } else {
                    LOG_WARN << "GrpcSlave::run(): No tag from Next()!";
                }
            case grpc::CompletionQueue::NextStatus::SHUTDOWN:
                LOG_DEBUG << "GrpcSlave::run() - SUTDOWN. Tearing down the gRPC slave connection(s) ";
                sync_client_.reset();
                return;
            } break;
        } // switch
    }
}

GrpcFollow::SyncFromServer::SyncFromServer(GrpcFollow &grpc, const std::string &address)
    : grpc_{grpc}
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
            break;
        case Op::READ:
            // Handle the transaction
            // Set timer to write if appropriate
            break;
        case Op::DISCONNECT:
            // quit
            ;
            break;
    } // switch op
}


} // ns
