//#include <boost/uuid/string_generator.hpp>

#include <grpc/grpc.h>

#include "GrpcFollow.h"
#include "nsblast/logging.h"
#include "nsblast/util.h"

using namespace std;
using namespace std::string_literals;
using namespace std::chrono_literals;


namespace nsblast::lib {

GrpcFollow::GrpcFollow(Server &server)
    : server_{server}
{

}

void GrpcFollow::start()
{
//    LOG_INFO_N << "Starting up gRPC follower framework.";
    //    restart();
}

void GrpcFollow::stop()
{
    if (follower_) {
        follower_->stop();
    }

    follower_.reset();
}

std::shared_ptr<GrpcFollow::SyncFromServer> GrpcFollow::createSyncClient(due_t due, on_update_t onUpdate)
{

    LOG_DEBUG << "GrpcFollow::createSyncClient - Setting up sync from primary: "
              << server().config().cluster_server_addr;

    follower_ = make_shared<SyncFromServer>(
        *this,
        server().config().cluster_server_addr,
        std::move(due), std::move(onUpdate));

    follower_->start();
    return follower_;
}


GrpcFollow::SyncFromServer::SyncFromServer(GrpcFollow &grpc,
                                           const std::string &address,
                                           due_t due,
                                           on_update_t onUpdate)
    : grpc_{grpc}, due_{std::move(due)}, on_update_{std::move(onUpdate)}
{
    LOG_INFO_N << "Setting up replication channel to " << address;

    channel_ = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
    if (auto status = channel_->GetState(false); status == GRPC_CHANNEL_TRANSIENT_FAILURE) {
        LOG_WARN << "Failed to initialize channel. Is the server address even valid?";
        throw std::runtime_error{"Failed to initialize channel"};
    }
}

void GrpcFollow::SyncFromServer::start()
{
    assert(channel_);
    self_ = shared_from_this();

    LOG_TRACE_N << "Starting gRPC async callback for Sync()";
    grpc_.stub_->async()->Sync(&ctx_, this);
    can_write_ = true;
    writeIf();

}

void GrpcFollow::SyncFromServer::stop()
{
    // Tell the server we are done
    if (!done_) {
        StartWritesDone();
        done_ = true;
    }

} // start




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
            LOG_TRACE_N << "Asking for transactions from #" << *ack;
            StartWrite(&req_);
        }
    }
}

void GrpcFollow::SyncFromServer::OnDone(const grpc::Status &s)
{
    LOG_INFO_N << "gRPC Replication is done. Status is " << s.error_message();
    self_.reset();
}


} // ns
