
#include <cstring>
#include "gtest/gtest.h"

#include "nsblast/DnsMessages.h"
#include "nsblast/detail/write_labels.hpp"

using namespace std;
using namespace nsblast::lib;

namespace {

using namespace nsblast;

struct WriteLabelsSetup {

    WriteLabelsSetup() {
        // Buffer can not be re-allocated, as "existing" maintains pointers into the buffer
        buffer.reserve(1024);
    }

    auto add(string_view fqdn) {
        labels_buffer.resize(255);

        nsblast::lib::detail::writeName(labels_buffer, 0, fqdn);
        Labels labels{labels_buffer, 0};

        uint16_t start_offset = static_cast<uint16_t>(buffer.size());

        auto len = nsblast::lib::detail::writeLabels(labels, existing, buffer, nsblast::MAX_UDP_QUERY_BUFFER);

        latest = {buffer, start_offset};

        return len;
    }

    vector<char> labels_buffer;
    vector<char> buffer;
    deque<Labels> existing;
    Labels latest;
};

bool hasPointer(span_t labels) {

    for(auto it = labels.begin(); it < labels.end(); ++it) {
        if (*it & nsblast::lib::detail::START_OF_POINTER_TAG) {
            return true;
        }

        // Jump to the next label
        assert(*it <= 64);
        it += *it;
    }

    return false;
}

// Weak validation. Assumes that the data is valid!
template <typename T>
size_t numPointers(const T& buffer, uint16_t offset) {
    size_t count = 0;
    while(true) {
        auto ch = buffer.at(offset);
        if (ch & nsblast::lib::detail::START_OF_POINTER_TAG) {
            offset = nsblast::lib::detail::resolvePtr(buffer, offset);
            ++count;
        } else if (ch) {
            offset += ch + 1;
        } else {
            break;
        }
    }
    return count;
}

} // anon ns

TEST(WriteLabels, simpleOk) {
    WriteLabelsSetup wls;

    const string_view fqdn = "example.com";

    auto len = wls.add(fqdn);

    EXPECT_EQ(len, fqdn.size() + 2);
    EXPECT_EQ(wls.latest.string(), fqdn);
    EXPECT_EQ(wls.existing.size(), 1);
    EXPECT_EQ(wls.existing.at(0).string(), fqdn);
    EXPECT_FALSE(wls.latest.empty());
    EXPECT_FALSE(hasPointer(wls.latest.selfView()));
}

TEST(WriteLabels, compressionOk) {
    WriteLabelsSetup wls;

    const string_view fqdn = "example.com";
    const string_view fqdn2 = "ns1.example.com";
    const string_view fqdn3 = "ns2.example.com";
    const string_view fqdn4 = "www.a.b.c.example.com";
    const string_view fqdn5 = "c.example.com";
    const string_view fqdn6 = "ns1.nsblast.com";

    wls.add(fqdn);
    EXPECT_EQ(wls.latest.string(), fqdn);
    EXPECT_EQ(numPointers(wls.buffer, wls.latest.offset()), 0);
    EXPECT_EQ(wls.existing.size(), 1);
    EXPECT_EQ(wls.existing.at(0).string(), fqdn);
    EXPECT_FALSE(hasPointer(wls.latest.selfView()));

    wls.add(fqdn2);
    EXPECT_EQ(wls.latest.string(), fqdn2);
    EXPECT_EQ(wls.latest.bytes(), 6); // "ns1" + len (1) + ptr (2)
    EXPECT_TRUE(hasPointer(wls.latest.selfView()));
    EXPECT_EQ(numPointers(wls.buffer, wls.latest.offset()), 1);
    EXPECT_EQ(wls.existing.size(), 2);
    EXPECT_EQ(wls.existing.at(0).string(), fqdn);
    EXPECT_EQ(wls.existing.at(1).string(), fqdn2);

    wls.add(fqdn3);
    EXPECT_EQ(wls.latest.string(), fqdn3);
    EXPECT_EQ(wls.latest.bytes(), 6); // "ns2" + len (1) + ptr (2)
    EXPECT_TRUE(hasPointer(wls.latest.selfView()));
    EXPECT_EQ(numPointers(wls.buffer, wls.latest.offset()), 1);
    EXPECT_EQ(wls.existing.size(), 3);
    EXPECT_EQ(wls.existing.at(0).string(), fqdn);
    EXPECT_EQ(wls.existing.at(1).string(), fqdn2);
    EXPECT_EQ(wls.existing.at(2).string(), fqdn3);

    wls.add(fqdn4);
    EXPECT_EQ(wls.latest.string(), fqdn4);
    EXPECT_EQ(wls.latest.bytes(), 12); // "www.a.b.c" + len (1) + ptr (2)
    EXPECT_TRUE(hasPointer(wls.latest.selfView()));
    EXPECT_EQ(numPointers(wls.buffer, wls.latest.offset()), 1);
    EXPECT_EQ(wls.existing.size(), 4);
    EXPECT_EQ(wls.existing.at(0).string(), fqdn);
    EXPECT_EQ(wls.existing.at(1).string(), fqdn2);
    EXPECT_EQ(wls.existing.at(2).string(), fqdn3);
    EXPECT_EQ(wls.existing.at(3).string(), fqdn4);

    wls.add(fqdn5);
    EXPECT_EQ(wls.latest.string(), fqdn5);
    EXPECT_EQ(wls.latest.bytes(), 2); // ptr (2)
    EXPECT_TRUE(hasPointer(wls.latest.selfView()));
    EXPECT_EQ(numPointers(wls.buffer, wls.latest.offset()), 2); // ptr --> c --> example.com
    EXPECT_EQ(wls.existing.size(), 4); // No new existing entry expected here
    EXPECT_EQ(wls.existing.at(0).string(), fqdn);
    EXPECT_EQ(wls.existing.at(1).string(), fqdn2);
    EXPECT_EQ(wls.existing.at(2).string(), fqdn3);
    EXPECT_EQ(wls.existing.at(3).string(), fqdn4);

    wls.add(fqdn6);
    EXPECT_EQ(wls.latest.string(), fqdn6);
    EXPECT_EQ(wls.latest.bytes(), 14); // "ns1.nsblast" (11) + len (1) ptr (2)
    EXPECT_TRUE(hasPointer(wls.latest.selfView()));
    EXPECT_EQ(numPointers(wls.buffer, wls.latest.offset()), 1);
    EXPECT_EQ(wls.existing.size(), 5);
    EXPECT_EQ(wls.existing.at(0).string(), fqdn);
    EXPECT_EQ(wls.existing.at(1).string(), fqdn2);
    EXPECT_EQ(wls.existing.at(2).string(), fqdn3);
    EXPECT_EQ(wls.existing.at(3).string(), fqdn4);
    EXPECT_EQ(wls.existing.at(4).string(), fqdn6);

    wls.add(fqdn2); // repeat
    EXPECT_EQ(wls.latest.string(), fqdn2);
    EXPECT_EQ(wls.latest.bytes(), 2); // ptr (2)
    EXPECT_TRUE(hasPointer(wls.latest.selfView()));
    EXPECT_EQ(numPointers(wls.buffer, wls.latest.offset()), 2); // ptr --> ns1 --> example.com
    EXPECT_EQ(wls.existing.size(), 5); // No new existing entry expected here
    EXPECT_EQ(wls.existing.at(0).string(), fqdn);
    EXPECT_EQ(wls.existing.at(1).string(), fqdn2);
    EXPECT_EQ(wls.existing.at(2).string(), fqdn3);
    EXPECT_EQ(wls.existing.at(3).string(), fqdn4);
    EXPECT_EQ(wls.existing.at(4).string(), fqdn6);
}


TEST(CreateMessageHeader, CheckingOpcodeQuery) {

    MessageBuilder mb;

    mb.createHeader(1, true, Message::Header::OPCODE::QUERY, false);

    auto header = mb.header();
    EXPECT_EQ(header.opcode(), Message::Header::OPCODE::QUERY);
}

TEST(CreateMessageHeader, CheckingOpcodeIquery) {

    MessageBuilder mb;

    mb.createHeader(1, true, Message::Header::OPCODE::IQUERY, false);

    auto header = mb.header();
    EXPECT_EQ(header.opcode(), Message::Header::OPCODE::IQUERY);
}

TEST(CreateMessageHeader, CheckingOpcodeStatus) {

    MessageBuilder mb;

    mb.createHeader(1, true, Message::Header::OPCODE::STATUS, false);

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

TEST(Labels, CreateOnlyRoot) {
    char data[] = {"\000"};
    optional<Labels> label;
    EXPECT_NO_THROW(label.emplace(data, 0));
    EXPECT_EQ(label->count(), 1); // root
    EXPECT_EQ(label->size(), 1); // Trailing dot
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

TEST(Rr, generalOnRootPath) {
    optional<StorageBuilder::NewRr> rr;

    StorageBuilder sb;

    EXPECT_NO_THROW(rr.emplace(sb.createRr(span_t{}, 1, 2, {})));

    const auto esize = 1   // root node
                       + 2 // type
                       + 2 // class
                       + 4 // ttl
                       + 2 // rdlength
                       ;
    EXPECT_EQ(rr->size(), esize);

    // Validate labels using the RR's internal buffer-window
    const Labels rrlabel{rr->span(), 0};
    EXPECT_EQ(rrlabel.string(), ""s);

    // Validate labels using the Message's buffer and the rr's offset.
    const Labels mblabel{sb.buffer(), rr->offset()};
    EXPECT_EQ(mblabel.string(), ""s);
}

TEST(Rr, A) {
    StorageBuilder sb;

    auto ip = boost::asio::ip::address_v4::from_string("127.0.0.1");
    auto rr = sb.createA("www.example.com", 0, ip);

    EXPECT_EQ(rr.labels().string(), "www.example.com");

    EXPECT_EQ(rr.rdata().size(), 4);

    const auto bytes = ip.to_bytes();
    EXPECT_TRUE(memcmp(bytes.data(), rr.rdata().data(), 4) == 0);
}

TEST(Rr, AStr) {
    StorageBuilder sb;

    const string ip_str = "127.0.0.1";
    auto ip = boost::asio::ip::address_v4::from_string(ip_str);
    auto rr = sb.createA("www.example.com", 0, ip_str);

    EXPECT_EQ(rr.labels().string(), "www.example.com");

    EXPECT_EQ(rr.rdata().size(), 4);

    const auto bytes = ip.to_bytes();
    EXPECT_TRUE(memcmp(bytes.data(), rr.rdata().data(), 4) == 0);
}

TEST(Rr, AAAA) {
    StorageBuilder sb;

    auto ip = boost::asio::ip::address_v6::from_string("2001:0db8:85a3:0000:0000:8a2e:0370:7334");
    auto rr = sb.createA("www.example.com", 0, ip);

    EXPECT_EQ(rr.labels().string(), "www.example.com");

    EXPECT_EQ(rr.rdata().size(), 16);

    const auto bytes = ip.to_bytes();
    EXPECT_TRUE(memcmp(bytes.data(), rr.rdata().data(), 16) == 0);
}

TEST(Rr, AAAAStr) {
    StorageBuilder sb;

    const string ip_str = "2001:0db8:85a3:0000:0000:8a2e:0370:7334";
    auto ip = boost::asio::ip::address_v6::from_string(ip_str);
    auto rr = sb.createA("www.example.com", 0, ip_str);

    EXPECT_EQ(rr.labels().string(), "www.example.com");

    EXPECT_EQ(rr.rdata().size(), 16);

    const auto bytes = ip.to_bytes();
    EXPECT_TRUE(memcmp(bytes.data(), rr.rdata().data(), 16) == 0);
}


TEST(Rr, GenericA) {
    StorageBuilder sb;

    auto ip = boost::asio::ip::address::from_string("127.0.0.1");
    auto rr = sb.createA("www.example.com", 0, ip);

    EXPECT_EQ(rr.labels().string(), "www.example.com");

    EXPECT_EQ(rr.rdata().size(), 4);

    const auto bytes = ip.to_v4().to_bytes();
    EXPECT_TRUE(memcmp(bytes.data(), rr.rdata().data(), 4) == 0);
}

TEST(Rr, GenericAAAA) {
    StorageBuilder sb;

    auto ip = boost::asio::ip::address::from_string("2001:0db8:85a3:0000:0000:8a2e:0370:7334");
    auto rr = sb.createA("www.example.com", 0, ip);

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
    auto rr1 = sb.createA("www.example.com", 0, ip1);
    auto rr2 = sb.createA("ignored.example.com", 0, ip2);
    auto rr3 = sb.createA("", 0, ip3);

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

TEST(RrList, parse) {
    StorageBuilder sb;

    string_view fqdn = "example.com";
    string_view mname = "ns1.example.com";
    string_view rname = "hostmaster@example.com";


    auto ip1 = boost::asio::ip::address_v4::from_string("127.0.0.1");
    auto ip2 = boost::asio::ip::address_v4::from_string("127.0.0.2");
    auto ip3 = boost::asio::ip::address_v4::from_string("127.0.0.3");
    auto rr1 = sb.createA(fqdn, 5000, ip1);
    auto rr2 = sb.createA(fqdn, 5001, ip2);
    auto rr3 = sb.createA(fqdn, 5002, ip3);
    sb.createSoa(fqdn, 5003, mname, rname,
                           1000, 1001, 1002, 1003, 1004);

    RrList rs{sb.buffer(), rr1.offset(), 4, false};
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

TEST(Rr, Rp) {
    StorageBuilder sb;

    string_view fqdn = "example.com";
    string_view mbox = "foo.example.com";
    string_view txt = "bar.example.com";

    auto rr = sb.createRp(fqdn, 1000, mbox, txt);

    EXPECT_EQ(rr.labels().string(), fqdn);

    RrRp rp{sb.buffer(), rr.offset()};

    EXPECT_EQ(rp.labels().string(), fqdn);
    EXPECT_EQ(rp.type(), nsblast::TYPE_RP);
    EXPECT_EQ(rp.ttl(), 1000);
    EXPECT_EQ(rp.mbox().string(), mbox);
    EXPECT_EQ(rp.txt().string(), txt);
}

TEST(Rr, Hinfo) {
    StorageBuilder sb;

    string_view fqdn = "example.com";
    string_view cpu = "foo";
    string_view os = "bar";

    auto rr = sb.createHinfo(fqdn, 1000, cpu, os);

    EXPECT_EQ(rr.labels().string(), fqdn);

    RrHinfo hinfo{sb.buffer(), rr.offset()};

    EXPECT_EQ(hinfo.labels().string(), fqdn);
    EXPECT_EQ(hinfo.type(), nsblast::TYPE_HINFO);
    EXPECT_EQ(hinfo.ttl(), 1000);
    EXPECT_EQ(hinfo.cpu(), cpu);
    EXPECT_EQ(hinfo.os(), os);
}

TEST(Rr, HinfoEmptyOs) {
    StorageBuilder sb;

    string_view fqdn = "example.com";
    string_view cpu = "foo";
    string_view os = "";

    auto rr = sb.createHinfo(fqdn, 1000, cpu, os);

    EXPECT_EQ(rr.labels().string(), fqdn);

    RrHinfo hinfo{sb.buffer(), rr.offset()};

    EXPECT_EQ(hinfo.labels().string(), fqdn);
    EXPECT_EQ(hinfo.type(), nsblast::TYPE_HINFO);
    EXPECT_EQ(hinfo.ttl(), 1000);
    EXPECT_EQ(hinfo.cpu(), cpu);
    EXPECT_EQ(hinfo.os(), os);
}

TEST(Rr, PtrIpv4) {
    StorageBuilder sb;

    string_view fqdn = "in-addr.arpa";
    string_view ptr = "1.0.0.127";

    auto rr = sb.createPtr(fqdn, 1000, ptr);

    EXPECT_EQ(rr.labels().string(), fqdn);

    RrPtr cn{sb.buffer(), rr.offset()};

    EXPECT_EQ(cn.labels().string(), fqdn);
    EXPECT_EQ(cn.type(), nsblast::TYPE_PTR);
    EXPECT_EQ(cn.ttl(), 1000);
    EXPECT_EQ(cn.ptrdname().string(), ptr);
}

TEST(Rr, PtrIpv6) {
    StorageBuilder sb;

    string_view fqdn = "ip6.arpa";
    string_view ptr = "b.a.9.8.7.6.5.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.8.b.d.0.1.0.0.2";

    auto rr = sb.createPtr(fqdn, 1000, ptr);

    EXPECT_EQ(rr.labels().string(), fqdn);

    RrPtr cn{sb.buffer(), rr.offset()};

    EXPECT_EQ(cn.labels().string(), fqdn);
    EXPECT_EQ(cn.type(), nsblast::TYPE_PTR);
    EXPECT_EQ(cn.ttl(), 1000);
    EXPECT_EQ(cn.ptrdname().string(), ptr);
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

TEST(Rr, Afsdb) {
    StorageBuilder sb;

    string_view fqdn = "example.com";
    string_view host = "db.example.com";

    auto rr = sb.createAfsdb(fqdn, 1000, 3, host);

    EXPECT_EQ(rr.labels().string(), fqdn);

    RrAfsdb afsdb{sb.buffer(), rr.offset()};

    EXPECT_EQ(afsdb.labels().string(), fqdn);
    EXPECT_EQ(afsdb.type(), nsblast::TYPE_AFSDB);
    EXPECT_EQ(afsdb.ttl(), 1000);
    EXPECT_EQ(afsdb.host().string(), host);
    EXPECT_EQ(afsdb.subtype(), 3);
}

TEST(Rr, Srv) {
    StorageBuilder sb;

    string_view target = "example.com";
    string_view fqdn = "_test._tcp.example.com";

    auto rr = sb.createSrv(fqdn, 1000, 100, 200, 300, target);

    EXPECT_EQ(rr.labels().string(), fqdn);

    RrSrv srv{sb.buffer(), rr.offset()};

    EXPECT_EQ(srv.labels().string(), fqdn);
    EXPECT_EQ(srv.type(), nsblast::TYPE_SRV);
    EXPECT_EQ(srv.ttl(), 1000);
    EXPECT_EQ(srv.priority(), 100);
    EXPECT_EQ(srv.weight(), 200);
    EXPECT_EQ(srv.port(), 300);
    EXPECT_EQ(srv.target().string(), target);
}

TEST(StorageBuilder, SingleA) {
    StorageBuilder sb;
    string_view fqdn = "example.com";
    auto ip1 = boost::asio::ip::address_v4::from_string("127.0.0.1");

    sb.createA(fqdn, 1000, ip1);
    EXPECT_NO_THROW(sb.finish());

    EXPECT_EQ(sb.rrCount(), 1);
    EXPECT_EQ(sb.header().flags.a, true);
    EXPECT_EQ(sb.header().flags.aaaa, false);
    EXPECT_EQ(sb.header().flags.soa, false);
    EXPECT_EQ(sb.header().flags.ns, false);
    EXPECT_EQ(sb.header().flags.txt, false);
    EXPECT_EQ(sb.header().flags.cname, false);
}

TEST(StorageBuilder, Dhcid) {
    StorageBuilder sb;
    string_view fqdn = "example.com";
    string_view payload{"AAIBY2/AuCccgoJbsaxcQc9TUapptP69lOjxfNuVAA2kjEA="};

    sb.createBase64(fqdn, TYPE_DHCID, 1000, payload);
    EXPECT_NO_THROW(sb.finish());
    EXPECT_EQ(sb.rrCount(), 1);

    Entry entry{sb.buffer()};
    EXPECT_EQ(entry.begin()->type(), TYPE_DHCID);
    EXPECT_EQ(entry.begin()->rdataAsBase64(), payload);
}

TEST(Entry, SingleA) {
    StorageBuilder sb;
    string_view fqdn = "example.com";
    auto ip1 = boost::asio::ip::address_v4::from_string("127.0.0.1");

    sb.createA(fqdn, 1000, ip1);
    sb.finish();

    optional<Entry> e;
    EXPECT_NO_THROW(e.emplace(sb.buffer()));

    EXPECT_EQ(e->count(), 1);
    EXPECT_EQ(e->header().flags.a, true);
    EXPECT_EQ(e->header().flags.aaaa, false);
    EXPECT_EQ(e->header().flags.soa, false);
    EXPECT_EQ(e->header().flags.ns, false);
    EXPECT_EQ(e->header().flags.txt, false);
    EXPECT_EQ(e->header().flags.cname, false);

    auto it = e->begin();
    EXPECT_NE(it, e->end());
    if (it != e->end()) {
        EXPECT_EQ(it->type(), nsblast::TYPE_A);
        EXPECT_EQ(it->ttl(), 1000);
        EXPECT_EQ(it->labels().string(), fqdn);
        auto bytes = ip1.to_bytes();
        EXPECT_TRUE(memcmp(bytes.data(), it->rdata().data(), 4) == 0);
    }

    ++it;
    EXPECT_EQ(it, e->end());
}

TEST(Entry, DoubleA) {
    StorageBuilder sb;
    string_view fqdn = "example.com";
    auto ip1 = boost::asio::ip::address_v4::from_string("127.0.0.1");
    auto ip2 = boost::asio::ip::address_v4::from_string("127.0.0.2");

    sb.createA(fqdn, 1000, ip1);
    sb.createA(fqdn, 1000, ip2);
    sb.finish();

    optional<Entry> e;
    EXPECT_NO_THROW(e.emplace(sb.buffer()));

    EXPECT_EQ(e->count(), 2);
    EXPECT_EQ(e->header().flags.a, true);
    EXPECT_EQ(e->header().flags.aaaa, false);
    EXPECT_EQ(e->header().flags.soa, false);
    EXPECT_EQ(e->header().flags.ns, false);
    EXPECT_EQ(e->header().flags.txt, false);
    EXPECT_EQ(e->header().flags.cname, false);

    auto it = e->begin();
    EXPECT_NE(it, e->end());
    if (it != e->end()) {
        EXPECT_EQ(it->type(), nsblast::TYPE_A);
        EXPECT_EQ(it->ttl(), 1000);
        EXPECT_EQ(it->labels().string(), fqdn);
        auto bytes = ip1.to_bytes();
        EXPECT_TRUE(memcmp(bytes.data(), it->rdata().data(), 4) == 0);
    }

    ++it;
    EXPECT_NE(it, e->end());
    if (it != e->end()) {
        EXPECT_EQ(it->type(), nsblast::TYPE_A);
        EXPECT_EQ(it->ttl(), 1000);
        EXPECT_EQ(it->labels().string(), fqdn);
        auto bytes = ip2.to_bytes();
        EXPECT_TRUE(memcmp(bytes.data(), it->rdata().data(), 4) == 0);
    }

    ++it;
    EXPECT_EQ(it, e->end());
}

TEST(Entry, TripleA) {
    StorageBuilder sb;
    string_view fqdn = "example.com";
    auto ip1 = boost::asio::ip::address_v4::from_string("127.0.0.1");
    auto ip2 = boost::asio::ip::address_v4::from_string("127.0.0.2");
    auto ip3 = boost::asio::ip::address_v4::from_string("127.0.0.2");

    sb.createA(fqdn, 1000, ip1);
    sb.createA(fqdn, 1000, ip2);
    sb.createA(fqdn, 1000, ip3);
    sb.finish();

    optional<Entry> e;
    EXPECT_NO_THROW(e.emplace(sb.buffer()));

    EXPECT_EQ(e->count(), 3);
    EXPECT_EQ(e->header().flags.a, true);
    EXPECT_EQ(e->header().flags.aaaa, false);
    EXPECT_EQ(e->header().flags.soa, false);
    EXPECT_EQ(e->header().flags.ns, false);
    EXPECT_EQ(e->header().flags.txt, false);
    EXPECT_EQ(e->header().flags.cname, false);

    auto it = e->begin();
    EXPECT_NE(it, e->end());
    if (it != e->end()) {
        EXPECT_EQ(it->type(), nsblast::TYPE_A);
        EXPECT_EQ(it->ttl(), 1000);
        EXPECT_EQ(it->labels().string(), fqdn);
        auto bytes = ip1.to_bytes();
        EXPECT_TRUE(memcmp(bytes.data(), it->rdata().data(), 4) == 0);
    }

    ++it;
    EXPECT_NE(it, e->end());
    if (it != e->end()) {
        EXPECT_EQ(it->type(), nsblast::TYPE_A);
        EXPECT_EQ(it->ttl(), 1000);
        EXPECT_EQ(it->labels().string(), fqdn);
        auto bytes = ip2.to_bytes();
        EXPECT_TRUE(memcmp(bytes.data(), it->rdata().data(), 4) == 0);
    }

    ++it;
    EXPECT_NE(it, e->end());
    if (it != e->end()) {
        EXPECT_EQ(it->type(), nsblast::TYPE_A);
        EXPECT_EQ(it->ttl(), 1000);
        EXPECT_EQ(it->labels().string(), fqdn);
        auto bytes = ip3.to_bytes();
        EXPECT_TRUE(memcmp(bytes.data(), it->rdata().data(), 4) == 0);
    }

    ++it;
    EXPECT_EQ(it, e->end());
}

TEST(Entry, AandAAAASorting) {
    StorageBuilder sb;
    string_view fqdn = "example.com";
    auto ip1 = boost::asio::ip::address_v4::from_string("127.0.0.1");
    auto ip2 = boost::asio::ip::address_v4::from_string("127.0.0.2");
    auto ip3 = boost::asio::ip::address_v6::from_string("2001:0db8:85a3:0000:0000:8a2e:0370:7334");
    auto ip4 = boost::asio::ip::address_v6::from_string("2000:0db8:85a3:0000:0000:8a2e:0370:7335");

    // Notice order. Sorting the index must work to iterate in the expected order below
    sb.createA(fqdn, 1000, ip1);
    sb.createA(fqdn, 1000, ip3);
    sb.createA(fqdn, 1000, ip2);
    sb.createA(fqdn, 1000, ip4);
    sb.finish();

    optional<Entry> e;
    EXPECT_NO_THROW(e.emplace(sb.buffer()));

    EXPECT_EQ(e->count(), 4);
    EXPECT_EQ(e->header().flags.a, true);
    EXPECT_EQ(e->header().flags.aaaa, true);
    EXPECT_EQ(e->header().flags.soa, false);
    EXPECT_EQ(e->header().flags.ns, false);
    EXPECT_EQ(e->header().flags.txt, false);
    EXPECT_EQ(e->header().flags.cname, false);

    auto it = e->begin();
    EXPECT_NE(it, e->end());
    if (it != e->end()) {
        EXPECT_EQ(it->type(), nsblast::TYPE_A);
        EXPECT_EQ(it->ttl(), 1000);
        EXPECT_EQ(it->labels().string(), fqdn);
        auto bytes = ip1.to_bytes();
        EXPECT_TRUE(memcmp(bytes.data(), it->rdata().data(), 4) == 0);
    }

    ++it;
    EXPECT_NE(it, e->end());
    if (it != e->end()) {
        EXPECT_EQ(it->type(), nsblast::TYPE_A);
        EXPECT_EQ(it->ttl(), 1000);
        EXPECT_EQ(it->labels().string(), fqdn);
        auto bytes = ip2.to_bytes();
        EXPECT_TRUE(memcmp(bytes.data(), it->rdata().data(), 4) == 0);
    }

    ++it;
    EXPECT_NE(it, e->end());
    if (it != e->end()) {
        EXPECT_EQ(it->type(), nsblast::TYPE_AAAA);
        EXPECT_EQ(it->ttl(), 1000);
        EXPECT_EQ(it->labels().string(), fqdn);
        auto bytes = ip3.to_bytes();
        EXPECT_TRUE(memcmp(bytes.data(), it->rdata().data(), 16) == 0);
    }

    ++it;
    EXPECT_NE(it, e->end());
    if (it != e->end()) {
        EXPECT_EQ(it->type(), nsblast::TYPE_AAAA);
        EXPECT_EQ(it->ttl(), 1000);
        EXPECT_EQ(it->labels().string(), fqdn);
        auto bytes = ip4.to_bytes();
        EXPECT_TRUE(memcmp(bytes.data(), it->rdata().data(), 16) == 0);
    }

    ++it;
    EXPECT_EQ(it, e->end());
}

TEST(Entry, Mx) {
    StorageBuilder sb;
    string_view fqdn = "example.com";
    string_view mxname = "mail.example.com";

    sb.createMx(fqdn, 9999, 10, mxname);
    sb.finish();

    Entry e{sb.buffer()};
    auto it = e.begin();
    RrMx mx{sb.buffer(), it->offset()};
    EXPECT_EQ(mx.labels().string(), fqdn);
    EXPECT_EQ(mx.host().string(), mxname);
    EXPECT_EQ(mx.priority(), 10);
    EXPECT_EQ(mx.ttl(), 9999);
}

TEST(Entry, Ns) {
    StorageBuilder sb;
    string_view fqdn = "example.com";
    string_view nsname = "ns1.example.com";

    sb.createNs(fqdn, 9999, nsname);
    sb.finish();

    Entry e{sb.buffer()};
    auto it = e.begin();
    RrNs ns{sb.buffer(), it->offset()};
    EXPECT_EQ(ns.labels().string(), fqdn);
    EXPECT_EQ(ns.ns().string(), nsname);
    EXPECT_EQ(ns.ttl(), 9999);
}

TEST(Entry, Zone) {
    StorageBuilder sb;
    string_view fqdn = "example.com";
    string_view nsname = "ns1.example.com";
    string_view rname = "hostmaster@example.com";
    string_view mxname = "mail.example.com";
    auto ip1 = boost::asio::ip::address_v4::from_string("127.0.0.1");
    auto ip2 = boost::asio::ip::address_v4::from_string("127.0.0.2");
    auto ip3 = boost::asio::ip::address_v6::from_string("2001:0db8:85a3:0000:0000:8a2e:0370:7334");
    auto ip4 = boost::asio::ip::address_v6::from_string("2000:0db8:85a3:0000:0000:8a2e:0370:7335");

    // Notice order. Sorting the index must work to iterate in the expected order below
    sb.createA(fqdn, 1000, ip1);
    sb.createA(fqdn, 1000, ip3);
    sb.createA(fqdn, 1000, ip2);
    sb.createA(fqdn, 1000, ip4);
    sb.createNs(fqdn, 1000, "ns1.example.com");
    sb.createNs(fqdn, 1000, "ns2.example.com");
    sb.createNs(fqdn, 1000, "ns3.example.com");
    sb.createNs(fqdn, 1000, "ns4.example.com");
    sb.createSoa(fqdn, 5003, nsname, rname, 1000, 1001, 1002, 1003, 1004);
    sb.createMx(fqdn, 9999, 10, mxname);
    sb.finish();

    Entry e{sb.buffer()};

    EXPECT_EQ(e.header().flags.a, true);
    EXPECT_EQ(e.header().flags.aaaa, true);
    EXPECT_EQ(e.header().flags.soa, true);
    EXPECT_EQ(e.header().flags.ns, true);
    EXPECT_EQ(e.header().flags.txt, false);
    EXPECT_EQ(e.header().flags.cname, false);

    bool visited_ns = false;
    auto it = e.begin();
    for(auto type : {nsblast::TYPE_SOA,
        nsblast::TYPE_NS, nsblast::TYPE_NS, nsblast::TYPE_NS, nsblast::TYPE_NS,
        nsblast::TYPE_A, nsblast::TYPE_A, nsblast::TYPE_AAAA, nsblast::TYPE_AAAA,
        nsblast::TYPE_MX}) {

        EXPECT_NE(it, e.end());
        if (it != e.end()) {
            EXPECT_EQ(it->type(), type);
            EXPECT_EQ(it->labels().string(), fqdn);
        }

        switch(type) {
            case nsblast::TYPE_NS:
            if (!visited_ns) {
                visited_ns = true;

                auto n = it;
                for(auto i = 1; i <= 4; ++i) {
                    auto name = "ns"s + to_string(i) + ".example.com";
                    RrNs ns{sb.buffer(), n->offset()};
                    EXPECT_EQ(ns.ns().string(), name);
                    ++n;
                    EXPECT_NE(n, e.end());
                }
            }
            break;
            case nsblast::TYPE_MX:
            {
                RrMx mx{sb.buffer(), it->offset()};
                EXPECT_EQ(mx.host().string(), mxname);
                EXPECT_EQ(mx.priority(), 10);
            }
            break;
            case nsblast::TYPE_SOA:
            {
                RrSoa soa{sb.buffer(), it->offset()};
                EXPECT_EQ(soa.mname().string(), nsname);
                EXPECT_EQ(soa.rname().string(), rname);
            }
            break;
            default:
                ;
        }
        ++it;
    }
}

TEST(StorageBuilder, incrementSoaVersionOk) {

    string_view fqdn = "example.com";
    string_view nsname = "ns1.example.com";
    string_view rname = "hostmaster@example.com";

    StorageBuilder sb;
    // Notice order. Sorting the index must work to iterate in the expected order below
    sb.createNs(fqdn, 1000, "ns1.example.com");
    sb.createNs(fqdn, 1000, "ns2.example.com");
    sb.createSoa(fqdn, 5003, nsname, rname, 1000, 1001, 1002, 1003, 1004);
    sb.finish();

    Entry entry{sb.buffer()};

    EXPECT_EQ(entry.begin()->type(), nsblast::TYPE_SOA);

    {
        RrSoa soa{sb.buffer(), entry.begin()->offset()};
        EXPECT_EQ(soa.serial(), 1000);
    }

    sb.incrementSoaVersion(entry);

    {
        RrSoa soa{sb.buffer(), entry.begin()->offset()};
        EXPECT_EQ(soa.serial(), 1001);
    }

    sb.incrementSoaVersion(entry);

    {
        RrSoa soa{sb.buffer(), entry.begin()->offset()};
        EXPECT_EQ(soa.serial(), 1002);
    }
}

TEST(StorageBuilder, incrementSoaVersionNoSoa) {

    string_view fqdn = "example.com";

    StorageBuilder sb;
    // Notice order. Sorting the index must work to iterate in the expected order below
    sb.createNs(fqdn, 1000, "ns1.example.com");
    sb.createNs(fqdn, 1000, "ns2.example.com");
    sb.finish();

    Entry entry{sb.buffer()};

    EXPECT_NE(entry.begin()->type(), nsblast::TYPE_SOA);

    EXPECT_THROW(sb.incrementSoaVersion(entry), runtime_error);
}

TEST(Message, empty) {
     Message m;
     EXPECT_TRUE(m.empty());
}

TEST(MessageBuilder, Empty) {
     MessageBuilder mb;
     EXPECT_TRUE(mb.empty());
}

TEST(Message, singleQueryOk) {

    // UDP package from dig captured by wireshark
    const char raw[] = "\xd6\x01\x01\x20\x00\x01\x00\x00\x00\x00\x00\x01\x03\x77\x77\x77" \
"\x07\x65\x78\x61\x6d\x70\x6c\x65\x03\x63\x6f\x6d\x00\x00\x01\x00" \
"\x01\x00\x00\x29\x10\x00\x00\x00\x00\x00\x00\x0c\x00\x0a\x00\x08" \
"\x91\x64\xec\x6d\x5e\xc9\x0e\x4e";

    const string_view fqdn = "www.example.com";

    Message msg{raw};

    EXPECT_EQ(msg.header().qdcount(), 1);
    EXPECT_EQ(msg.header().ancount(), 0);
    EXPECT_EQ(msg.header().nscount(), 0);
    EXPECT_EQ(msg.header().arcount(), 1);
    EXPECT_EQ(msg.header().qr(), 0);

    auto rrset = msg.getQuestions();
    EXPECT_EQ(rrset.count(), 1);
    EXPECT_EQ(rrset.begin()->type(), nsblast::TYPE_A);
    EXPECT_EQ(rrset.begin()->labels().string(), fqdn);


}

// TODO: Add more tests with pointers

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
