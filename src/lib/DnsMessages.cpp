
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

template <typename T>
auto get16bValueAt(const T& b, size_t loc) {
    if (loc + 1 >= b.size()) {
        throw runtime_error{"getValueAt: Cannot get value outside range of buffer!"};
    }

    const auto *v = reinterpret_cast<const uint16_t *>(b.data() + loc);
    return ntohs(*v);
}

template <typename T>
auto set16bValueAt(const T& b, size_t loc, uint16_t value) {
    if (loc + 1 >= b.size()) {
        throw runtime_error{"getValueAt: Cannot get value outside range of buffer!"};
    }

    auto *v = reinterpret_cast<uint16_t *>(const_cast<char *>(b.data() + loc));
    *v = htons(value);
}

template <typename T>
void inc16BitValueAt(T& b, size_t loc) {
    auto val = get16bValueAt(b, loc);
    ++val;
    set16bValueAt(b, loc, val);
}

template <typename T>
auto getHdrFlags(const T& b) {
    if (b.size() < Message::Header::SIZE) {
        throw runtime_error{"getValueAt: Cannot get value outside range of buffer!"};
    }
    const auto bits = reinterpret_cast<const hdrbits *>(b.data() + 2);
    return *bits;
}

template <typename T>
void setHdrFlags(T& b, hdrbits newBits) {
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
    boost::span b{buffer_.data(), 100};
    return get16bValueAt(boost::span{buffer_.data(), buffer_.size()}, 0);
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
    return get16bValueAt(buffer_, 4);
}

uint16_t Message::Header::ancount() const
{
    return get16bValueAt(buffer_, 6);
}

uint16_t Message::Header::nscount() const
{
    return get16bValueAt(buffer_, 8);
}

uint16_t Message::Header::arcount() const
{
    return get16bValueAt(buffer_, 10);
}

void MessageBuilder::NewHeader::incQdcount()
{
    inc16BitValueAt(mutable_buffer_, 4);
}

void MessageBuilder::NewHeader::incAncount()
{
    inc16BitValueAt(mutable_buffer_, 6);
}

void MessageBuilder::NewHeader::incNscount()
{
    inc16BitValueAt(mutable_buffer_, 8);
}

void MessageBuilder::NewHeader::incArcount()
{
    inc16BitValueAt(mutable_buffer_, 10);
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
    auto b = boost::span{buffer_};

    return Header{buffer_};
}

Labels::Labels(boost::span<const char> buffer, size_t startOffset)
{
    parse(buffer, startOffset);
}

size_t Labels::size() const noexcept
{
    return size_;
}

size_t Labels::count() const noexcept
{
    return count_;
}

// This is a critical method as it fiddles with pointers into
// buffers for data received from the internet. It's the
// first place a hacker will look for exploits.
void Labels::parse(boost::span<const char> buffer, size_t startOffset)
{
    std::vector<uint16_t> seen_labels;

    if (startOffset >= buffer.size()) {
        throw runtime_error("Labels::parse: startOffset needs to be smaller than the buffers size");
    }

    //size_t octets = 0;
    size_t label_bytes = 0;
    bool in_header = true;
    for(auto it = buffer.begin() + startOffset; it != buffer.end(); ++it) {
        const auto ch = static_cast<uint8_t>(*it);
        if (in_header) {
            ++count_;

            const auto offset = static_cast<size_t>(distance(buffer.begin(), it));
            if (offset >= numeric_limits<uint16_t>::max()) {
                throw runtime_error("Labels::parse: Too long distance between labels in the buffer. Must be addressable with 16 bits.");
            }
            seen_labels.push_back(static_cast<uint16_t>(offset));

            // root?
            if (ch == 0) {
                buffer_view_ = buffer.subspan(0, offset +1);
                ++size_;
                return; // At this point we know that the labels are within the
                        // limits for their individual and total size, and that
                        // they are withinn the boundries for the buffer.
            }
            // Is it a pointer to the start of another label?
            if ((ch & 0xC0) == 0xC0) {
                if ((it + 1) == buffer.end()) {
                    throw runtime_error("Labels::parse: Found a label pointer starting at the last byte of the buffer");
                }
                if (find(seen_labels.begin(), seen_labels.end(), static_cast<uint16_t>(offset)) != seen_labels.end()) {
                    throw runtime_error("Labels::parse: Found a recursive pointer.");
                }

                // Loose the 2 bits signaling the pointer
                array<char, 2> ptr_buf;
                ptr_buf[0] = *it & ~0xC0;
                ptr_buf[1] = *++it;
                const auto *v = reinterpret_cast<const uint16_t *>(ptr_buf.data());
                // Convert to host representation
                const auto ptr = ntohs(*v);

                if (ptr >= buffer.size()) {
                    throw runtime_error("Labels::parse: Pointer tried to escape buffer");
                }

                // We will count the label when we land on it.
                --count_;

                // Now, jump to the location in the pointer.
                it = buffer.begin() + (ptr -1);
                assert(in_header);
                continue;
            }
            if (ch > 63) {
                throw runtime_error("Labels::parse: Max label size is 63 bytes: This label is "s + to_string(ch));
            }
            if (offset + ch >= buffer.size()) {
                throw runtime_error("Labels::parse: Labels exeed the containing buffer-size");
            }
            if (offset + ch >= 255) {
                throw runtime_error("Labels::parse: Labels exeed the 255 bytes limit for a fqdn: "s + to_string(offset + ch));
            }
            // OK.
            in_header = false;
            label_bytes = ch;

            // Don't count the first normal label-header, as there is no leading dot in the name
            if (size_) {
                ++size_;
            }
        } else {
            ++size_;
            if (--label_bytes == 0) {
                in_header = true;
            }
        }
    }

    throw runtime_error("Labels::parse: Labels are not valid");
}


} // ns
