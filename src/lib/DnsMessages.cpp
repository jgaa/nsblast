
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

constexpr int MAX_PTRS_IN_A_ROW = 16;
constexpr auto START_OF_POINTER_TAG = 0xC0; // Binary: 11000000
constexpr char CURRENT_STORAGE_VERSION = 1;

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

template <typename T, typename I>
void setValueAt(const T& b, size_t loc, I value) {
    if (loc + (sizeof(I) -1) >= b.size()) {
        throw runtime_error{"getValueAt: Cannot get value outside range of buffer!"};
    }

    auto *v = reinterpret_cast<I *>(const_cast<char *>(b.data() + loc));

    auto constexpr ilen = sizeof(I);

    if constexpr (ilen == 1) {
        *v = value;
    } else if constexpr (ilen == 2) {
        *v = htons(value);
    } else if constexpr (ilen == 4) {
        *v = htonl(value);
    } else {
        static_assert (ilen <= 0 || ilen == 3 || ilen > 4, "Unexpected integer length");
    }
}


template <typename T>
void set16bValueAt(const T& b, size_t loc, uint16_t value) {
    return setValueAt(b, loc, value);
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

template <typename T>
uint16_t resolvePtr(const T& buffer, uint16_t offset) {
    // Loose the 2 bits signaling the pointer and get a uint16 pointer to the pointer value
    auto *ch = buffer.data() + offset;
    array<char, 2> ptr_buf;
    ptr_buf[0] = *ch & ~START_OF_POINTER_TAG;
    ptr_buf[1] = *++ch;
    const auto *v = reinterpret_cast<const uint16_t *>(ptr_buf.data());
    // Convert to host representation

    const auto ptr = ntohs(*v);
    return ptr;
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

// Write a fdqn in text representation as a RFC1035 name (array of labels)
template <typename T>
uint16_t writeName(T& buffer, uint16_t offset, string_view fdqn) {
    const auto start_offset = offset;
    assert((offset + fdqn.size() + 2) < buffer.size());

    if (fdqn.size() > 255) {
        throw runtime_error{"writeName: fdqn must be less than 256 bytes. This fdqn is: "s
                            + to_string(fdqn.size())};
    }

    for(auto dot = fdqn.find('.'); !fdqn.empty(); dot = fdqn.find('.')) {
        const auto label = (dot != string_view::npos) ? fdqn.substr(0, dot) : fdqn;

        // The root should not appear here as a dot
        assert(label != ".");

        if (label.size() > 63) {
            throw runtime_error{"writeName: labels must be less 64 bytes. This label: "s
                                + to_string(label.size())};
        }
        buffer[offset] = static_cast<uint8_t>(label.size());
        ++offset;
        std::copy(label.begin(), label.end(), buffer.begin() + offset);
        offset += label.size();

        if (dot == string_view::npos) {
            break;
        }

        // Strip the label off `fdqn` so we can go hunting for the next label!
        fdqn = fdqn.substr(label.size());
        if (!fdqn.empty()) {
            assert(fdqn.front() == '.');
            fdqn = fdqn.substr(1);
        }
    }

    // Always add root
    buffer[offset] = 0;
    return ++offset - start_offset;
}

template <typename T>
void writeNamePtr(T& buffer, uint16_t offset, uint16_t namePtr) {
    auto *w = reinterpret_cast<uint16_t *>(buffer.data() + offset);
    *w = htons(namePtr);
    buffer[offset] |= START_OF_POINTER_TAG;
}

StorageBuilder::NewRr
StorageBuilder::createRr(string_view fqdn, uint16_t type, uint32_t ttl, boost::span<const char> rdata)
{
    if (name_ptr_) {
        // A list of rr's (RRSet) always contain the same fqdn,
        // so if we already have the name, we re-use it.
        return createRr(name_ptr_, type, ttl, rdata);
    }

    const auto start_offset = buffer_.size();

    if (fqdn.empty()) {
        throw runtime_error{"createRr: fqdn is empty"};
    }

    if (fqdn.back() == '.') {
        fqdn = fqdn.substr(0, fqdn.size() -1);
    }

    auto labels_len = fqdn.size() + 2;

    const auto len = calculateLen(labels_len, rdata.size());
    buffer_.resize(buffer_.size() + len);

    {
        const auto rlen = writeName(buffer_, start_offset, fqdn);\
        assert(name_ptr_ == 0);
        assert(start_offset != 0);
        name_ptr_ = start_offset;
        assert(rlen == labels_len);
    }

    return finishRr(start_offset, labels_len, ttl, type, rdata);
}

StorageBuilder::NewRr
StorageBuilder::createRr(uint16_t nameOffset, uint16_t type,
                         uint32_t ttl, boost::span<const char> rdata)
{
    const auto start_offset = buffer_.size();
    auto labels_len =  2;

    const auto len = calculateLen(labels_len, rdata.size());

    buffer_.resize(buffer_.size() + len);
    writeNamePtr(buffer_, start_offset, nameOffset);

    return finishRr(start_offset, labels_len, ttl, type, rdata);
}

StorageBuilder::NewRr
StorageBuilder::finishRr(uint16_t startOffset, uint16_t labelLen, uint16_t type,
                         uint32_t ttl, boost::span<const char> rdata)
{
    const auto len = calculateLen(labelLen, rdata.size());
    size_t coffset = startOffset + labelLen;

    set16bValueAt(buffer_, coffset, type); // Type
    coffset += 2;

    set16bValueAt(buffer_, coffset, Message::CLASS_IN); // Class
    coffset += 2;

    setValueAt(buffer_, coffset, ttl); // TTL
    coffset += 4;

    set16bValueAt(buffer_, coffset, rdata.size()); // Class
    coffset += 2;

    assert(buffer_.size() >= coffset + rdata.size());
    std::copy(rdata.begin(), rdata.end(), buffer_.begin() + coffset);

    auto used_bytes = (buffer_.data() + coffset + rdata.size()) - (buffer_.data() + startOffset);
    assert(static_cast<ptrdiff_t>(len) == used_bytes);

    ++num_rr_;

    return {buffer_,
                static_cast<uint16_t>(startOffset),
                static_cast<uint16_t>(coffset - startOffset),
                static_cast<uint16_t>(len)};
}

size_t StorageBuilder::calculateLen(uint16_t labelsLen, size_t rdataLen) const
{
    return labelsLen // labels
            + 2 // type
            + 2 // class
            + 4 // ttl
            + 2 // rdlenght
            + rdataLen // rdata
            ;
}

void StorageBuilder::prepare()
{
    // Create the header
    buffer_.reserve(1024); // TODO: Get some stats to find a reasonamble value here

    buffer_.resize(6); // Header
    buffer_[0] = CURRENT_STORAGE_VERSION;
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

const Message::buffer_t &Message::buffer() const
{
    return buffer_;
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

string Labels::string(bool showRoot) const
{
    std::string v;
    v.reserve(size());

    for(const auto label : *this) {
        if (!label.empty()) { // root is empty, no need to add it
            if (!v.empty()) { // Only add dot if we already have at least one label in the buffer
                v += '.';
            }
            v += label;
        }
    }

    if (showRoot) { // Trailing dot
        v += '.';
    }

    return v;
}

// This is a critical method as it fiddles with pointers into
// buffers for data received from the internet. It's the
// first place a hacker will look for exploits.
void Labels::parse(boost::span<const char> buffer, size_t startOffset)
{
    std::vector<uint16_t> jumped_to;

    if (startOffset >= buffer.size()) {
        throw runtime_error("Labels::parse: startOffset needs to be smaller than the buffers size");
    }

    offset_ = startOffset;

    //size_t octets = 0;
    size_t label_bytes = 0;
    auto num_ptrs_in_sequence = 0;
    bool in_header = true;
    for(auto it = buffer.begin() + startOffset; it != buffer.end(); ++it) {
        const auto ch = static_cast<uint8_t>(*it);
        if (in_header) {
            ++count_;

            const auto offset = static_cast<size_t>(distance(buffer.begin(), it));
            if (offset >= numeric_limits<uint16_t>::max()) {
                throw runtime_error("Labels::parse: Too long distance between labels in the buffer. Must be addressable with 16 bits.");
            }

            // root?
            if (ch == 0) {
                buffer_view_ = buffer;
                ++size_;
                return; // At this point we know that the labels are within the
                        // limits for their individual and total size, and that
                        // they are withinn the boundries for the buffer.
            }

            // Is it a pointer to the start of another label?
            if ((ch & START_OF_POINTER_TAG) != START_OF_POINTER_TAG) {
                num_ptrs_in_sequence = 0;
            } else {
                if (++num_ptrs_in_sequence >= MAX_PTRS_IN_A_ROW) {
                    throw runtime_error{"Labels::parse: Too many pointers in a row"};
                }
                if ((it + 1) == buffer.end()) {
                    throw runtime_error("Labels::parse: Found a label pointer starting at the last byte of the buffer");
                }

                auto ptr = resolvePtr(buffer, offset);
                if (ptr >= buffer.size() || ptr < 0) {
                    throw runtime_error("Labels::parse: Pointer tried to escape buffer");
                }

                // Don't allow jumping to the same pointer again
                if (find(jumped_to.begin(), jumped_to.end(), static_cast<uint16_t>(ptr)) != jumped_to.end()) {
                    throw runtime_error("Labels::parse: Found a recursive pointer.");
                }
                jumped_to.push_back(static_cast<uint16_t>(ptr));

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

Labels::Iterator::Iterator(boost::span<const char> buffer, uint16_t offset)
    : buffer_{buffer}, current_loc_{offset} {
    followPointers();
    update();
}

Labels::Iterator::Iterator(const Labels::Iterator &it)
    : buffer_{it.buffer_}, current_loc_{it.current_loc_}
{
    followPointers();
    update();
}

Labels::Iterator &Labels::Iterator::operator =(const Labels::Iterator &it) {
    buffer_ = it.buffer_;
    current_loc_ = it.current_loc_;
    followPointers();
    update();
    return *this;
}

Labels::Iterator &Labels::Iterator::operator++() {
    increment();
    return *this;
}

bool Labels::Iterator::equals(const boost::span<const char> a, const boost::span<const char> b) {
    return a.data() == b.data() && a.size() == b.size();
}

Labels::Iterator Labels::Iterator::operator++(int) {
    auto tmp = *this;
    increment();
    return tmp;
}

void Labels::Iterator::update() {
    if (!buffer_.empty()) {
        const auto *b =  buffer_.data() + current_loc_ + 1;
        const auto len = static_cast<size_t>(buffer_[current_loc_]);

        csw_ = {b, len};

        if (csw_.size() == 0) {
            // Root node. Don't point to anything
            csw_ = {};
        } else {
            assert((csw_.data() + csw_.size()) < (buffer_.data() + buffer_.size()));
            assert(csw_.data() > buffer_.data());
        }
    }
}

void Labels::Iterator::increment() {
    if (!csw_.empty()) {
        current_loc_ += csw_.size() + 1;
        followPointers();
        update();
    } else {
        // Morph into an end() iterator
        current_loc_ = {};
        buffer_ = {};
        csw_ = {};
    }
}

void Labels::Iterator::followPointers()
{
    // Is it a pointer?
    for(auto i = 0; ((buffer_[current_loc_] & START_OF_POINTER_TAG) == START_OF_POINTER_TAG); ++i) {
        if (i >= MAX_PTRS_IN_A_ROW) {
            throw runtime_error{"Labels::Iterator::increment: Recursive pointer or too many jumps!"};
        }

        const auto ptr = resolvePtr(buffer_, current_loc_);

        // The parsing validated the pointers. But to avoid coding errors...
        assert(ptr >= 0);
        assert(ptr < buffer_.size());

        current_loc_ = ptr;
    }
}


} // ns
