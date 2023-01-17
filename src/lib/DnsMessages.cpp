
#include <cassert>
#include <stdexcept>
#include <string>
#include <boost/asio.hpp>

#include "nsblast/DnsMessages.h"
#include "nsblast/logging.h"

using namespace std;
using namespace std::string_literals;

namespace nsblast::lib {

namespace {

#pragma pack(1)
struct hdrbits {
    uint16_t qr : 1;
    uint16_t opcode : 4;
    uint16_t aa : 1;
    uint16_t tc : 1;
    uint16_t rd : 1;
    uint16_t ra : 1;
    uint16_t z : 3;
    uint16_t rcode : 4;
};
#pragma pack(0)

auto get16bValueAt(const std::string_view& b, size_t loc) {
    if (loc + 1 >= b.size()) {
        throw runtime_error{"getValueAt: Cannot get value outside range of buffer!"};
    }

    const auto *v = reinterpret_cast<const uint16_t *>(b.data() + loc);
    return ntohs(*v);
}

auto set16bValueAt(const std::string_view& b, size_t loc, uint16_t value) {
    if (loc + 1 >= b.size()) {
        throw runtime_error{"getValueAt: Cannot get value outside range of buffer!"};
    }

    auto *v = reinterpret_cast<uint16_t *>(const_cast<char *>(b.data() + loc));
    *v = htons(value);
}

void inc16BitValueAt(string_view b, size_t loc) {
    auto val = get16bValueAt(b, loc);
    ++val;
    set16bValueAt(b, loc, val);
}

auto getHdrFlags(const Message::buffer_t& b) {
    if (b.size() < Message::Header::SIZE) {
        throw runtime_error{"getValueAt: Cannot get value outside range of buffer!"};
    }
    const auto bits = reinterpret_cast<const hdrbits *>(b.data() + 2);
    return *bits;
}

void setHdrFlags(Message::buffer_t& b, hdrbits newBits) {
    if (b.size() < Message::Header::SIZE) {
        throw runtime_error{"getValueAt: Cannot set value outside range of buffer!"};
    }
    auto bits = reinterpret_cast<hdrbits *>(b.data() + 2);
    *bits = newBits;
}


} // anon ns

MessageBuilder::NewHeader
MessageBuilder::createHeader(uint16_t id, bool qr, Message::Header::OPCODE opcode, bool rd)
{
    assert(buffer_.empty());

    buffer_.resize(Header::SIZE); // Sets all bytes to 0

    auto *v = reinterpret_cast<uint16_t *>(buffer_.data());
    *v = htons(id);
    ++v; // Now points to the flags section at offset 2

    uint16_t bits = {};

    // We use hdrbits to address small unsigned integers and bits inside a 2 octets memory location.
    assert(sizeof(hdrbits) == 2);
    assert(sizeof(hdrbits) == sizeof(bits));

    auto opcodeValue = static_cast<uint8_t>(opcode);
    if (opcodeValue >= static_cast<char>(Header::OPCODE::RESERVED_)) {
        throw runtime_error{"createHeader: Invalid opcode "s + to_string(opcodeValue)};
    }

    auto *b = reinterpret_cast<hdrbits *>(&bits);
    b->qr = qr;
    b->rd = rd;
    b->opcode = static_cast<uint8_t>(opcode);

    *v = bits;

    return NewHeader{buffer_};
}

uint16_t Message::Header::id() const
{
    return get16bValueAt({buffer_.data(), buffer_.size()}, 0);
}

bool Message::Header::qr() const
{
    return getHdrFlags(buffer_).qr;
}

Message::Header::OPCODE Message::Header::opcode() const
{
    return static_cast<OPCODE>(getHdrFlags(buffer_).opcode);
}

bool Message::Header::aa() const
{
    return getHdrFlags(buffer_).aa;
}

bool Message::Header::tc() const
{
    return getHdrFlags(buffer_).tc;
}

bool Message::Header::rd() const
{
    return getHdrFlags(buffer_).rd;
}

bool Message::Header::ra() const
{
    return getHdrFlags(buffer_).ra;
}

bool Message::Header::z() const
{
    return getHdrFlags(buffer_).z;
}

Message::Header::RCODE Message::Header::rcode() const
{
    return static_cast<RCODE>(getHdrFlags(buffer_).rcode);
}

uint16_t Message::Header::qdcount() const
{
    return get16bValueAt({buffer_.data(), buffer_.size()}, 4);
}

uint16_t Message::Header::ancount() const
{
    return get16bValueAt({buffer_.data(), buffer_.size()}, 6);
}

uint16_t Message::Header::nscount() const
{
    return get16bValueAt({buffer_.data(), buffer_.size()}, 8);
}

uint16_t Message::Header::arcount() const
{
    return get16bValueAt({buffer_.data(), buffer_.size()}, 10);
}

void MessageBuilder::NewHeader::incQdcount()
{
    inc16BitValueAt({mutable_buffer_.data(), mutable_buffer_.size()}, 4);
}

void MessageBuilder::NewHeader::incAncount()
{
    inc16BitValueAt({mutable_buffer_.data(), mutable_buffer_.size()}, 6);
}

void MessageBuilder::NewHeader::incNscount()
{
    inc16BitValueAt({mutable_buffer_.data(), mutable_buffer_.size()}, 8);
}

void MessageBuilder::NewHeader::incArcount()
{
    inc16BitValueAt({mutable_buffer_.data(), mutable_buffer_.size()}, 10);
}

void MessageBuilder::NewHeader::setTc(bool flag)
{
    auto bits = getHdrFlags(mutable_buffer_);
    bits.tc = flag;
    setHdrFlags(mutable_buffer_, bits);
}

void MessageBuilder::NewHeader::setRa(bool flag)
{
    auto bits = getHdrFlags(mutable_buffer_);
    bits.ra = flag;
    setHdrFlags(mutable_buffer_, bits);
}

void MessageBuilder::NewHeader::setRcode(Message::Header::RCODE rcode)
{
    auto bits = getHdrFlags(mutable_buffer_);

    const auto newRcode = static_cast<uint8_t>(rcode);
    if (newRcode >= static_cast<uint8_t>(Message::Header::RCODE::RESERVED_)) {
        throw runtime_error{"setRcode: Invalid rcode: "s + to_string(newRcode)};
    }

    bits.rcode = newRcode;
    setHdrFlags(mutable_buffer_, bits);
}

const Message::Header Message::header() const
{
    return Header{buffer_};
}


} // ns
