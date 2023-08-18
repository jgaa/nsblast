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
}

void GrpcFollow::stop()
{
    if (follower_) {
        follower_->stop();
    }

    follower_.reset();
}

void GrpcFollow::createSyncClient(due_t due, on_update_t onUpdate)
{
    assert(!due_);
    assert(!on_update_);

    LOG_DEBUG << "GrpcFollow::createSyncClient - Setting up sync from primary: "
              << server().config().cluster_server_addr;

    due_ = std::move(due);
    on_update_ = std::move(onUpdate);

    startFollower();
    scheduleNextTimer();
}

void GrpcFollow::scheduleNextTimer()
{
    timer_.expires_from_now(boost::posix_time::seconds{server().config().cluster_keepalive_timer});
    timer_.async_wait([this](boost::system::error_code ec) {
        if (!ec.failed()) {
            onTimer();
        }

        if (!stopped_) {
            scheduleNextTimer();
        }
    });
}

GrpcFollow::SyncFromServer::SyncFromServer(GrpcFollow &grpc,
                                           const std::string &address)
    : grpc_{grpc}
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
    StartRead(&update_);
}

void GrpcFollow::SyncFromServer::stop()
{
    // Tell the server we are done
    if (!done_) {
        done_ = true;
        writeIf();
    }

} // start


void GrpcFollow::SyncFromServer::timeout()
{
    writeIf();
}

void GrpcFollow::SyncFromServer::writeIf()
{
    lock_guard lock{mutex_};
    if (can_write_) {
        if (done_) [[unlikely]] {
            LOG_TRACE_N << "I am done. Starting to shut down the stream.";
            StartWritesDone();
            can_write_ = false;
            return;
        }
        if (auto ack = grpc_.due_()) {
            req_.set_level(grpc::nsblast::pb::SyncLevel::ENTRIES);
            req_.set_startafter(*ack);
            can_write_ = false;
            LOG_TRACE_N << "Asking for transactions from #" << *ack;
            StartWrite(&req_);
        }
    }
}

void GrpcFollow::SyncFromServer::ping()
{
    if (!done_) {
        LOG_TRACE_N << "Repeating the last request as a keep-alive message.";
        writeIf();
    }

    const auto max_response_time = chrono::steady_clock::now()
                         + chrono::seconds{grpc_.server().config().cluster_keepalive_timeout};
    if (grpc_.last_contact_.load() > max_response_time) {
        LOG_INFO_N << "We may have lost connectivity with the primary (cluster_keepalive_timeout="
                   << grpc_.server().config().cluster_keepalive_timeout
                   << " seconds).";
        static const grpc::nsblast::pb::SyncUpdate not_in_sync;
        callOnUpdate(not_in_sync);
    }
}

void GrpcFollow::SyncFromServer::OnReadDone(bool ok) {
    if (done_) [[unlikely]] {
        return;
    }
    if (!ok) [[unlikely]] {
        LOG_TRACE_N << "Not ok. Assuming the other end is shutting down.";
        stop();
    }

    grpc_.last_contact_ = chrono::steady_clock::now();
    callOnUpdate(update_);
    update_.Clear();
    StartRead(&update_);
}

void GrpcFollow::SyncFromServer::OnDone(const grpc::Status &s)
{
    LOG_INFO_N << "gRPC Replication is done. Status is " << s.error_message();
    self_.reset();
}

void GrpcFollow::startFollower()
{
    assert(!follower_);
    follower_ = make_shared<SyncFromServer>(*this, server().config().cluster_server_addr);
    follower_->start();
}

void GrpcFollow::onTimer()
{
    if (!stopped_) {
        if (auto f = follower_) {
            if (f->isDone()) {
                LOG_DEBUG_N << "Resetting the agent.";
                follower_.reset();
                return;
            }

            f->ping();
        } else if (due_){
            // No follower, but createSyncClient has been called. Let's create a new instance.
            startFollower();
        }
    }
}

void GrpcFollow::SyncFromServer::callOnUpdate(const grpc::nsblast::pb::SyncUpdate &update)
{
    assert(grpc_.on_update_);
    lock_guard lock{update_mutex_};
    grpc_.on_update_(update);
}


} // ns
