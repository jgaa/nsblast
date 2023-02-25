#include "gtest/gtest.h"
#include "RestApi.h"

#include "TmpDb.h"

#include "nsblast/DnsMessages.h"
#include "nsblast/logging.h"
#include "nsblast/DnsEngine.h"

#include "test_res.h"

using namespace std;
using namespace nsblast;
using namespace nsblast::lib;

namespace {

// UDP package from dig captured by wireshark
const char query_www_example_com[] =
        "\xd6\x01\x01\x20\x00\x01\x00\x00\x00\x00\x00\x01\x03\x77\x77\x77" \
        "\x07\x65\x78\x61\x6d\x70\x6c\x65\x03\x63\x6f\x6d\x00\x00\x01\x00" \
        "\x01\x00\x00\x29\x10\x00\x00\x00\x00\x00\x00\x0c\x00\x0a\x00\x08" \
        "\x91\x64\xec\x6d\x5e\xc9\x0e\x4e";


} // anon ns

TEST(DnsEngine, requestQueryA) {

    TmpDb db;
    db.createTestZone();
    db.createWwwA();

    DnsEngine dns{db.config(), db.resource()};

    DnsEngine::Request req;
    req.span = query_www_example_com;

    Message orig{query_www_example_com};

    MessageBuilder msg;
    dns.processRequest(req, msg);

    EXPECT_EQ(msg.header().id(), orig.header().id());
    EXPECT_EQ(msg.header().rcode(), Message::Header::RCODE::OK);
    EXPECT_EQ(msg.getQuestions().count(), orig.getQuestions().count());
    if (msg.getQuestions().count() > 0) {
        EXPECT_EQ(msg.getQuestions().begin()->type(), orig.getQuestions().begin()->type());
        EXPECT_EQ(msg.getQuestions().begin()->labels().string(), orig.getQuestions().begin()->labels().string());
    }

}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);

    logfault::LogManager::Instance().AddHandler(
                make_unique<logfault::StreamHandler>(clog, logfault::LogLevel::INFO));
    return RUN_ALL_TESTS();
}
