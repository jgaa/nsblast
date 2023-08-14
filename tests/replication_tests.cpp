#include "gtest/gtest.h"

#include "TmpDb.h"

#include "nsblast/DnsMessages.h"
#include "nsblast/Server.h"
#include "nsblast/logging.h"

#include "PrimaryReplication.h"

#include "test_res.h"

using namespace std;
using namespace nsblast;
using namespace nsblast::lib;

namespace {

class MockSyncClient : public GrpcPrimary::SyncClientInterface {
public:
    using fn_t = function<bool(MockSyncClient&)>;

    class Fn {
    public:
        Fn(fn_t && fn)
            : fn{std::move(fn)} {}

        fn_t fn;
        promise<void> p;
        bool used = false;
    };

    MockSyncClient() = default;

    // SyncClientInterface interface
    bool enqueue(GrpcPrimary::update_t update) {
        if (queueUsed() + 1 >= queue_limit) {
            return false; // full
        }

        ++num_enqueued;
        updates.emplace_back(std::move(update));

        afterAnything();

        return true;
    }

    size_t queueUsed() const noexcept {
        return updates.size();
    }

    boost::uuids::uuid uuid() const noexcept {
        return uuid_;
    }


    // Set a trigger for a future.
    auto returnWhen(fn_t && fn) {
        fn_.emplace_back(std::move(fn));
        return fn_.back().p.get_future();
    }

    void afterAnything() {
        for(auto& fn : fn_) {
            if (!fn.used) {
                if (fn.fn(*this)) {
                    fn.used = true;
                    fn.p.set_value();
                }
            }
        }
    }

    const boost::uuids::uuid uuid_ = newUuid();
    size_t queue_limit = 16;
    size_t num_enqueued = 0;
    std::vector<Fn> fn_;
    std::vector<GrpcPrimary::update_t> updates;
};


} // anon ns

TEST(ReplicationPrimary, NewAgentNoBacklog) {

    MockServer ms;
    ms->config().cluster_role = "primary";
    ms.StartReplication();
    ms.startIoThreads();

    {
        auto client = make_shared<MockSyncClient>();
        EXPECT_EQ(client->queueUsed(), 0);

        auto replication_agent = ms.primaryReplication().addAgent(client);
        // Get it going
        auto& agent = reinterpret_cast<PrimaryReplication::Agent &>(*replication_agent);
        auto future = agent.getTestFuture();
        replication_agent->onTrxId(0);

        // Wait for the agent to catch up and switch to streaming mode.
        EXPECT_EQ(future.wait_for(10s), std::future_status::ready);
        EXPECT_TRUE(replication_agent->isStreaming());

        // Create one mock transaction and verify that it's queued in the agent.
        auto update = make_shared<grpc::nsblast::pb::SyncUpdate>();

        auto repl_trx = make_unique<nsblast::pb::Transaction>();
        repl_trx->set_id(1);
        repl_trx->set_uuid(newUuidStr());
        repl_trx->set_node("this");
        repl_trx->add_parts();

        future = client->returnWhen([](MockSyncClient& mc) {
            return mc.num_enqueued == 1;
        });

        ms.primaryReplication().onTransaction(std::move(repl_trx));
        EXPECT_EQ(future.wait_for(10s), std::future_status::ready);
        EXPECT_EQ(client->queueUsed(), 1);
        EXPECT_TRUE(client->updates.back()->isinsync());
        EXPECT_EQ(client->updates.front()->trx().id(), 1);
    }
    ms.stop();
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);

    logfault::LogManager::Instance().AddHandler(
                make_unique<logfault::StreamHandler>(clog, logfault::LogLevel::TRACE));
    return RUN_ALL_TESTS();
}
