
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

    string_view fqdn = "www.example.com";

    char data[] = "teste";

    auto nr = sb.createRr(fqdn, 123, 1000, data);

    // This parses the buffert for the RR
    // rr must now equal nr
    Rr rr{sb.buffer(), nr.offset()};

    EXPECT_EQ(rr.labels().string(), fqdn);
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

TEST(RrSet, parse) {
    StorageBuilder sb;

    string_view fqdn = "example.com";
    string_view mname = "ns1.example.com";
    string_view rname = "hostmaster@example.com";


    auto ip1 = boost::asio::ip::address_v4::from_string("127.0.0.1");
    auto ip2 = boost::asio::ip::address_v4::from_string("127.0.0.2");
    auto ip3 = boost::asio::ip::address_v4::from_string("127.0.0.3");
    auto rr1 = sb.createRrA(fqdn, 5000, ip1);
    auto rr2 = sb.createRrA(fqdn, 5001, ip2);
    auto rr3 = sb.createRrA(fqdn, 5002, ip3);
    auto rrs = sb.createSoa(fqdn, 5003, mname, rname,
                           1000, 1001, 1002, 1003, 1004);

    RrSet rs{sb.buffer(), rr1.offset(), 4};
    EXPECT_EQ(rs.count(), 4);

    {
        uint32_t ttl = 5000;
        for(const auto& rr : rs) {
            EXPECT_EQ(rr.labels().string(), fqdn);
            EXPECT_EQ(rr.ttl(), ttl);
            ++ttl;
        }
    }

    auto it = rs.begin();
    EXPECT_NE(it, rs.end());

    auto bytes = ip1.to_bytes();
    EXPECT_TRUE(memcmp(bytes.data(), rr1.rdata().data(), 4) == 0);
    EXPECT_EQ(it->type(), nsblast::TYPE_A);
    EXPECT_EQ(it->ttl(), 5000);

    ++it;
    bytes = ip2.to_bytes();
    EXPECT_TRUE(memcmp(bytes.data(), rr2.rdata().data(), 4) == 0);
    EXPECT_EQ(it->type(), nsblast::TYPE_A);
    EXPECT_EQ(it->ttl(), 5001);

    ++it;
    bytes = ip3.to_bytes();
    EXPECT_TRUE(memcmp(bytes.data(), rr3.rdata().data(), 4) == 0);
    EXPECT_EQ(it->type(), nsblast::TYPE_A);
    EXPECT_EQ(it->ttl(), 5002);

    ++it;
    EXPECT_EQ(it->labels().string(), fqdn);
    EXPECT_EQ(it->type(), nsblast::TYPE_SOA);
    EXPECT_EQ(it->ttl(), 5003);

    RrSoa soa{sb.buffer(), it->offset()};
    EXPECT_EQ(soa.labels().string(), fqdn);
    EXPECT_EQ(soa.type(), nsblast::TYPE_SOA);
    EXPECT_EQ(soa.ttl(), 5003);
}

TEST(Rr, Soa) {
    StorageBuilder sb;

    string_view fqdn = "www.example.com";
    string_view mname = "ns1.example.com";
    string_view rname = "hostmaster@example.com";

    auto rr = sb.createSoa(fqdn, 9999, mname, rname,
                           1000, 1001, 1002, 1003, 1004);

    RrSoa soa{sb.buffer(), rr.offset()};

    EXPECT_EQ(soa.labels().string(), fqdn);
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

TEST(Rr, Cname) {
    StorageBuilder sb;

    string_view fqdn = "example.com";
    string_view cname = "blogs.example.com";

    auto rr = sb.createCname(fqdn, 1000, cname);

    EXPECT_EQ(rr.labels().string(), fqdn);

    RrCname cn{sb.buffer(), rr.offset()};

    EXPECT_EQ(cn.labels().string(), fqdn);
    EXPECT_EQ(cn.type(), nsblast::TYPE_CNAME);
    EXPECT_EQ(cn.ttl(), 1000);
    EXPECT_EQ(cn.cname().string(), cname);
}

TEST(Rr, Ns) {
    StorageBuilder sb;

    string_view fqdn = "example.com";
    string_view ns = "ns1.example.com";

    auto rr = sb.createNs(fqdn, 1000, ns);

    EXPECT_EQ(rr.labels().string(), fqdn);

    RrNs dn{sb.buffer(), rr.offset()};

    EXPECT_EQ(dn.labels().string(), fqdn);
    EXPECT_EQ(dn.type(), nsblast::TYPE_NS);
    EXPECT_EQ(dn.ttl(), 1000);
    EXPECT_EQ(dn.ns().string(), ns);
}


TEST(Rr, TxtSimple) {
    StorageBuilder sb;

    string_view fqdn = "example.com";
    string_view txt = "Just some simple text";

    auto rr = sb.createTxt(fqdn, 1000, txt);
    EXPECT_EQ(rr.labels().string(), fqdn);

    RrTxt rt{sb.buffer(), rr.offset()};
    EXPECT_EQ(rt.text().size(), 1);
    EXPECT_EQ(rt.text().at(0), txt);
    EXPECT_EQ(rt.string(), txt);
}

TEST(Rr, Txt250b) {
    StorageBuilder sb;

    string_view fqdn = "example.com";
    string_view txt = "Lorem ipsum dolor sit amet, consectetur adipiscing elit. "
        "Sed tristique sed felis ac ultricies. Pellentesque a sollicitudin magna, "
        "et mollis libero. Praesent at vestibulum ante. Vestibulum non purus ac "
        "diam imperdiet euismod. Suspendisse ipsum mi leo.";

    EXPECT_EQ(txt.size(), 250);

    auto rr = sb.createTxt(fqdn, 1000, txt);
    EXPECT_EQ(rr.labels().string(), fqdn);

    RrTxt rt{sb.buffer(), rr.offset()};
    EXPECT_EQ(rt.text().size(), 1);
    EXPECT_EQ(rt.text().at(0), txt);
    EXPECT_EQ(rt.string(), txt);
}

TEST(Rr, Txt1250b) {
    StorageBuilder sb;

    string_view fqdn = "example.com";
    string_view txt = "Lorem ipsum dolor sit amet, consectetur adipiscing elit. Vestibulum id rutrum purus. Vestibulum viverra accumsan nibh eu ornare. Maecenas ac efficitur mi. Donec bibendum pharetra iaculis. Donec dolor nulla, suscipit vitae orci nec, venenatis pulvinar tortor. Duis nec mauris ut orci fringilla pharetra. Phasellus vitae dolor id est dignissim tincidunt at ac odio. Aliquam sed leo tellus. Pellentesque faucibus, sem quis dignissim faucibus, lacus ipsum facilisis sapien, vel suscipit ex nibh tristique odio."
        "Morbi scelerisque eros sodales pretium imperdiet. Aliquam luctus, ex ut vulputate molestie, ante dui tincidunt arcu, sed tristique ligula turpis vel ipsum. Sed orci sapien, tristique id bibendum et, tristique at lectus. Maecenas id pulvinar nunc. Donec nibh tortor, imperdiet et lacus nec, interdum egestas sapien. Duis pellentesque purus et felis congue, vel tincidunt urna scelerisque. Praesent rutrum nisl ligula, sit amet accumsan felis pellentesque at. Fusce aliquam egestas dui, eget tristique nibh varius vel. Sed quis laoreet augue. Fusce eu nunc et nibh auctor semper. Integer tempus lectus in est interdum, quis mattis turpis tincidunt. Sed malesuada dui erat, a vulputate purus tempor nec. Duis ornare elementum rutrum. Sed neque in.";

    EXPECT_EQ(txt.size(), 1250);

    auto rr = sb.createTxt(fqdn, 1000, txt, true);
    EXPECT_EQ(rr.labels().string(), fqdn);

    RrTxt rt{sb.buffer(), rr.offset()};
    EXPECT_EQ(rt.text().size(), 5);
    EXPECT_EQ(rt.text().at(0), txt.substr(0, 255));
    EXPECT_EQ(rt.text().at(1), txt.substr(255, 255));
    EXPECT_EQ(rt.string(), txt);
}

TEST(Rr, TxtRdataSegments) {
    StorageBuilder sb;

    string_view fqdn = "example.com";
    array<string_view, 4> txt = {"This is a test", "Lorem ipsum dolor sit amet", "Morbi scelerisque eros sodales pretium imperdiet. ", {}};

    auto rr = sb.createTxtRdata(fqdn, 1000, txt);
    EXPECT_EQ(rr.labels().string(), fqdn);

    RrTxt rt{sb.buffer(), rr.offset()};
    EXPECT_EQ(rt.text().size(), 4);
    EXPECT_EQ(rt.text().at(0), txt.at(0));
    EXPECT_EQ(rt.text().at(1), txt.at(1));
    EXPECT_EQ(rt.text().at(2), txt.at(2));
    EXPECT_EQ(rt.text().at(3), string_view{}); // empty
}

TEST(Rr, TxtOverflowSimple) {
    StorageBuilder sb;

    string_view fqdn = "example.com";
    string_view txt = "Lorem ipsum dolor sit amet, consectetur adipiscing elit. Aliquam laoreet magna quis metus posuere hendrerit. Aliquam pretium sem justo, id euismod nibh dapibus ac. Quisque lobortis lacus eget nibh vulputate, sit amet pharetra ante mattis. Nullam cras amet.";

    EXPECT_EQ(txt.size(), 256);
    EXPECT_THROW(sb.createTxt(fqdn, 1000, txt), runtime_error);
}

TEST(Rr, TxtOverflowSegment) {
    StorageBuilder sb;

    string_view fqdn = "example.com";

    string_view overflow = "Lorem ipsum dolor sit amet, consectetur adipiscing elit. Aliquam laoreet magna quis metus posuere hendrerit. Aliquam pretium sem justo, id euismod nibh dapibus ac. Quisque lobortis lacus eget nibh vulputate, sit amet pharetra ante mattis. Nullam cras amet.";
    array<string_view, 4> txt = {"This is a test", "Lorem ipsum dolor sit amet", "Morbi scelerisque eros sodales pretium imperdiet. ", overflow};

    EXPECT_THROW(sb.createTxtRdata(fqdn, 1000, txt), runtime_error);
}

TEST(Rr, TxtOverflowTotal) {
    StorageBuilder sb;

    string_view fqdn = "example.com";
    string_view txt = "Lorem ipsum dolor sit amet, consectetur adipiscing elit. "
        "Sed tristique sed felis ac ultricies. Pellentesque a sollicitudin magna, "
        "et mollis libero. Praesent at vestibulum ante. Vestibulum non purus ac "
        "diam imperdiet euismod. Suspendisse ipsum mi leo.";

    EXPECT_EQ(txt.size(), 250);

    vector<string_view> v;
    for(size_t i = 0; i < 32; ++i) {
        v.emplace_back(txt);
    }

    auto rr = sb.createTxtRdata(fqdn, 1000, v);
    RrTxt rt{sb.buffer(), rr.offset()};
    EXPECT_EQ(rt.text().size(), 32);

    v.emplace_back(txt);
    EXPECT_THROW(sb.createTxtRdata(fqdn, 1000, v), runtime_error);
}

TEST(Rr, Mx) {
    StorageBuilder sb;

    string_view fqdn = "example.com";
    string_view host = "mail.example.com";

    auto rr = sb.createMx(fqdn, 1000, 10, host);

    EXPECT_EQ(rr.labels().string(), fqdn);

    RrMx dn{sb.buffer(), rr.offset()};

    EXPECT_EQ(dn.labels().string(), fqdn);
    EXPECT_EQ(dn.type(), nsblast::TYPE_MX);
    EXPECT_EQ(dn.ttl(), 1000);
    EXPECT_EQ(dn.host().string(), host);
    EXPECT_EQ(dn.priority(), 10);
}

TEST(StorageBuilder, SingleA) {
    StorageBuilder sb;
    string_view fqdn = "example.com";
    auto ip1 = boost::asio::ip::address_v4::from_string("127.0.0.1");

    sb.createRrA(fqdn, 1000, ip1);
    EXPECT_NO_THROW(sb.finish());

    EXPECT_EQ(sb.rrCount(), 1);
    EXPECT_EQ(sb.header().flags.a, true);
    EXPECT_EQ(sb.header().flags.aaaa, false);
    EXPECT_EQ(sb.header().flags.soa, false);
    EXPECT_EQ(sb.header().flags.ns, false);
    EXPECT_EQ(sb.header().flags.txt, false);
    EXPECT_EQ(sb.header().flags.cname, false);
}

// TODO: Add more tests with pointers

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
