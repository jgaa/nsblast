#include "gtest/gtest.h"

#include "TmpDb.h"

#include "nsblast/DnsMessages.h"
#include "nsblast/Server.h"
#include "nsblast/logging.h"

#include "Replication.h"

#include "test_res.h"

using namespace std;
using namespace nsblast;
using namespace nsblast::lib;

namespace {

class MockSyncClient : public GrpcPrimary::SyncClientInterface {
public:
    MockSyncClient() = default;

    // SyncClientInterface interface
    bool enqueue(GrpcPrimary::update_t update) {
        if (queue_used + 1 >= queue_limit) {
            return false; // full
        }

        ++queue_used;
        return true;
    }

    boost::uuids::uuid uuid() const noexcept {
        return uuid_;
    }

    const boost::uuids::uuid uuid_ = newUuid();
    size_t queue_used = 0;
    size_t queue_limit = 16;
};


} // anon ns

TEST(ReplicationPrimary, NewAgentNoBacklog) {

    MockServer ms;
    ms->config().cluster_role = "primary";
    ms.StartReplication();

    {
        auto client = make_shared<MockSyncClient>();
        EXPECT_EQ(client->queue_used, 0);

        auto replication_agent = ms.replication().addAgent(client);
        // Get it going
        auto future = reinterpret_cast<Replication::FollowerAgent &>(*replication_agent).getTestFuture();
        replication_agent->onTrxId(0);

        // Wait for the agent to catch up and switch to streaming mode.0
        EXPECT_EQ(future.wait_for(10s), std::future_status::ready);
        EXPECT_TRUE(replication_agent->isStreaming());
    }
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);

    logfault::LogManager::Instance().AddHandler(
                make_unique<logfault::StreamHandler>(clog, logfault::LogLevel::TRACE));
    return RUN_ALL_TESTS();
}
