
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
constexpr char BUFFER_HEADER_LEN = 8;

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

template <typename T, typename I>
I getValueAt(const T& b, size_t loc) {
    if (loc + (sizeof(I) -1) >= b.size()) {
        throw runtime_error{"getValueAt: Cannot get value outside range of buffer!"};
    }

    auto *v = reinterpret_cast<const I *>(b.data() + loc);

    auto constexpr ilen = sizeof(I);

    if constexpr (ilen == 1) {
        return *v;
    } else if constexpr (ilen == 2) {
        return ntohs(*v);
    } else if constexpr (ilen == 4) {
        return ntohl(*v);
    } else {
        static_assert (ilen <= 0 || ilen == 3 || ilen > 4, "getValueAt: Unexpected integer length");
    }

    throw runtime_error{"getValueAt: Something is very, very wrong..."};
}

template <typename T>
auto get16bValueAt(const T& b, size_t loc) {
    return getValueAt<T, uint16_t>(b, loc);
}

template <typename T>
auto get32bValueAt(const T& b, size_t loc) {
    return getValueAt<T, uint32_t>(b, loc);
}

template <typename T, typename I>
void setValueAt(const T& b, size_t loc, I value) {
    if (loc + (sizeof(I) -1) >= b.size()) {
        throw runtime_error{"setValueAt: Cannot set value outside range of buffer!"};
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
        static_assert (ilen <= 0 || ilen == 3 || ilen > 4, "setValueAt: Unexpected integer length");
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
uint16_t writeName(T& buffer, uint16_t offset, string_view fdqn, bool commit = true) {
    const auto start_offset = offset;

    if (commit) {
        assert((offset + fdqn.size() + 2) <= buffer.size());
    }

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
        if (commit) {
            buffer[offset] = static_cast<uint8_t>(label.size());
        }
        ++offset;
        if (commit) {
            std::copy(label.begin(), label.end(), buffer.begin() + offset);
        }
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
    if (commit) {
        buffer[offset] = 0;
    }
    return ++offset - start_offset;
}

template <typename T>
void writeNamePtr(T& buffer, uint16_t offset, uint16_t namePtr) {
    auto *w = reinterpret_cast<uint16_t *>(buffer.data() + offset);
    *w = htons(namePtr);
    buffer[offset] |= START_OF_POINTER_TAG;
}

StorageBuilder::        NewRr
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
        const auto rlen = writeName(buffer_, start_offset, fqdn);
        assert(name_ptr_ == 0);
        assert(start_offset != 0);
        name_ptr_ = start_offset;
        assert(rlen == labels_len);
    }

    assert(label_len_ == 0);
    label_len_ = labels_len;
    return finishRr(start_offset, labels_len, type, ttl, rdata);
}

StorageBuilder::NewRr StorageBuilder::createSoa(string_view fqdn, uint32_t ttl, string_view mname,
                                                string_view rname, uint32_t serial,
                                                uint32_t refresh, uint32_t retry,
                                                uint32_t expire, uint32_t minimum)
{
    // I could be fancy and create the SOA while creating the Rr, to avoid
    // copying the rdata buffer, but this code is much cleaner - and soa's
    // are not frequently updated (normally)

    vector<char> rdata;

    const auto mname_size = writeName(rdata, 0, mname, false);
    const auto rname_size = writeName(rdata, 0, rname, false);

    rdata.resize(mname_size + rname_size + (4 * 5));

    // TODO: See if we can compress the labels if parts of the names are already present
    // in the builders buffer.
    auto bytes = writeName(rdata, 0, mname);
    assert(bytes == mname_size);

    bytes = writeName(rdata, mname_size, rname);
    assert(bytes == rname_size);

    auto offset = mname_size + rname_size;
    assert(offset == (static_cast<int>(rdata.size()) - (4 * 5)));

    // Write the 32 bit values in the correct order
    for(const auto val : {serial, refresh, retry, expire, minimum}) {
        setValueAt(rdata, offset, val);
        offset += sizeof(uint32_t);
    }

    return createRr(fqdn, TYPE_SOA, ttl, rdata);
}

StorageBuilder::NewRr StorageBuilder::createCname(string_view fqdn, uint32_t ttl, string_view cname)
{
    return createDomainNameInRdata(fqdn, TYPE_CNAME, ttl, cname);
}

StorageBuilder::NewRr StorageBuilder::createNs(string_view fqdn, uint32_t ttl, string_view ns)
{
    return createDomainNameInRdata(fqdn, TYPE_NS, ttl, ns);
}

StorageBuilder::NewRr StorageBuilder::createMx(string_view fqdn, uint32_t ttl, uint16_t priority, string_view host)
{
    vector<char> rdata;

    const auto host_size = writeName(rdata, 0, host, false);
    rdata.resize(host_size + 2);

    setValueAt(rdata, 0, priority);
    writeName(rdata, 2, host);

    return createRr(fqdn, TYPE_MX, ttl, rdata);
}

StorageBuilder::NewRr StorageBuilder::createTxt(string_view fqdn, uint32_t ttl, string_view txt, bool split)
{
    if (txt.size() > TXT_SEGMENT_MAX) {
        if (split) {
            deque<string_view> q;
            while(!txt.empty()) {
                const auto len = min(TXT_SEGMENT_MAX, txt.size());
                q.emplace_back(txt.substr(0, len));
                txt = txt.substr(len);
            }
            assert(!q.empty());
            return createTxtRdata(fqdn, ttl, q);
        }

        throw runtime_error{"Text entry is too long to fit in one segment"};
    }

    array<string_view, 1> a = {txt};
    return createTxtRdata(fqdn, ttl, a);
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

    return finishRr(start_offset, labels_len, type, ttl, rdata);
}

/* Update the header to it's correct binary value.
 * Sort and add the index to the buffer
 */
void StorageBuilder::finish()
{
    assert(BUFFER_HEADER_LEN == sizeof(Header));

    if (buffer_.size() < sizeof(Header)) {
        throw runtime_error{"StorageBuilder::finish: No room in buffer_ for the header."};
    }

    sort(index_.begin(), index_.end(), [](const auto& left, const auto& right) {
        static constexpr array<uint8_t, 30> sorting_table = {
            9, /* a */ 3, /* ns */ 2, 9, 9, /* cname */ 5, /* soa */ 1, 9, 9, 9, // 0
            9, 9, 9, 9, 9, /* mx */ 6, /* txt */ 7, 9, 9, 9,                     // 10
            9, 9, 9, 9, 9, 9, 9, 9, /* aaaa */ 4, 9                              // 20
        };

        assert(left.type < sorting_table.size());
        assert(right.type < sorting_table.size());
        return sorting_table.at(left.type) < sorting_table.at(right.type);
    });

    boost::span index{reinterpret_cast<const char *>(&index_[0]), index_.size() * sizeof(Index)};

    // Convert the index entries to network byte order
    for(auto& e : index_) {
        e.offset = htons(e.offset);
        e.type = htons(e.type);
    }

    // Append the sorted and converted index to the buffer.
    index_offset_ = buffer_.size();
    buffer_.insert(buffer_.end(), index.begin(), index.end());

    // Commit the header
    Header *h = reinterpret_cast<Header *>(buffer_.data());
    *h = {};
    h->flags = flags_;
    h->rrcount = htons(static_cast<uint16_t>(index_.size()));
    h->labelsize = label_len_;
    h->ixoffset = htons(index_offset_);

    assert(h->version == CURRENT_STORAGE_VERSION);
}

StorageBuilder::Header StorageBuilder::header() const
{
    if (buffer_.size() < sizeof(Header)) {
        throw runtime_error{"StorageBuilder::finish: No room in buffer_ for the header."};
    }

    return *reinterpret_cast<const Header *>(buffer_.data());
}

StorageBuilder::NewRr StorageBuilder::createDomainNameInRdata(string_view fqdn, uint16_t type, uint32_t ttl, string_view dname)
{
    vector<char> rdata;
    const auto mname_size = writeName(rdata, 0, dname, false);
    rdata.resize(mname_size);
    writeName(rdata, 0, dname);
    return createRr(fqdn, type, ttl, rdata);
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

    set16bValueAt(buffer_, coffset, rdata.size()); // RDLEN
    coffset += 2;

    assert(buffer_.size() >= coffset + rdata.size());
    std::copy(rdata.begin(), rdata.end(), buffer_.begin() + coffset);

    auto used_bytes = (buffer_.data() + coffset + rdata.size()) - (buffer_.data() + startOffset);
    assert(static_cast<ptrdiff_t>(len) == used_bytes);

    adding(startOffset, type);

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

    buffer_.resize(BUFFER_HEADER_LEN); // Header
    buffer_[0] = CURRENT_STORAGE_VERSION;
}

void StorageBuilder::adding(uint16_t startOffset, uint16_t type)
{
    index_.push_back({type, startOffset});

    switch(type) {
    case TYPE_SOA:
        flags_.soa = true;
        break;
    case TYPE_NS:
        flags_.ns = true;
        break;
    case TYPE_A:
        flags_.a = true;
        break;
    case TYPE_AAAA:
        flags_.aaaa = true;
        break;
    case TYPE_CNAME:
        flags_.cname = true;
        break;
    case TYPE_TXT:
        flags_.txt = true;
        break;
    default:
        ;
    }
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

RrSet Message::getQuestions() const
{

}

RrSet Message::getAnswers() const
{

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

uint16_t Labels::bytes() const noexcept
{
    return bytes_;
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
    buffer_view_ = buffer;

    bool in_pointer = false;

    //size_t octets = 0;
    size_t label_bytes = 0;
    auto num_ptrs_in_sequence = 0;
    bool in_header = true;
    for(auto it = buffer.begin() + startOffset; it != buffer.end(); ++it) {
        if (!in_pointer) {
            ++bytes_;
        }
        const auto ch = static_cast<uint8_t>(*it);
        if (in_header) {
            ++count_;

            const auto offset = static_cast<size_t>(distance(buffer.begin(), it));
            if (offset >= numeric_limits<uint16_t>::max()) {
                throw runtime_error("Labels::parse: Too long distance between labels in the buffer. Must be addressable with 16 bits.");
            }

            // root?
            if (ch == 0) {
                ++size_;

                return; // At this point we know that the labels are within the
                        // limits for their individual and total size, and that
                        // they are withinn the boundries for the buffer.
            }

            // Is it a pointer to the start of another label?
            if ((ch & START_OF_POINTER_TAG) != START_OF_POINTER_TAG) {
                num_ptrs_in_sequence = 0;
            } else {
                if (!in_pointer) {
                    in_pointer = true;
                    ++bytes_; // This is the end of the buffer occupied by this label
                }

                if (++num_ptrs_in_sequence >= MAX_PTRS_IN_A_ROW) {
                    throw runtime_error{"Labels::parse: Too many pointers in a row"};
                }
                if ((it + 1) == buffer.end()) {
                    throw runtime_error("Labels::parse: Found a label pointer starting at the last byte of the buffer");
                }

                if (buffer_view_.empty()) {
                    buffer_view_ = buffer.subspan(offset_, size_ + 1);
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
    if (!buffer_.empty()) { // end() ?
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
}

uint16_t Rr::type() const
{
    return get16bValueAt(self_view_, offset_to_type_);
}

uint16_t Rr::clas() const
{
    return get16bValueAt(self_view_, offset_to_type_ + 2);
}

uint32_t Rr::ttl() const
{
    return get32bValueAt(self_view_, offset_to_type_ + 4);
}

uint16_t Rr::rdlength() const
{
    return get32bValueAt(self_view_, offset_to_type_ + 8);
}

Rr::buffer_t Rr::rdata() const
{
    const auto start_of_rdata = offset_to_type_ + 10;
    return self_view_.subspan(start_of_rdata);
            //{self_view_.data() + start_of_rdata, self_view_.size() - start_of_rdata};
}

Labels Rr::labels() const
{
    if (!labels_) {
        labels_.emplace(buffer_view_, offset_);
    }

    return *labels_;
}

void Rr::parse()
{
    const auto max_window_size = buffer_view_.size() - offset_;
    if (max_window_size < 2) {
        throw runtime_error{"Rr::parse: Buffer-window < 2 bytes!"};
    }

    size_t labelLen = 2;

    // Only parse the labels at this time if it's not a pointer.
    // If it's a pointer, we know that the buffer size for the label section
    // is 2 bytes.
    if ((buffer_view_[offset_] & START_OF_POINTER_TAG) != START_OF_POINTER_TAG) {
        labels_.emplace(buffer_view_, offset_);
        labelLen = labels_->bytes();
        auto sl = labels_->string().size();
        assert(labels_->bytes() == (sl + 2 /* first len + root */));
    }

    // Get the start of the buffer after the labels (in self_view_)
    offset_to_type_ = labelLen;
    const auto rdlenSizeOffset = offset_ + offset_to_type_ + 2 +  2 + 4;

    if ((rdlenSizeOffset + 2) >= buffer_view_.size()) {
        throw runtime_error{"Rr::parse: Buffer-window is too small to hold rdtata section!"};
    }

    const auto rdlen = get16bValueAt(buffer_view_,  rdlenSizeOffset);
    const auto len = labelLen + (2 + 2 + 4 + 2 + rdlen);

    if (len > max_window_size) {
        throw runtime_error{"Rr::parse: Buffer-window is too small to hold the full RR!"};
    }

    self_view_ =  buffer_view_.subspan(offset_, len);
    assert(self_view_.size() == len);
    assert(self_view_.size() <= max_window_size);
}

RrSet::RrSet(RrSet::buffer_t bufferView, uint16_t offset, uint16_t count)
    : view_{bufferView}, offset_{offset}, count_{count}
{
    parse();
}

RrSet::Iterator RrSet::begin() const
{
    return Iterator{view_, offset_, index_};
}

RrSet::Iterator RrSet::end() const
{
    return Iterator{{}, {}, index_};
}

void RrSet::parse()
{
    uint16_t coffset = offset_;
    for(size_t i = 0; i < count_; ++i) {
        Rr rr{view_, coffset};
        index_.emplace_back(rr.type(), coffset);
        coffset += rr.size();
    }
}

RrSet::Iterator::Iterator(boost::span<const char> buffer, uint16_t offset, const RrSet::index_t &index)
    : index_{index}, buffer_{buffer}
{
    if (buffer.empty()) { // end() iterator
        current_ = index_.end();
        return;
    }

    current_ = index_.begin();
    update();
}

RrSet::Iterator &RrSet::Iterator::operator++()
{
    increment();
    return *this;
}

RrSet::Iterator RrSet::Iterator::operator++(int) {
    auto self = *this;
    increment();
    return self;
}

//RrSet::Iterator::Iterator(const RrSet::Iterator &it)
//    : index_{it.index_}, buffer_{it.buffer_}, current_{it.current_}, crr_{it.crr_}
//{

//}

bool RrSet::Iterator::equals(const boost::span<const char> a, const boost::span<const char> b)
{
    return a.data() == b.data() && a.size() == b.size();
}

void RrSet::Iterator::update()
{
    if (current_ == index_.end()) {
        crr_ = {};
        return;
    }

    crr_ = {buffer_, current_->offset};
}

void RrSet::Iterator::increment()
{
    assert(current_ != index_.end());
    ++current_;
    update();
}

Labels RrSoa::mname()
{
    return {rdata(), 0};
}

Labels RrSoa::rname()
{
    // TODO: Not optimal...
    return {rdata(), mname().bytes()};
}

uint32_t RrSoa::serial() const
{
    const auto rd = rdata();
    assert(rd.size() >= 24);
    return get32bValueAt(rd, rd.size() - 20);
}

uint32_t RrSoa::refresh() const
{
    const auto rd = rdata();
    assert(rd.size() >= 24);
    return get32bValueAt(rd, rd.size() - 16);
}

uint32_t RrSoa::retry() const
{
    const auto rd = rdata();
    assert(rd.size() >= 24);
    return get32bValueAt(rd, rd.size() - 12);
}

uint32_t RrSoa::expire() const
{
    const auto rd = rdata();
    assert(rd.size() >= 24);
    return get32bValueAt(rd, rd.size() - 8);
}

uint32_t RrSoa::minimum() const
{
    const auto rd = rdata();
    assert(rd.size() >= 24);
    return get32bValueAt(rd, rd.size() - 4);
}

Labels RrCname::cname() const
{
    if (type() != TYPE_CNAME) {
        throw runtime_error{"Not a TYPE_CNAME"};
    }
    return {rdata(), 0};
}

Labels RrNs::ns() const
{
    if (type() != TYPE_NS) {
        throw runtime_error{"Not a TYPE_NS"};
    }
    return {rdata(), 0};
}

Labels RrMx::host()
{
    if (type() != TYPE_MX) {
        throw runtime_error{"Not a TYPE_MX"};
    }
    return {rdata(), 2};
}

uint32_t RrMx::priority() const
{
    if (type() != TYPE_MX) {
        throw runtime_error{"Not a TYPE_MX"};
    }

    const auto rd = rdata();
    assert(rd.size() >= 2);
    return get16bValueAt(rd, 0);
}

Entry::Entry(boost::span<const char> buffer)
    : buffer_(buffer.begin(), buffer.end())
    , header_{reinterpret_cast<const Header *>(buffer_.data())}
    , count_{ntohs(header_->rrcount)}
    , index_{mkIndex(buffer_, *header_, count_)}
{
}

Entry::Iterator::Iterator(const Entry &entry, bool begin)
    : entry_{&entry}
    , ix_{begin ? entry.index().begin() : entry.index().end()}
{
    update();
}

Entry::Iterator Entry::Iterator::operator++(int)
{
    auto self = *this;
    increment();
    update();
    return self;
}

Entry::Iterator& Entry::Iterator::operator++()
{
    increment();
    update();
}

void Entry::Iterator::update()
{
    if (ix_ != entry_->index().end()) {
        const auto pos = ntohs(ix_->offset);
        assert(pos < entry_->buffer().size() + 10);
        crr_ = {entry_->buffer(), pos};
        assert(crr_.type() == ntohs(ix_->type));
        return;
    }

    crr_ = {};
}

void Entry::Iterator::increment()
{
   ++ix_;
}


} // ns
