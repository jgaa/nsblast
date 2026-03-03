#include <chrono>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

#include "gtest/gtest.h"

#include "TmpDb.h"

#include "nsblast/Server.h"
#include "nsblast/logging.h"

using namespace std;
using namespace std::chrono_literals;
using namespace nsblast;
using namespace nsblast::lib;

namespace {

bool waitFor(function<bool()> predicate, chrono::milliseconds timeout)
{
    const auto deadline = chrono::steady_clock::now() + timeout;
    while (chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }

        this_thread::sleep_for(50ms);
    }

    return predicate();
}

bool canBindLoopback()
{
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;

    const bool ok = ::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0;
    ::close(fd);
    return ok;
}

} // anon ns

TEST(ReplicationKeepalive, IdleStreamingFollowerStaysInSync) {
    if (!canBindLoopback()) {
        GTEST_SKIP() << "Loopback TCP sockets are unavailable in this environment";
    }

    ASSERT_EQ(setenv("NSBLAST_CLUSTER_AUTH_KEY", "nsblast-test-cluster-secret", 1), 0);

    auto primary_db = make_shared<TmpDb>();
    primary_db->config().cluster_server_addr = "127.0.0.1:19123";
    auto follower_db = make_shared<TmpDb>();
    follower_db->config().cluster_server_addr = "127.0.0.1:19123";
    follower_db->config().cluster_keepalive_timer = 1;
    follower_db->config().cluster_keepalive_timeout = 2;
    follower_db->config().cluster_ack_delay = 50;

    MockServer primary{primary_db};
    MockServer follower{follower_db};
    primary.setClusterRole("primary");
    follower.setClusterRole("follower");

    primary.initReplication();
    primary.StartReplication();
    primary.startGrpcService();
    primary.startForwardingTransactionsToReplication();
    primary.startIoThreads();

    follower.initReplication();
    follower.startGrpcService();
    follower.StartReplication();
    follower.startIoThreads();

    primary_db->createTestZone("example.com");

    const auto initial_sync_reached = waitFor([&] {
        return follower.db().getLastCommittedTransactionId() > 0 && follower.followerInSync();
    }, 10s);

    bool still_in_sync_after_idle = false;
    if (initial_sync_reached) {
        // Stay idle longer than cluster_keepalive_timeout. The follower should stay
        // in sync because the primary now replies to keepalive reads with an empty
        // heartbeat update.
        this_thread::sleep_for(3s);
        still_in_sync_after_idle = follower.followerInSync();
    }

    follower.stop();
    primary.stop();

    EXPECT_TRUE(initial_sync_reached);
    if (initial_sync_reached) {
        EXPECT_TRUE(still_in_sync_after_idle);
    }
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);

    logfault::LogManager::Instance().AddHandler(
        make_unique<logfault::StreamHandler>(clog, logfault::LogLevel::TRACE));
    return RUN_ALL_TESTS();
}
