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

// UDP package from dig captured by wireshark
const char query_example_com[] =
        "\xd6\x86\x01\x20\x00\x01\x00\x00\x00\x00\x00\x01\x07\x65\x78\x61" \
        "\x6d\x70\x6c\x65\x03\x63\x6f\x6d\x00\x00\xff\x00\x01\x00\x00\x29" \
        "\x10\x00\x00\x00\x00\x00\x00\x0c\x00\x0a\x00\x08\xb9\x72\xa1\xe6" \
        "\x66\x5e\xe1\x97";

} // anon ns

TEST(DnsEngine, requestQueryA) {

    TmpDb db;
    db.createTestZone();
    db.createWwwA();

    DnsEngine dns{db.config(), db.resource()};

    DnsEngine::Request req;
    req.span = query_www_example_com;

    Message orig{query_www_example_com};


    shared_ptr<MessageBuilder> mb;
    auto cb = [&mb](shared_ptr<MessageBuilder>& data, bool final) {
        mb = data;
        EXPECT_TRUE(final);
    };

    dns.processRequest(req, cb);
    Message msg{mb->span()};

    EXPECT_EQ(msg.header().id(), orig.header().id());
    EXPECT_EQ(msg.header().rcode(), Message::Header::RCODE::OK);
    EXPECT_EQ(msg.getQuestions().count(), orig.getQuestions().count());
    if (msg.getQuestions().count() > 0) {
        EXPECT_EQ(msg.getQuestions().begin()->type(), orig.getQuestions().begin()->type());
        EXPECT_EQ(msg.getQuestions().begin()->labels().string(), orig.getQuestions().begin()->labels().string());
    }
}

TEST(DnsEngine, requestAllRespAll) {

    TmpDb db;
    db.config().udp_qany_response = "all";
    db.createTestZone();

    DnsEngine dns{db.config(), db.resource()};
    DnsEngine::Request req;
    req.span = query_example_com;

    Message orig{query_example_com};


    shared_ptr<MessageBuilder> mb;
    auto cb = [&mb](shared_ptr<MessageBuilder>& data, bool final) {
        mb = data;
        EXPECT_TRUE(final);
    };

    dns.processRequest(req, cb);
    Message msg{mb->span()};

    EXPECT_EQ(msg.header().id(), orig.header().id());
    EXPECT_EQ(msg.header().rcode(), Message::Header::RCODE::OK);
    EXPECT_EQ(msg.getQuestions().count(), orig.getQuestions().count());
    if (msg.getQuestions().count() > 0) {
        EXPECT_EQ(msg.getQuestions().begin()->type(), orig.getQuestions().begin()->type());
        EXPECT_EQ(msg.getQuestions().begin()->labels().string(), orig.getQuestions().begin()->labels().string());
    }
    EXPECT_EQ(msg.getAnswers().count(), 10);
}

TEST(DnsEngine, requestAllRespRelevant) {

    TmpDb db;
    db.config().udp_qany_response = "relevant";
    db.createTestZone();

    DnsEngine dns{db.config(), db.resource()};
    DnsEngine::Request req;
    req.span = query_example_com;

    Message orig{query_example_com};


    shared_ptr<MessageBuilder> mb;
    auto cb = [&mb](shared_ptr<MessageBuilder>& data, bool final) {
        mb = data;
        EXPECT_TRUE(final);
    };

    dns.processRequest(req, cb);
    Message msg{mb->span()};

    EXPECT_EQ(msg.header().id(), orig.header().id());
    EXPECT_EQ(msg.header().rcode(), Message::Header::RCODE::OK);
    EXPECT_EQ(msg.getQuestions().count(), orig.getQuestions().count());
    if (msg.getQuestions().count() > 0) {
        EXPECT_EQ(msg.getQuestions().begin()->type(), orig.getQuestions().begin()->type());
        EXPECT_EQ(msg.getQuestions().begin()->labels().string(), orig.getQuestions().begin()->labels().string());
    }
    EXPECT_EQ(msg.getAnswers().count(), 5);
}

TEST(DnsEngine, requestAllRespHinfo) {

    TmpDb db;
    db.config().udp_qany_response = "hinfo";
    db.createTestZone();

    DnsEngine dns{db.config(), db.resource()};
    DnsEngine::Request req;
    req.span = query_example_com;

    Message orig{query_example_com};


    shared_ptr<MessageBuilder> mb;
    auto cb = [&mb](shared_ptr<MessageBuilder>& data, bool final) {
        mb = data;
        EXPECT_TRUE(final);
    };

    dns.processRequest(req, cb);
    Message msg{mb->span()};

    EXPECT_EQ(msg.header().id(), orig.header().id());
    EXPECT_EQ(msg.header().rcode(), Message::Header::RCODE::OK);
    EXPECT_EQ(msg.getQuestions().count(), orig.getQuestions().count());
    if (msg.getQuestions().count() > 0) {
        EXPECT_EQ(msg.getQuestions().begin()->type(), orig.getQuestions().begin()->type());
        EXPECT_EQ(msg.getQuestions().begin()->labels().string(), orig.getQuestions().begin()->labels().string());
    }
    EXPECT_EQ(msg.getAnswers().count(), 1);
    nsblast::lib::RrHinfo hinfo{msg.span(), msg.getAnswers().begin()->offset()};
    EXPECT_EQ(hinfo.cpu(), "RFC8482");
    EXPECT_TRUE(hinfo.os().empty());
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);

    logfault::LogManager::Instance().AddHandler(
                make_unique<logfault::StreamHandler>(clog, logfault::LogLevel::INFO));
    return RUN_ALL_TESTS();
}
