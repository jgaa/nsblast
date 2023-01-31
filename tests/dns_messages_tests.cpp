
#include <cstring>
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
    optional<StorageBuilder::NewRr> rr;

    StorageBuilder sb;

    char b[] = "abcdefg";
    const boost::span data{b};
    const auto fqdn = "www.example.com"s;

    EXPECT_NO_THROW(rr.emplace(sb.createRr(fqdn, 1, 2, data)));

    const auto esize = fqdn.size() + 2 // first length field and root node length field
                       + 2 // type
                       + 2 // class
                       + 4 // ttl
                       + 2 // rdlength
                       + data.size(); // rdata;
    EXPECT_EQ(rr->size(), esize);

    // Validate labels using the RR's internal buffer-window
    const Labels rrlabel{rr->span(), 0};
    EXPECT_EQ(rrlabel.string(), "www.example.com"s);

    // Validate labels using the Message's buffer and the rr's offset.
    const Labels mblabel{sb.buffer(), rr->offset()};
    EXPECT_EQ(mblabel.string(), "www.example.com"s);
}

TEST(Rr, A) {
    StorageBuilder sb;

    auto ip = boost::asio::ip::address_v4::from_string("127.0.0.1");
    auto rr = sb.createRrA("www.example.com", 0, ip);

    EXPECT_EQ(rr.labels().string(), "www.example.com");

    EXPECT_EQ(rr.rdata().size(), 4);

    const auto bytes = ip.to_bytes();
    EXPECT_TRUE(memcmp(bytes.data(), rr.rdata().data(), 4) == 0);


}

TEST(Rr, AAAA) {
    StorageBuilder sb;

    auto ip = boost::asio::ip::address_v6::from_string("2001:0db8:85a3:0000:0000:8a2e:0370:7334");
    auto rr = sb.createRrA("www.example.com", 0, ip);

     EXPECT_EQ(rr.labels().string(), "www.example.com");

    EXPECT_EQ(rr.rdata().size(), 16);

    const auto bytes = ip.to_bytes();
    EXPECT_TRUE(memcmp(bytes.data(), rr.rdata().data(), 16) == 0);
}

TEST(Rr, GenericA) {
    StorageBuilder sb;

    auto ip = boost::asio::ip::address::from_string("127.0.0.1");
    auto rr = sb.createRrA("www.example.com", 0, ip);

    EXPECT_EQ(rr.labels().string(), "www.example.com");

    EXPECT_EQ(rr.rdata().size(), 4);

    const auto bytes = ip.to_v4().to_bytes();
    EXPECT_TRUE(memcmp(bytes.data(), rr.rdata().data(), 4) == 0);
}

TEST(Rr, GenericAAAA) {
    StorageBuilder sb;

    auto ip = boost::asio::ip::address::from_string("2001:0db8:85a3:0000:0000:8a2e:0370:7334");
    auto rr = sb.createRrA("www.example.com", 0, ip);

    EXPECT_EQ(rr.labels().string(), "www.example.com");

    EXPECT_EQ(rr.rdata().size(), 16);

    const auto bytes = ip.to_v6().to_bytes();
    EXPECT_TRUE(memcmp(bytes.data(), rr.rdata().data(), 16) == 0);
}

TEST(Rr, MultipleA) {
    StorageBuilder sb;

    auto ip1 = boost::asio::ip::address_v4::from_string("127.0.0.1");
    auto ip2 = boost::asio::ip::address_v4::from_string("127.0.0.2");
    auto ip3 = boost::asio::ip::address_v4::from_string("127.0.0.3");
    auto rr1 = sb.createRrA("www.example.com", 0, ip1);
    auto rr2 = sb.createRrA("ignored.example.com", 0, ip2);
    auto rr3 = sb.createRrA("", 0, ip3);

    EXPECT_EQ(rr1.labels().string(), "www.example.com");
    EXPECT_EQ(rr2.labels().string(), "www.example.com");
    EXPECT_EQ(rr3.labels().string(), "www.example.com");

    // All labels should point to to rr1.labels()
    EXPECT_EQ(rr1.labels().begin(), rr2.labels().begin());
    EXPECT_EQ(rr2.labels().begin(), rr3.labels().begin());

    EXPECT_EQ(rr1.rdata().size(), 4);
    EXPECT_EQ(rr2.rdata().size(), 4);
    EXPECT_EQ(rr3.rdata().size(), 4);

    auto bytes = ip1.to_bytes();
    EXPECT_TRUE(memcmp(bytes.data(), rr1.rdata().data(), 4) == 0);
    bytes = ip2.to_bytes();
    EXPECT_TRUE(memcmp(bytes.data(), rr2.rdata().data(), 4) == 0);
    bytes = ip3.to_bytes();
    EXPECT_TRUE(memcmp(bytes.data(), rr3.rdata().data(), 4) == 0);
}

TEST(Rr, parse) {
    StorageBuilder sb;

    string_view fdqn = "www.example.com";

    char data[] = "teste";

    auto nr = sb.createRr(fdqn, 123, 1000, data);

    // This parses the buffert for the RR
    // rr must now equal nr
    Rr rr{sb.buffer(), nr.offset()};

    EXPECT_EQ(rr.labels().string(), fdqn);
    EXPECT_EQ(rr.type(), 123);
    EXPECT_EQ(rr.ttl(), 1000);
    EXPECT_EQ(nr.rdata().size(), rr.rdata().size());
    EXPECT_EQ(nr.rdata().data(), rr.rdata().data());
    EXPECT_EQ(nr.labels().begin(), rr.labels().begin());
    EXPECT_EQ(nr.size(), rr.size());
    EXPECT_EQ(nr.offset(), rr.offset());
    EXPECT_EQ(nr.view().size(), rr.view().size());
    EXPECT_EQ(nr.view().data(), rr.view().data());
}

TEST(Rr, Soa) {
    StorageBuilder sb;

    string_view fdqn = "www.example.com";
    string_view mname = "ns1.example.com";
    string_view rname = "hostmaster@example.com";

    auto rr = sb.createSoa(fdqn, 9999, mname, rname,
                           1000, 1001, 1002, 1003, 1004);

    RrSoa soa{sb.buffer(), rr.offset()};

    EXPECT_EQ(soa.labels().string(), fdqn);
    EXPECT_EQ(soa.type(), nsblast::TYPE_SOA);
    EXPECT_EQ(soa.ttl(), 9999);

    EXPECT_EQ(soa.mname().string(), mname);
    EXPECT_EQ(soa.rname().string(), rname);
    EXPECT_EQ(soa.serial(), 1000);
    EXPECT_EQ(soa.refresh(), 1001);
    EXPECT_EQ(soa.retry(), 1002);
    EXPECT_EQ(soa.expire(), 1003);
    EXPECT_EQ(soa.minimum(), 1004);
}

// TODO: Add more tests with pointers

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
