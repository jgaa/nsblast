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

    MockServer ms;
    ms->createTestZone();
    ms->createWwwA();

    DnsEngine dns{ms};

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

    MockServer ms;
    ms->config().udp_qany_response = "all";
    ms->createTestZone();

    DnsEngine dns{ms};
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

    MockServer ms;
    ms->config().udp_qany_response = "relevant";
    ms->createTestZone();

    DnsEngine dns{ms};
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

    MockServer ms;
    ms->config().udp_qany_response = "hinfo";
    ms->createTestZone();

    DnsEngine dns{ms};
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

TEST(DnsEngine, ednsWithoutOpt) {

    // "A" query without edns, captured with Wireshark
    const char query[] = "\x7c\x0a\x01\x20\x00\x01\x00\x00\x00\x00\x00\x00\x07\x65\x78\x61" \
"\x6d\x70\x6c\x65\x03\x63\x6f\x6d\x00\x00\x01\x00\x01";


    MockServer ms;
    ms->createTestZone();

    DnsEngine dns{ms};
    DnsEngine::Request req;
    req.span = query;

    Message orig{query};

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
    EXPECT_EQ(msg.getAnswers().count(), 2);
    EXPECT_EQ(msg.getAdditional().count(), 0);
}

TEST(DnsEngine, ednsWithOpt) {

    // "A" query with edns (version 0), captured with Wireshark
    const char query[] = "\xe2\xe2\x01\x20\x00\x01\x00\x00\x00\x00\x00\x01\x07\x65\x78\x61" \
"\x6d\x70\x6c\x65\x03\x63\x6f\x6d\x00\x00\x01\x00\x01\x00\x00\x29" \
"\x10\x00\x00\x00\x00\x00\x00\x0c\x00\x0a\x00\x08\xdd\x00\x69\x66" \
"\xc4\x23\x4f\x8f";


    MockServer ms;
    ms->createTestZone();

    DnsEngine dns{ms};
    DnsEngine::Request req;
    req.span = query;

    Message orig{query};

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
    EXPECT_EQ(msg.getAnswers().count(), 2);
    EXPECT_EQ(msg.getAdditional().count(), 1);
    EXPECT_EQ(msg.getAdditional().begin()->type(), TYPE_OPT);

    RrOpt opt{mb->span(), msg.getAdditional().begin()->offset()};
    EXPECT_EQ(opt.rcode(), 0);
    EXPECT_EQ(opt.version(), 0);
    EXPECT_EQ(opt.maxBufferLen(), MAX_UDP_QUERY_BUFFER_WITH_OPT);
}

TEST(DnsEngine, ednsWithUnsupportedVersion) {

    // "A" query with edns (version 0), captured with Wireshark
    const char query[] = "\xee\x19\x01\x20\x00\x01\x00\x00\x00\x00\x00\x01\x07\x65\x78\x61" \
"\x6d\x70\x6c\x65\x03\x63\x6f\x6d\x00\x00\x01\x00\x01\x00\x00\x29" \
"\x10\x00\x00\x01\x00\x00\x00\x0c\x00\x0a\x00\x08\x8a\xa3\x33\x1b" \
"\x02\x06\xeb\xff";

    MockServer ms;
    ms->createTestZone();

    DnsEngine dns{ms};
    DnsEngine::Request req;
    req.span = query;

    Message orig{query};

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
    EXPECT_EQ(msg.getAnswers().count(), 0);
    EXPECT_EQ(msg.getAdditional().count(), 1);
    EXPECT_EQ(msg.getAdditional().begin()->type(), TYPE_OPT);

    RrOpt opt{mb->span(), msg.getAdditional().begin()->offset()};
    EXPECT_EQ(opt.rcode(), 1);
    EXPECT_EQ(opt.version(), 0);
    EXPECT_EQ(opt.maxBufferLen(), MAX_UDP_QUERY_BUFFER_WITH_OPT);
}

TEST(DnsEngine, ednsWithTwoOptRrs) {

    // "A" query with edns (version 0), and two OPT RR's
    const char query[] = "\x9c\x68\x01\x20\x00\x01\x00\x00\x00\x00\x00\x02\x07\x65\x78\x61" \
"\x6d\x70\x6c\x65\x03\x63\x6f\x6d\x00\x00\x01\x00\x01\x00\x00\x29" \
"\x10\x00\x00\x00\x00\x00\x00\x0c\x00\x0a\x00\x08\x99\x4e\xb2\x53" \
"\xce\x04\x49\x81\x00\x00\x29\x10\x00\x00\x00\x00\x00\x00\x0c\x00\x0a\x00\x08\x99" \
"\x4e\xb2\x53\xce\x04\x49\x81";


    MockServer ms;
    ms->createTestZone();

    DnsEngine dns{ms};
    DnsEngine::Request req;
    req.span = query;

    Message orig{query};

    shared_ptr<MessageBuilder> mb;
    auto cb = [&mb](shared_ptr<MessageBuilder>& data, bool final) {
        mb = data;
        EXPECT_TRUE(final);
    };

    dns.processRequest(req, cb);
    Message msg{mb->span()};

    EXPECT_EQ(msg.header().id(), orig.header().id());
    EXPECT_EQ(msg.header().rcode(), Message::Header::RCODE::FORMAT_ERROR);
    EXPECT_EQ(msg.getQuestions().count(), orig.getQuestions().count());
    if (msg.getQuestions().count() > 0) {
        EXPECT_EQ(msg.getQuestions().begin()->type(), orig.getQuestions().begin()->type());
        EXPECT_EQ(msg.getQuestions().begin()->labels().string(), orig.getQuestions().begin()->labels().string());
    }
    EXPECT_EQ(msg.getAnswers().count(), 0);
    EXPECT_EQ(msg.getAdditional().count(), 1);
    EXPECT_EQ(msg.getAdditional().begin()->type(), TYPE_OPT);

    RrOpt opt{mb->span(), msg.getAdditional().begin()->offset()};
    EXPECT_EQ(opt.rcode(), 0);
    EXPECT_EQ(opt.version(), 0);
    EXPECT_EQ(opt.maxBufferLen(), MAX_UDP_QUERY_BUFFER_WITH_OPT);
}


int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);

    logfault::LogManager::Instance().AddHandler(
                make_unique<logfault::StreamHandler>(clog, logfault::LogLevel::INFO));
    return RUN_ALL_TESTS();
}
