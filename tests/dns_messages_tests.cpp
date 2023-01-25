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

TEST(Labels, WithValidPointer) {
    char data[] = {"\003www\300\014XXXXXX\007example\003com"};

    // Validate that the offset to lhe last segment is octec 14 (dec 12)
    EXPECT_EQ(data[12], 007);

    optional<Labels> label;
    EXPECT_NO_THROW(label.emplace(data, 0));

    EXPECT_EQ(label->string(), "www.example.com"s);
    EXPECT_EQ(label->string(true), "www.example.com."s);
}

TEST(Labels, WithInvalidPointerOffBuffer) {
    char data[] = {"\003www\300\031XXXXXX\007example\003com"};

    // Validate that the offset to lhe last segment is octec 14 (dec 12)
    EXPECT_EQ(data[12], 007);
    EXPECT_THROW(Labels(data, 0), runtime_error);
}

TEST(Labels, WithInvalidPointerAtEndOfBuffer) {
    char data[] = {"\003www\300"};

    boost::span b{data, 5}; // We don't want the trailing 0
    EXPECT_THROW(Labels(b, 0), runtime_error);
}

TEST(Labels, WithRecursivePtr) {
    char data[] = {"\003www\300\000"};
    EXPECT_THROW(Labels(data, 0), runtime_error);
}

TEST(Labels, WithPtr2Ptr) {
    char first[] = {"\003www"};
    char last[] = {"\007example\003com"};
    std::vector<char> data;

    // Start with "www"
    boost::span<char> b{first};
    b = b.subspan(0, b.size() -1); // strip trailing zero
    std::copy(b.begin(), b.end(), std::back_inserter(data));

    // Create an array of 15 pointers to the next pointer
    for(auto i = 0; i < 15; i++) {
        data.push_back('\300');
        data.push_back(data.size() + 1);
    }

    // End with "example.com"
    b = {last};
    std::copy(b.begin(), b.end(), std::back_inserter(data));

    optional<Labels> label;
    EXPECT_NO_THROW(label.emplace(data, 0));

    EXPECT_EQ(label->string(), "www.example.com"s);
    EXPECT_EQ(label->string(true), "www.example.com."s);
}

TEST(Labels, With2ManyPtr2Ptr) {
    std::vector<char> data;

    // Create an array of 16 pointers to the next pointer
    for(auto i = 0; i < 16; i++) {
        data.push_back('\300');
        data.push_back(data.size() + 1);
    }

    EXPECT_THROW(Labels(data, 0), runtime_error);
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

// TODO: Add more tests with pointers

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
