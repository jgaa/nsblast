#include "gtest/gtest.h"

#include "nsblast/DnsMessages.h"

using namespace std;
using namespace nsblast::lib;

TEST(CreateMessageHeader, CheckingOpcodeQuery) {

    MessageBuilder mb;

    auto nh = mb.createHeader(1, true, Message::Header::OPCODE::QUERY, false);

    auto header = mb.header();
    EXPECT_EQ(header.opcode(), Message::Header::OPCODE::QUERY);
}

TEST(CreateMessageHeader, CheckingOpcodeIquery) {

    MessageBuilder mb;

    auto nh = mb.createHeader(1, true, Message::Header::OPCODE::IQUERY, false);

    auto header = mb.header();
    EXPECT_EQ(header.opcode(), Message::Header::OPCODE::IQUERY);
}

TEST(CreateMessageHeader, CheckingOpcodeStatus) {

    MessageBuilder mb;

    auto nh = mb.createHeader(1, true, Message::Header::OPCODE::STATUS, false);

    auto header = mb.header();
    EXPECT_EQ(header.opcode(), Message::Header::OPCODE::STATUS);
}

TEST(CreateMessageHeader, CheckingInvalidOpcode) {

    MessageBuilder mb;

    EXPECT_THROW(mb.createHeader(1, true, static_cast<Message::Header::OPCODE>(255), false),
                 runtime_error);
}

TEST(CreateMessageHeader, CheckingInvalidRcode) {

    MessageBuilder mb;

    auto nh = mb.createHeader(1, true, Message::Header::OPCODE::QUERY, false);
    EXPECT_THROW(nh.setRcode(Message::Header::RCODE::RESERVED_),
                 runtime_error);
}


TEST(CreateMessageHeader, CheckingBitsAndCounters) {

    MessageBuilder mb;

    auto nh = mb.createHeader(1, true, Message::Header::OPCODE::QUERY, false);

    auto header = mb.header();
    EXPECT_EQ(header.opcode(), Message::Header::OPCODE::QUERY);
    EXPECT_EQ(header.rcode(), Message::Header::RCODE::OK); // 0 is default
    EXPECT_TRUE(header.qr());
    EXPECT_FALSE(header.aa());
    EXPECT_FALSE(header.tc());
    EXPECT_FALSE(header.rd());
    EXPECT_FALSE(header.ra());
    EXPECT_EQ(header.z(), 0);
    EXPECT_EQ(header.id(), 1);
    EXPECT_EQ(header.qdcount(), 0);
    EXPECT_EQ(header.ancount(), 0);
    EXPECT_EQ(header.nscount(), 0);
    EXPECT_EQ(header.arcount(), 0);

    nh.setTc(true);
    EXPECT_TRUE(header.tc());
    EXPECT_EQ(header.opcode(), Message::Header::OPCODE::QUERY);
    EXPECT_EQ(header.rcode(), Message::Header::RCODE::OK); // 0 is default
    EXPECT_TRUE(header.qr());
    EXPECT_FALSE(header.aa());
    EXPECT_FALSE(header.rd());
    EXPECT_FALSE(header.ra());
    EXPECT_EQ(header.z(), 0);
    EXPECT_EQ(header.id(), 1);
    EXPECT_EQ(header.qdcount(), 0);
    EXPECT_EQ(header.ancount(), 0);
    EXPECT_EQ(header.nscount(), 0);
    EXPECT_EQ(header.arcount(), 0);

    nh.setRa(true);
    EXPECT_TRUE(header.tc());
    EXPECT_EQ(header.opcode(), Message::Header::OPCODE::QUERY);
    EXPECT_EQ(header.rcode(), Message::Header::RCODE::OK); // 0 is default
    EXPECT_TRUE(header.qr());
    EXPECT_FALSE(header.aa());
    EXPECT_FALSE(header.rd());
    EXPECT_TRUE(header.ra());
    EXPECT_EQ(header.z(), 0);
    EXPECT_EQ(header.id(), 1);
    EXPECT_EQ(header.qdcount(), 0);
    EXPECT_EQ(header.ancount(), 0);
    EXPECT_EQ(header.nscount(), 0);
    EXPECT_EQ(header.arcount(), 0);

    nh.setTc(false);
    EXPECT_FALSE    (header.tc());
    EXPECT_EQ(header.opcode(), Message::Header::OPCODE::QUERY);
    EXPECT_EQ(header.rcode(), Message::Header::RCODE::OK); // 0 is default
    EXPECT_TRUE(header.qr());
    EXPECT_FALSE(header.aa());
    EXPECT_FALSE(header.rd());
    EXPECT_TRUE(header.ra());
    EXPECT_EQ(header.z(), 0);
    EXPECT_EQ(header.id(), 1);
    EXPECT_EQ(header.qdcount(), 0);
    EXPECT_EQ(header.ancount(), 0);
    EXPECT_EQ(header.nscount(), 0);
    EXPECT_EQ(header.arcount(), 0);

    nh.incQdcount();
    EXPECT_FALSE    (header.tc());
    EXPECT_EQ(header.opcode(), Message::Header::OPCODE::QUERY);
    EXPECT_EQ(header.rcode(), Message::Header::RCODE::OK); // 0 is default
    EXPECT_TRUE(header.qr());
    EXPECT_FALSE(header.aa());
    EXPECT_FALSE(header.rd());
    EXPECT_TRUE(header.ra());
    EXPECT_EQ(header.z(), 0);
    EXPECT_EQ(header.id(), 1);
    EXPECT_EQ(header.qdcount(), 1);
    EXPECT_EQ(header.ancount(), 0);
    EXPECT_EQ(header.nscount(), 0);
    EXPECT_EQ(header.arcount(), 0);

    nh.incQdcount();
    EXPECT_FALSE    (header.tc());
    EXPECT_EQ(header.opcode(), Message::Header::OPCODE::QUERY);
    EXPECT_EQ(header.rcode(), Message::Header::RCODE::OK); // 0 is default
    EXPECT_TRUE(header.qr());
    EXPECT_FALSE(header.aa());
    EXPECT_FALSE(header.rd());
    EXPECT_TRUE(header.ra());
    EXPECT_EQ(header.z(), 0);
    EXPECT_EQ(header.id(), 1);
    EXPECT_EQ(header.qdcount(), 2);
    EXPECT_EQ(header.ancount(), 0);
    EXPECT_EQ(header.nscount(), 0);
    EXPECT_EQ(header.arcount(), 0);

    nh.incAncount();
    EXPECT_FALSE    (header.tc());
    EXPECT_EQ(header.opcode(), Message::Header::OPCODE::QUERY);
    EXPECT_EQ(header.rcode(), Message::Header::RCODE::OK); // 0 is default
    EXPECT_TRUE(header.qr());
    EXPECT_FALSE(header.aa());
    EXPECT_FALSE(header.rd());
    EXPECT_TRUE(header.ra());
    EXPECT_EQ(header.z(), 0);
    EXPECT_EQ(header.id(), 1);
    EXPECT_EQ(header.qdcount(), 2);
    EXPECT_EQ(header.ancount(), 1);
    EXPECT_EQ(header.nscount(), 0);
    EXPECT_EQ(header.arcount(), 0);

    nh.incNscount();
    EXPECT_FALSE    (header.tc());
    EXPECT_EQ(header.opcode(), Message::Header::OPCODE::QUERY);
    EXPECT_EQ(header.rcode(), Message::Header::RCODE::OK); // 0 is default
    EXPECT_TRUE(header.qr());
    EXPECT_FALSE(header.aa());
    EXPECT_FALSE(header.rd());
    EXPECT_TRUE(header.ra());
    EXPECT_EQ(header.z(), 0);
    EXPECT_EQ(header.id(), 1);
    EXPECT_EQ(header.qdcount(), 2);
    EXPECT_EQ(header.ancount(), 1);
    EXPECT_EQ(header.nscount(), 1);
    EXPECT_EQ(header.arcount(), 0);

    nh.incArcount();
    EXPECT_FALSE    (header.tc());
    EXPECT_EQ(header.opcode(), Message::Header::OPCODE::QUERY);
    EXPECT_EQ(header.rcode(), Message::Header::RCODE::OK); // 0 is default
    EXPECT_TRUE(header.qr());
    EXPECT_FALSE(header.aa());
    EXPECT_FALSE(header.rd());
    EXPECT_TRUE(header.ra());
    EXPECT_EQ(header.z(), 0);
    EXPECT_EQ(header.id(), 1);
    EXPECT_EQ(header.qdcount(), 2);
    EXPECT_EQ(header.ancount(), 1);
    EXPECT_EQ(header.nscount(), 1);
    EXPECT_EQ(header.arcount(), 1);

    nh.setRcode(Message::Header::RCODE::REFUSED);
    EXPECT_FALSE(header.tc());
    EXPECT_EQ(header.opcode(), Message::Header::OPCODE::QUERY);
    EXPECT_EQ(header.rcode(), Message::Header::RCODE::REFUSED);
    EXPECT_TRUE(header.qr());
    EXPECT_FALSE(header.aa());
    EXPECT_FALSE(header.rd());
    EXPECT_TRUE(header.ra());
    EXPECT_EQ(header.z(), 0);
    EXPECT_EQ(header.id(), 1);
    EXPECT_EQ(header.qdcount(), 2);
    EXPECT_EQ(header.ancount(), 1);
    EXPECT_EQ(header.nscount(), 1);
    EXPECT_EQ(header.arcount(), 1);
}

TEST(Cpp, SimpleArray) {
    char data[] = {"\003www\007example\003com"};
    boost::span s{data};

    // Just validate my assumption that octal expansion still works
    EXPECT_EQ(s.size(), ".www.example.com."s.size());
}

TEST(Labels, CreateSimpleOk) {
    char data[] = {"\003www\007example\003com"};
    optional<Labels> label;
    EXPECT_NO_THROW(label.emplace(data, 0));
    EXPECT_EQ(label->count(), 4); // www example com root
    EXPECT_EQ(label->size(), "www.example.com."s.size());
}

TEST(Labels, CreateLabelTooLong) {
    char data[] = {"\003www\100xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\003com"};
    EXPECT_THROW(Labels(data, 0), runtime_error);
}

TEST(Labels, CreateLabelAlmostTooLong) {
    char data[] = {"\003www\077xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\003com"};
    EXPECT_NO_THROW(Labels(data, 0));
}

TEST(Labels, CreateLabelsCombinedTooLong) {
    char data[] = {"\003www\077xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
                   "\077xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
                   "\077xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
                   "\077xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\003com"};
    EXPECT_THROW(Labels(data, 0), runtime_error);
}

TEST(Labels, CreateLabelsCombinedAlmostTooLong) {
    char data[] = {"\077xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
                   "\077xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
                   "\077xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
                   "\076xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"};
    EXPECT_NO_THROW(Labels(data, 0));
}

TEST(Labels, CreateLabelsCombinedExactelyTooLong) {
    char data[] = {"\077xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
                   "\077xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
                   "\077xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
                   "\077xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"};
    EXPECT_THROW(Labels(data, 0), runtime_error);
}

TEST(Labels, CreateExeedsBuffer) {
    char data[] = {"\003www\077xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"};
    EXPECT_THROW(Labels(data, 0), runtime_error);
}

TEST(Labels, CreateExeedsSmallBuffer) {
    char data[] = {"\003www\077"};
    EXPECT_THROW(Labels(data, 0), runtime_error);
}

TEST(Labels, ToString) {
    char data[] = {"\003www\007example\003com"};
    optional<Labels> label;
    EXPECT_NO_THROW(label.emplace(data, 0));

    EXPECT_EQ(label->string(), "www.example.com"s);
    EXPECT_EQ(label->string(true), "www.example.com."s);
}

TEST(Rr, CreateGeneral) {
    optional<MessageBuilder::NewRr> rr;

    MessageBuilder mb;

    char b[] = "abcdefg";
    const boost::span data{b};
    const auto fqdn = "www.example.com"s;

    EXPECT_NO_THROW(rr.emplace(mb.createRr(fqdn, 1, 2, data)));

    const auto esize = fqdn.size() + 2 // first length field and root node length field
                       + 2 // type
                       + 2 // class
                       + 4 // ttl
                       + 2 // rdlength
                       + data.size(); // rdata;
    EXPECT_EQ(rr->size(), esize);
}

// TODO: Add tests with pointers

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
