//#include <boost/uuid/string_generator.hpp>

#include <grpc/grpc.h>

#include "GrpcFollow.h"
#include "nsblast/logging.h"
//#include "nsblast/util.h"
#include "nsblast/AckTimer.hpp"

using namespace std;
using namespace std::string_literals;
using namespace std::chrono_literals;


namespace nsblast::lib {

GrpcFollow::GrpcFollow(Server &server)
    : server_{server}
    , auth_key_{getHashFromKeyInFileOrEnvVar(server.config().cluster_auth_key,
                                             "NSBLAST_CLUSTER_AUTH_KEY")}
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

void GrpcFollow::createSyncClient(get_current_trxid_t due, on_update_t onUpdate)
{
    assert(!get_ack_t);
    assert(!on_update_);

    LOG_DEBUG << "GrpcFollow::createSyncClient - Setting up sync from primary: "
              << server().config().cluster_server_addr;

    get_ack_t = std::move(due);
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
    , ack_timer_{grpc.server().ctx(), [this] {
                     onAckTimer();
                 }
      }
{
    std::shared_ptr<grpc::ChannelCredentials> creds;

    string_view how = "plain text";

    if (!grpc_.server().config().cluster_x509_ca_cert.empty()) {
        grpc::SslCredentialsOptions opts;
        opts.pem_root_certs = readFileToBuffer(grpc_.server().config().cluster_x509_ca_cert);
        creds = grpc::SslCredentials(opts);
        how = "tls with x509";
    } else {
        creds = grpc::InsecureChannelCredentials();
    }

    assert(creds);

    LOG_INFO_N << "Setting up replication channel to " << address
               << " using a " << how << " connection.";

    channel_ = grpc::CreateChannel(address, creds);
    if (auto status = channel_->GetState(false); status == GRPC_CHANNEL_TRANSIENT_FAILURE) {
        LOG_WARN << "Failed to initialize channel. Is the server address even valid?";
        throw std::runtime_error{"Failed to initialize channel"};
    }

    stub_ = grpc::nsblast::pb::NsblastSvc::NewStub(channel_);
    assert(stub_);
}

void GrpcFollow::SyncFromServer::start()
{
    assert(channel_);
    self_ = shared_from_this();

    LOG_TRACE_N << "Starting gRPC async callback for Sync()";
    assert(stub_);
    ctx_.AddMetadata("auth-hash", grpc_.authKey().hash);
    ctx_.AddMetadata("auth-seed", grpc_.authKey().seed);
    stub_->async()->Sync(&ctx_, this);
    can_write_ = true;
    writeIf();
    StartRead(&update_);
    StartCall();
}

void GrpcFollow::SyncFromServer::stop()
{
    // Tell the server we are done
    if (!done_) {
        done_ = true;
        writeIf();
    }

} // start


bool GrpcFollow::SyncFromServer::writeIf()
{
    lock_guard lock{mutex_};

    if (can_write_) {
        if (done_) [[unlikely]] {
            LOG_TRACE_N << "I am done. Starting to shut down the stream.";
            StartWritesDone();
            can_write_ = false;
            return false;
        }

        const auto ack = grpc_.get_ack_t();
        req_.set_level(grpc::nsblast::pb::SyncLevel::ENTRIES);
        req_.set_startafter(ack);
        can_write_ = false;
        LOG_TRACE_N << "Asking for transactions from #" << ack;
        StartWrite(&req_);
        return true; // We started a write operation wrote
    } else {
        LOG_TRACE_N << "I can't write right now. Waiting for a write opperation to complete.";
    }

    return false;
}

void GrpcFollow::SyncFromServer::ping()
{
    if (!done_) {
        LOG_TRACE_N << "Repeating the last request as a keep-alive message.";
        if (!writeIf()) {
            // We were onable to write.
            // make sure there is a write later.
            startAckTimer();
        }
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

void GrpcFollow::SyncFromServer::OnWriteDone(bool ok) {
    lock_guard lock{mutex_};
    can_write_ = true;
}

void GrpcFollow::SyncFromServer::OnReadDone(bool ok) {
    if (done_) [[unlikely]] {
        return;
    }
    if (!ok) [[unlikely]] {
        LOG_TRACE_N << "Not ok. Assuming the other end is shutting down.";
        stop();
        return;
    }

    if (!was_connected_) {
        was_connected_ = true;
    }

    grpc_.last_contact_ = chrono::steady_clock::now();
    callOnUpdate(update_);
    update_.Clear();
    StartRead(&update_);
    startAckTimer();
}

void GrpcFollow::SyncFromServer::OnDone(const grpc::Status &s)
{
    if (!was_connected_) {
        LOG_ERROR_N << "Failed to establish connection to primary grpc server. "
                       "Is the 'cluster-auth-key' valid?";
    }

    LOG_INFO_N << "gRPC Replication is done. Status is " << s.error_message()
               << " was_connected_=" << was_connected_;

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
        } else if (get_ack_t){
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

void GrpcFollow::SyncFromServer::startAckTimer()
{
    if (done_) {
        return;
    }

    LOG_TRACE_N << "Maybe starting ACK timer";
    ack_timer_.startIfIdle(grpc_.server().config().cluster_ack_delay);
}

void GrpcFollow::SyncFromServer::onAckTimer()
{
    LOG_TRACE_N << "Called.";

    if (!writeIf()) {
        LOG_TRACE_N << "We did not write anything. Re-starting the ACK timer";
        startAckTimer();
    }
}


} // ns
