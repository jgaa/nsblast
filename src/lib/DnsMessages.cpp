
#include <cassert>
#include <stdexcept>
#include <string>
#include <boost/asio.hpp>
#include <algorithm>

#include "nsblast/DnsMessages.h"
#include "nsblast/detail/write_labels.hpp"
#include "nsblast/logging.h"
#include "nsblast/util.h"

using namespace std;
using namespace std::string_literals;

namespace nsblast::lib {

using namespace detail;

namespace {

#pragma pack(1)
struct hdrbits {
    // First byte
    uint8_t rd : 1;
    uint8_t tc : 1;
    uint8_t aa : 1;
    uint8_t opcode : 4;
    uint8_t qr : 1;

    // Second byte
    uint8_t rcode : 4;
    uint8_t z : 1;
    uint8_t ad : 1;
    uint8_t nd : 1; // Non-authenticated data
    uint8_t ra : 1;
};
#pragma pack(0)



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
        throw runtime_error{"getHdrFlags: Cannot get value outside range of buffer!"};
    }
    const auto hdrptr = b.data() + 2;
    const auto bits = reinterpret_cast<const hdrbits *>(hdrptr);
    return *bits;
}

template <typename T>
void setHdrFlags(T& b, hdrbits newBits) {
    if (b.size() < Message::Header::SIZE) {
        throw runtime_error{"setHdrFlags: Cannot set value outside range of buffer!"};
    }
    auto hdrptr = b.data() + 2;
    auto bits = reinterpret_cast<hdrbits *>(hdrptr);
    *bits = newBits;
}


} // anon ns

MessageBuilder::NewHeader
MessageBuilder::createHeader(uint16_t id, bool qr, Message::Header::OPCODE opcode, bool rd)
{
    assert(buffer_.empty() || buffer_.size() == 2);

    increaseBuffer(Header::SIZE);

    auto *v = reinterpret_cast<uint16_t *>(buffer_.data());
    *v = htons(id);
    ++v; // Now points to the flags section at offset 2

    uint16_t bits = {};

    // We use hdrbits to address small unsigned integers and bits inside a 2 octets memory location.
    static_assert(sizeof(hdrbits) == 2);
    static_assert(sizeof(hdrbits) == sizeof(bits));

    auto opcodeValue = static_cast<uint8_t>(opcode);
    if (opcodeValue >= static_cast<char>(Header::OPCODE::RESERVED_)) {
        throw runtime_error{"createHeader: Invalid opcode "s + to_string(opcodeValue)};
    }

    auto *b = reinterpret_cast<hdrbits *>(&bits);
    b->qr = qr;
    b->rd = rd;
    b->opcode = static_cast<uint8_t>(opcode);

    if (opcode == Message::Header::OPCODE::NOTIFY) {
        b->aa = true;
    }

    *v = bits;

    return NewHeader{buffer_};
}

bool MessageBuilder::addRr(const Rr &rr, NewHeader& hdr, MessageBuilder::Segment segment)
{
    if (maxBufferSize_ && buffer_.size() + rr.size() >= maxBufferSize_) {
truncate:
        LOG_TRACE << "MessageBuilder::addRr: Out of buffer-space";
        increaseBuffer(0); // Sync Message::span to the new buffer-size in case it reallocated
        if (segment == MessageBuilder::Segment::ANSWER) {
            // RFC 2181 section 9
            hdr.setTc(true);
        }
        return false;
    }

    auto start_buffer_len = buffer_.size();

    auto label_len = detail::writeLabels(rr.labels(), labels_, buffer_, maxBufferSize_);
    if (!label_len) {
        assert(start_buffer_len == buffer_.size());
        goto truncate;
    }

    const auto data_len = rr.dataLen() ;
    if (maxBufferSize_ && (buffer_.size() + data_len) >= maxBufferSize_) {
        // We don't want the labels from the start of this segment in the buffer
        buffer_.resize(start_buffer_len);
        goto truncate;
    }

    //buffer_.reserve(buffer_.size() + data_len);

    auto data = rr.dataSpanAfterLabel();
    copy(data.begin(), data.end(), back_inserter(buffer_));

    hdr.increment(segment);
    increaseBuffer(0); // Sync Message::span to the new buffer-size
    return true;
}

bool MessageBuilder::addQuestion(string_view fqdn, uint16_t type)
{
    const auto start_offset =  buffer_.size();
    auto llen = writeName<false>(buffer_, start_offset, fqdn);

    increaseBuffer(llen + 4);
    writeName(buffer_, start_offset, fqdn);
    set16bValueAt(buffer_, buffer_.size() - 4, type);
    set16bValueAt(buffer_, buffer_.size() - 2, CLASS_IN);
    NewHeader(buffer_).incQdcount();

    return true;
}

void MessageBuilder::addOpt(uint16_t maxBufferSize, uint16_t version)
{
    if (opt_) {
        throw runtime_error{"MessageBuilder::addOpt: Can only be called once on a message."};
    }
    opt_ = OptValues{maxBufferSize, version};
}

void MessageBuilder::setRcode(uint16_t rcode)
{
    if (!rcode_) {
        rcode_ = rcode;
    } else {
        LOG_DEBUG << "Ignoring another rcode on a message where the rcode is already set.";
    }
}

void MessageBuilder::setRcode(Message::Header::RCODE rcode)
{
    setRcode(static_cast<uint16_t>(rcode));
}

void MessageBuilder::finish()
{
    handleOpt();
    createIndex();
}

void MessageBuilder::handleOpt()
{
    const auto rcb = RrOpt::rcodeBits(rcode_);
    NewHeader hdr{buffer_};
    hdr.setRcode(rcb.hdr);

    if (!opt_) {
        if (rcb.opt) {
            LOG_ERROR << "RCODE is 12 bits, but there is no OPT record in reply!";
            hdr.setRcode(Message::Header::RCODE::SERVER_FAILURE);
        }
        return;
    }

    RrOpt opt{opt_->version, rcode_, opt_->bufferSize};
    addRr(opt, hdr, Segment::ADDITIONAL);
}

StorageBuilder::NewRr
StorageBuilder::createRr(span_t fqdn, uint16_t type, uint32_t ttl, span_t rdata,
                         bool isOneEntity)
{
    assert(!finished_);
    if (name_ptr_) {

        if (isOneEntity) {
            // A list of rr's (RRSet) always contain the same fqdn,
            // so if we already have the name, we re-use it.
            return createRr(name_ptr_, type, ttl, rdata);
        }

        // If we already have the fdqn, use the pointer.
        // However, this is not the common case, so for now
        // we only check against the first fqdn added.
        const auto dl = defaultLabels().string();
        if (!dl.empty() && (dl == string_view{fqdn.data(), fqdn.size()})) {
            return createRr(name_ptr_, type, ttl, rdata);
        }
    }

    const auto start_offset = buffer_.size();

    size_t labels_len = 0;
    if (!fqdn.empty()) {
        if (fqdn.back() == '.') {
            fqdn = fqdn.subspan(0, fqdn.size() -1);
        }
        labels_len =  fqdn.size() + 2;
    } else {
        labels_len = 1;
    }

    const auto len = calculateLen(labels_len, rdata.size());
    buffer_.resize(buffer_.size() + len);

    {
        const auto rlen = writeName(buffer_, start_offset, {fqdn.data(), fqdn.size()});
        //assert(name_ptr_ == 0);
        assert(start_offset != 0);
        if (!name_ptr_) {
            name_ptr_ = start_offset;
        }
        assert(rlen == labels_len);
    }

    if (label_len_ == 0) {
        label_len_ = labels_len;
    }
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

    const auto mname_size = writeName<false>(rdata, 0, mname);
    const auto rname_size = writeName<false, true>(rdata, 0, rname);

    rdata.resize(mname_size + rname_size + (4 * 5));

    // TODO: See if we can compress the labels if parts of the names are already present
    // in the builders buffer.
    auto bytes = writeName(rdata, 0, mname);
    assert(bytes == mname_size);

    bytes = writeName<true, true>(rdata, mname_size, rname);
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

StorageBuilder::NewRr StorageBuilder::createPtr(string_view fqdn, uint32_t ttl, string_view hostname)
{
    return createDomainNameInRdata(fqdn, TYPE_PTR, ttl, hostname);
}

StorageBuilder::NewRr StorageBuilder::createNs(string_view fqdn, uint32_t ttl, string_view ns)
{
    return createDomainNameInRdata(fqdn, TYPE_NS, ttl, ns);
}

StorageBuilder::NewRr StorageBuilder::createInt16AndLabels(string_view fqdn, uint16_t type, uint32_t ttl, uint16_t val, string_view label)
{
    vector<char> rdata;
    const auto host_size = writeName<false>(rdata, 0, label);
    rdata.resize(host_size + 2);

    setValueAt(rdata, 0, val);
    writeName(rdata, 2, label);

    return createRr(fqdn, type, ttl, rdata);
}


StorageBuilder::NewRr StorageBuilder::createMx(string_view fqdn, uint32_t ttl, uint16_t priority, string_view host)
{
    return createInt16AndLabels(fqdn, TYPE_MX, ttl, priority, host);
}

StorageBuilder::NewRr StorageBuilder::createAfsdb(string_view fqdn, uint32_t ttl, uint16_t subtype, string_view host)
{
    return createInt16AndLabels(fqdn, TYPE_AFSDB, ttl, subtype, host);
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

StorageBuilder::NewRr StorageBuilder::createHinfo(string_view fqdn, uint32_t ttl,
                                                  string_view cpu, string_view os)
{
    if (cpu.size() > TXT_SEGMENT_MAX || os.size() > TXT_SEGMENT_MAX) {
        throw runtime_error("StorageBuilder::createHinfo: cpu and os must be <= 255 bytes");
    }

    array<string_view, 2> segments = {cpu, os};
    return createTxtRdata(fqdn, ttl, segments, TYPE_HINFO);
}

StorageBuilder::NewRr StorageBuilder::createRp(string_view fqdn, uint32_t ttl,
                                               string_view mbox, string_view txt)
{
    vector<char> rdata;

    // We store the labels uncompressed
    const auto mbox_len = writeName<false, true>(rdata, 0, mbox);
    const auto txt_len = writeName<false>(rdata, 0, txt);

    rdata.resize(mbox_len + txt_len);
    writeName<true, true>(rdata, 0, mbox);
    writeName(rdata, mbox_len, txt);

    return createRr(fqdn, TYPE_RP, ttl, rdata);
}

StorageBuilder::NewRr StorageBuilder::createSrv(string_view fqdn, uint32_t ttl, uint16_t priority, uint16_t weight, uint16_t port, string_view target)
{
    vector<char> rdata;
    const auto target_len = writeName<false>(rdata, 0, target);

    rdata.resize(6 + target_len);
    set16bValueAt(rdata, 0, priority);
    set16bValueAt(rdata, 2, weight);
    set16bValueAt(rdata, 4, port);
    writeName(rdata, 6, target);
    return createRr(fqdn, TYPE_SRV, ttl, rdata);
}

StorageBuilder::NewRr StorageBuilder::createBase64(string_view fqdn, uint16_t type, uint32_t ttl, string_view base64EncodedBlob)
{
    const auto rdata = base64Decode(base64EncodedBlob);
    return createRr(fqdn, type, ttl, rdata);
}

StorageBuilder::NewRr StorageBuilder::createA(string_view fqdn, uint32_t ttl, const string &ip)
{
    auto addr = boost::asio::ip::address::from_string(string{ip});
    if (addr.is_v4()) {
        return createA(fqdn, ttl, addr.to_v4());
    } if (addr.is_v6()) {
        return createA(fqdn, ttl, addr.to_v6());
    }

    throw runtime_error{"StorageBuilder::createA: Failed to convert ip-string to ipv4/ipv6"};
}

StorageBuilder::NewRr StorageBuilder::createA(string_view fqdn, uint32_t ttl, string_view ip)
{
    return createA(fqdn, ttl, string{ip});
}

StorageBuilder::NewRr
StorageBuilder::createRr(uint16_t nameOffset, uint16_t type,
                         uint32_t ttl, boost::span<const char> rdata)
{
    assert(!finished_);
    const auto start_offset = buffer_.size();
    auto labels_len =  2;

    const auto len = calculateLen(labels_len, rdata.size());

    buffer_.resize(buffer_.size() + len);
    writeNamePtr(buffer_, start_offset, nameOffset);

    return finishRr(start_offset, labels_len, type, ttl, rdata);
}

StorageBuilder::NewRr StorageBuilder::addRr(const Rr &rr)
{
    const auto fqdn = rr.labels().string();
    return createRr(fqdn, rr.type(), rr.ttl(), rr.rdata(), false);
}

void StorageBuilder::replaceSoa(const RrSoa &soa)
{
    for(auto & it : index_) {
        if (it.type == TYPE_SOA) {
            RrSoa old_soa{buffer_, it.offset};

            auto old_area = old_soa.dataSpanAfterLabel();
            auto new_area = soa.dataSpanAfterLabel();

            if (old_area.size() != new_area.size()) {
                throw runtime_error{"StorageBuilder::replaceSoa: SOA records not same size!"};
            }

            auto ptr = const_cast<char *>(old_area.data());
            memcpy(ptr, new_area.data(), old_area.size());
            return;
        }
    }

    throw runtime_error{"StorageBuilder::replaceSoa: SOA record not found!"};
}

/* Update the header to it's correct binary value.
 * Sort and add the index to the buffer
 */
void StorageBuilder::finish()
{
    assert(!finished_);
    static_assert(BUFFER_HEADER_LEN == sizeof(Header));

    if (buffer_.size() < sizeof(Header)) {
        throw runtime_error{"StorageBuilder::finish: No room in buffer_ for the header."};
    }

    if (sort_) {
        sort(index_.begin(), index_.end(), [](const auto& left, const auto& right) {
            static constexpr array<uint8_t, 255> sorting_table = {
                9, /* a */ 3, /* ns */ 2, 9, 9, /* cname */ 5, /* soa */ 1, 9, 9, 9, // 0
                9, 9, 9, 9, 9, /* mx */ 6, /* txt */ 7, 9, 9, 9,                     // 10
                9, 9, 9, 9, 9, 9, 9, 9, /* aaaa */ 4, 9,                             // 20
                16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
                16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
                16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
                16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
                16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
                16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
                16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
                16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
                16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
                16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
                16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
                16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
                16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
                16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
                16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
                16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
                16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
                16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
                16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
                16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
                16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
                16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
                16, 16, 16, 16, 16
            };

            assert(left.type < sorting_table.size());
            assert(right.type < sorting_table.size());
            return sorting_table.at(left.type) < sorting_table.at(right.type);
        });
    }// sort

    boost::span index{reinterpret_cast<const char *>(&index_[0]), index_.size() * sizeof(Index)};

    // Convert the index entries to network byte order
    for(auto& e : index_) {
        e.offset = htons(e.offset);
        e.type = htons(e.type);
    }

    // Append the (may be) sorted and converted index to the buffer.
    index_offset_ = buffer_.size();
    buffer_.insert(buffer_.end(), index.begin(), index.end());

    // Commit the header
    auto *h = reinterpret_cast<Header *>(buffer_.data());
    *h = {};
    h->flags = flags_;
    h->rrcount = htons(static_cast<uint16_t>(index_.size()));
    h->labelsize = label_len_;
    h->zonelen = zonelen_;
    h->ixoffset = htons(index_offset_);

    assert(h->version == CURRENT_STORAGE_VERSION);
    finished_ = true;
}

StorageBuilder::Header StorageBuilder::header() const
{
    if (buffer_.size() < sizeof(Header)) {
        throw runtime_error{"StorageBuilder::finish: No room in buffer_ for the header."};
    }

    return *reinterpret_cast<const Header *>(buffer_.data());
}

void StorageBuilder::setZoneLen(size_t len) {
    if (len > 255) {
        throw std::runtime_error{"setZoneLen: too long!"};
    }
    zonelen_ = static_cast<uint8_t>(len);
}

uint32_t StorageBuilder::incrementSoaVersion(const Entry &entry)
{
    if (!soa_offset_) {
        throw runtime_error{"incrementSoaVersion: No soa_offset_"};
    }

    auto oldSoa = find_if(entry.begin(), entry.end(), [](const auto &v) {
        return v.type() == TYPE_SOA;
    });

    if (oldSoa == entry.end()) {
        throw runtime_error{"incrementSoaVersion: No soa in entry"};
    }

    RrSoa rrOldSoa{entry.buffer(), oldSoa->offset()};
    const auto oldSerial = rrOldSoa.serial();

    RrSoa newSoa{buffer_, soa_offset_};
    const auto offset = newSoa.serialOffset();

    LOG_TRACE << "New soa version before increment: " << newSoa.serial();

    const auto new_version = oldSerial + 1;
    setValueAt(buffer_, offset, new_version);

    LOG_TRACE << "New soa version after increment: " << newSoa.serial();

    return new_version;
}

StorageBuilder::NewRr StorageBuilder::createDomainNameInRdata(string_view fqdn, uint16_t type, uint32_t ttl, string_view dname)
{
    vector<char> rdata;
    const auto mname_size = writeName<false>(rdata, 0, dname);
    rdata.resize(mname_size);
    writeName(rdata, 0, dname);
    return createRr(fqdn, type, ttl, rdata);
}

StorageBuilder::NewRr
StorageBuilder::finishRr(uint16_t startOffset, uint16_t labelLen, uint16_t type,
                         uint32_t ttl, boost::span<const char> rdata)
{
    if (type == TYPE_SOA) {
        if (soa_offset_ == 0) {
            soa_offset_ = startOffset;
        } else if (one_soa_) {
            throw runtime_error{"StorageBuilder::finishRr: More than one SOA!"};
        }
        assert(soa_offset_ > 0);
    }

    ttl = sanitizeTtl(ttl);

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
    boost::span b{span_.data(), 100};
    return get16bValueAt(boost::span{span_.data(), span_.size()}, 0);
}

bool Message::Header::qr() const
{
    return getHdrFlags(span_).qr;
}

Message::Header::OPCODE Message::Header::opcode() const
{
    return static_cast<OPCODE>(getHdrFlags(span_).opcode);
}

bool Message::Header::aa() const
{
    return getHdrFlags(span_).aa;
}

bool Message::Header::tc() const
{
    return getHdrFlags(span_).tc;
}

bool Message::Header::rd() const
{
    return getHdrFlags(span_).rd;
}

bool Message::Header::ra() const
{
    return getHdrFlags(span_).ra;
}

bool Message::Header::z() const
{
    return getHdrFlags(span_).z;
}

Message::Header::RCODE Message::Header::rcode() const
{
    return static_cast<RCODE>(getHdrFlags(span_).rcode);
}

uint16_t Message::Header::qdcount() const
{
    return get16bValueAt(span_, 4);
}

uint16_t Message::Header::ancount() const
{
    return get16bValueAt(span_, 6);
}

uint16_t Message::Header::nscount() const
{
    return get16bValueAt(span_, 8);
}

uint16_t Message::Header::arcount() const
{
    return get16bValueAt(span_, 10);
}

bool Message::Header::validate() const
{
    auto flags = getHdrFlags(span_);

    if (flags.opcode >= static_cast<uint8_t>(OPCODE::RESERVED_)) {
        LOG_TRACE << "Message::Header::validate(): Invalid opcode";
        return false;
    }

    if (flags.opcode == static_cast<uint8_t>(OPCODE::QUERY) && flags.aa && !flags.qr) {
        LOG_TRACE << "Message::Header::validate(): aa flag set in query";
        return false;
    }

    if (flags.tc && !flags.qr) {
        LOG_TRACE << "Message::Header::validate(): tc flag set in query. Unusual, but acceptable.";
        //return false;
    }

    if (flags.ra && !flags.qr) {
        LOG_TRACE << "Message::Header::validate(): ra flag set in query";
        return false;
    }

    if (flags.z) {
        LOG_TRACE << "Message::Header::validate(): z (reserved) must be 0";
        return false;
    }

    if (flags.rcode) {
        if (!flags.qr) {
            LOG_TRACE << "Message::Header::validate(): rcode set in query";
            return false;
        }

//        if (flags.rcode > static_cast<uint8_t>(Header::RCODE::RESERVED_)) {
//            LOG_TRACE << "Message::Header::validate(): Invalid rcode";
//            return false;
//        }
    }

    if (!flags.qr) {
        if (ancount()) {
            LOG_TRACE << "Message::Header::validate(): ancount in query";
            return false;
        }

//        if (nscount()) {
//            LOG_TRACE << "Message::Header::validate(): nscount in query";
//            return false;
//        }
    }

    return true;
}

void MessageBuilder::NewHeader::incQdcount()
{
    inc16BitValueAt(*mutable_buffer_, 4);
}

void MessageBuilder::NewHeader::incAncount()
{
    inc16BitValueAt(*mutable_buffer_, 6);
}

void MessageBuilder::NewHeader::incNscount()
{
    inc16BitValueAt(*mutable_buffer_, 8);
}

void MessageBuilder::NewHeader::incArcount()
{
    inc16BitValueAt(*mutable_buffer_, 10);
}

void MessageBuilder::NewHeader::increment(MessageBuilder::Segment segment)
{
    inc16BitValueAt(*mutable_buffer_, 4 + (static_cast<uint16_t>(segment) * 2));
}

void MessageBuilder::NewHeader::setAa(bool flag)
{
    auto bits = getHdrFlags(*mutable_buffer_);
    bits.aa = flag;
    setHdrFlags(*mutable_buffer_, bits);
}

void MessageBuilder::NewHeader::setTc(bool flag)
{
    auto bits = getHdrFlags(*mutable_buffer_);
    bits.tc = flag;
    setHdrFlags(*mutable_buffer_, bits);
}

void MessageBuilder::NewHeader::setRa(bool flag)
{
    auto bits = getHdrFlags(*mutable_buffer_);
    bits.ra = flag;
    setHdrFlags(*mutable_buffer_, bits);
}

void MessageBuilder::NewHeader::setRcode(uint8_t rcode)
{
    auto bits = getHdrFlags(*mutable_buffer_);
    bits.rcode = rcode;
    setHdrFlags(*mutable_buffer_, bits);
}

void MessageBuilder::NewHeader::setRcode(Message::Header::RCODE rcode)
{
    setRcode(static_cast<uint8_t>(rcode));
}

void MessageBuilder::NewHeader::setOpcode(Message::Header::OPCODE code)
{
    auto bits = getHdrFlags(*mutable_buffer_);
    bits.opcode = static_cast<uint8_t>(code);
    setHdrFlags(*mutable_buffer_, bits);
}

Message::Message(span_t span)
    : span_{span}
{
    createIndex();
}

Message::Header Message::header() const
{
    return Header{span_};
}

const span_t &Message::span() const
{
    return span_;
}

std::optional<RrSoa> Message::getSoa() const
{
    for(auto& rr : getAnswers()) {
        if (rr.type() == TYPE_SOA) {
            return {RrSoa{span_, rr.offset()}};
        }
    }
    return {};
}

void Message::createIndex()
{
    Header hdr{span_};

    if (!hdr.validate()) {
        throw runtime_error{"Message::createIndex: Invalid message header"};
    }

    // Now, go trough each RR section.
    size_t offset = nsblast::lib::Message::Header::SIZE;
    size_t sectionIndex = 0;
    for(auto sectionCount : {hdr.qdcount(), hdr.ancount(), hdr.nscount(), hdr.arcount()}) {
        if (sectionCount) {
            auto& rs = rrsets_.at(sectionIndex);
            rs.emplace(span_, offset, sectionCount, sectionIndex == 0);
            offset += rs->bytes();
        } else {
            rrsets_.at(sectionIndex).reset();
        }
        ++sectionIndex;
    }
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

                if (size_ > 255) { // size_ + 1 byte for the first size-byte.
                    throw runtime_error{"Labels::parse: Labels exeed the 255 bytes limit for a fqdn"};
                }

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
            if ((ch & START_OF_EXT_LABEL_TAG) == START_OF_EXT_LABEL_TAG) [[unlikely]] {
                // Depricated in RFC 6891
                throw runtime_error{"Deprecated: Extended Label Type 0x40"};
            }
            if (ch > 63) [[unlikely]]  {
                throw runtime_error("Labels::parse: Max label size is 63 bytes: This label is "s + to_string(ch));
            }
            if (offset + ch >= buffer.size()) [[unlikely]] {
                throw runtime_error("Labels::parse: Labels exeed the containing buffer-size");
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

        if (size_ > 254) { // size_ + 1 byte for the first size-byte and 1 byte for the root node.
            throw runtime_error{"Labels::parse: Labels exeed the 255 bytes limit for a fqdn"};
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
        const auto *b =  buffer_.data() + static_cast<size_t>(current_loc_) + 1;
        const auto len = static_cast<uint8_t>(buffer_[current_loc_]);

        csw_ = {b, len};

        if (csw_.empty()) {
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
    if (isQuery()) {
        return 0;
    }
    return get32bValueAt(self_view_, offset_to_type_ + 4);
}

uint16_t Rr::rdlength() const
{
    if (isQuery()) {
        return 0;
    }
    return get16bValueAt(self_view_, offset_to_type_ + 8);
}

Rr::buffer_t Rr::rdata() const
{
    if (isQuery()) {
        return {};
    }
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

string Rr::rdataAsBase64() const
{
    return Base64Encode(rdata());
}

RrInfo Rr::rrInfo() const noexcept {
    assert(!self_view_.empty());
    assert(offset_to_type_ > 0);
    const auto llen = static_cast<uint16_t>(offset_to_type_);
    return {static_cast<uint16_t>(offset_), static_cast<uint16_t>(size()), llen};
}

void Rr::parse(bool isQuery)
{
    const auto max_window_size = buffer_view_.size() - offset_;
    if (max_window_size < 2) {
        throw runtime_error{"Rr::parse: Buffer-window < 2 bytes!"};
    }

    size_t labelLen = 2;

    // Only parse the labels at this time if it's not a pointer.
    // If it's a pointer, we know that the buffer size for the label section
    // is 2 bytes.
    if (buffer_view_[offset_] == 0) {
        // Root. Only one byte
        labelLen = 1;
    } else if ((buffer_view_[offset_] & START_OF_POINTER_TAG) != START_OF_POINTER_TAG) {
        labels_.emplace(buffer_view_, offset_);
        labelLen = labels_->bytes();
    }

    // Get the start of the buffer after the labels (in self_view_)
    offset_to_type_ = labelLen;

    if (isQuery) {
        // The query has only labels, qtype and qclass.
         self_view_ =  buffer_view_.subspan(offset_, offset_to_type_ + 4);
         return;
    }

    const auto rdlenSizeOffset = offset_ + offset_to_type_ + 2 +  2 + 4;

    if ((rdlenSizeOffset + 2) > buffer_view_.size()) {
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

RrList::RrList(RrList::buffer_t bufferView, uint16_t offset, uint16_t count, bool isQuestion)
    : view_{bufferView}, offset_{offset}, count_{count}, isQuestion_{isQuestion}
{
    parse();
}

RrList::Iterator RrList::begin() const
{
    return Iterator{view_, offset_, index_, isQuestion_};
}

RrList::Iterator RrList::end() const
{
    return Iterator{{}, {}, index_, isQuestion_};
}

void RrList::parse()
{
    uint16_t coffset = offset_;
    for(size_t i = 0; i < count_; ++i) {
        Rr rr{view_, coffset, isQuestion_};
        index_.emplace_back(rr.type(), coffset);
        coffset += rr.size();
        bytes_ += rr.size();
    }
}

RrList::Iterator::Iterator(boost::span<const char> buffer,
                          uint16_t /*offset*/,
                          const RrList::index_t &index,
                          bool isQuestion)
    : index_{index}, buffer_{buffer}, isQuestion_{isQuestion}
{
    if (buffer.empty()) { // end() iterator
        current_ = index_.end();
        return;
    }

    current_ = index_.begin();
    update();
}

RrList::Iterator &RrList::Iterator::operator++()
{
    increment();
    return *this;
}

RrList::Iterator RrList::Iterator::operator++(int) {
    auto self = *this;
    increment();
    return self;
}

bool RrList::Iterator::equals(const boost::span<const char> a, const boost::span<const char> b)
{
    return a.data() == b.data() && a.size() == b.size();
}

void RrList::Iterator::update()
{
    if (current_ == index_.end()) {
        crr_ = {};
        return;
    }

    crr_ = {buffer_, current_->offset, isQuestion_};
}

void RrList::Iterator::increment()
{
    assert(current_ != index_.end());
    ++current_;
    update();
}

Labels RrSoa::mname() const
{
    return {rdata(), 0};
}

string RrSoa::email() const
{
    auto rn = rname().string();
    string email;
    email.reserve(rn.size());
    char prev = 0;
    for(auto it = rn.begin(); it != rn.end(); ++it) {
        const char ch = *it;
        if (ch == '.') [[unlikely]] {
            if (prev == '\\') {
                ; // escaped
            } else {
                // end of segment.
                email += '@';
                ++it;
                copy(it, rn.end(), back_inserter(email));
                break;
            }
        }
        prev = ch;
        if (ch != '\\') [[likely]] {
            email += ch;
        }
    }

    return email;
}

Labels RrSoa::rname() const
{
    // TODO: Not optimal...
    return {rdata(), mname().bytes()};
}

uint32_t RrSoa::serial() const
{
    const auto rd = rdata();
    assert(rd.size() >= 22);
    return get32bValueAt(rd, rd.size() - 20);
}

uint32_t RrSoa::refresh() const
{
    const auto rd = rdata();
    assert(rd.size() >= 22);
    return get32bValueAt(rd, rd.size() - 16);
}

uint32_t RrSoa::retry() const
{
    const auto rd = rdata();
    assert(rd.size() >= 22);
    return get32bValueAt(rd, rd.size() - 12);
}

uint32_t RrSoa::expire() const
{
    const auto rd = rdata();
    assert(rd.size() >= 22);
    return get32bValueAt(rd, rd.size() - 8);
}

uint32_t RrSoa::minimum() const
{
    const auto rd = rdata();
    assert(rd.size() >= 22);
    return get32bValueAt(rd, rd.size() - 4);
}

uint16_t RrSoa::serialOffset() const
{
    const auto diff = rdata().data() - buffer_view_.data();
    assert(diff >= 0);
    assert(diff < static_cast<decltype(diff)>(buffer_view_.size()));

    auto offset = diff + (static_cast<long>(rdata().size()) - 20);
    assert(offset < static_cast<decltype(offset)>(buffer_view_.size()));
    assert(offset > 0);
    return static_cast<uint16_t>(offset);
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
    : span_(buffer)
    , header_{reinterpret_cast<const Header *>(span_.data())}
    , count_{ntohs(header_->rrcount)}
    , index_{mkIndex(span_, *header_, count_)}
{
}

RrSoa Entry::getSoa() const
{
    assert(hasSoa());
    return {buffer(), begin()->offset()};
}



boost::asio::ip::address RrA::address() const
{
    if (rdata().size() == 4) {
        return bufferToAddr<boost::asio::ip::address_v4>(rdata());
    }

    return bufferToAddr<boost::asio::ip::address_v6>(rdata());
}

string RrA::string() const
{
    return address().to_string();
}

uint32_t sanitizeTtl(uint32_t ttl) noexcept
{
    return min(ttl, TTL_MAX);
}

Labels RrPtr::ptrdname() const
{
    if (type() != TYPE_PTR) {
        throw runtime_error{"Not a TYPE_PTR"};
    }
    return {rdata(), 0};
}

string_view RrHinfo::cpu() const
{
    if (type() != TYPE_HINFO) {
        throw runtime_error{"Not a TYPE_HINFO"};
    }

    return getTextFromRdata<2>(rdata(), 0);
}

string_view RrHinfo::os() const
{
    if (type() != TYPE_HINFO) {
        throw runtime_error{"Not a TYPE_HINFO"};
    }

    return getTextFromRdata<2>(rdata(), 1);
}

Labels RrRp::mbox() const
{
    if (type() != TYPE_RP) {
        throw runtime_error{"Not a TYPE_RP"};
    }

    return getLabelsFromRdata<2>(rdata(), 0);
}

Labels RrRp::txt() const
{
    if (type() != TYPE_RP) {
        throw runtime_error{"Not a TYPE_RP"};
    }

    return getLabelsFromRdata<2>(rdata(), 1);
}

Labels RrAfsdb::host()
{
    if (type() != TYPE_AFSDB) {
        throw runtime_error{"Not a TYPE_AFSDB"};
    }

    return {rdata(), 2};
}

uint32_t RrAfsdb::subtype() const
{
    if (type() != TYPE_AFSDB) {
        throw runtime_error{"Not a TYPE_AFSDB"};
    }

    const auto rd = rdata();
    assert(rd.size() >= 2);
    return get16bValueAt(rd, 0);
}

Labels RrSrv::target() const
{
    if (type() != TYPE_SRV) {
        throw runtime_error{"Not a TYPE_SRV"};
    }

    return {rdata(), 6};
}

uint32_t RrSrv::priority() const
{
    if (type() != TYPE_SRV) {
        throw runtime_error{"Not a TYPE_SRV"};
    }
    return get16bValueAt(rdata(), 0);
}

uint32_t RrSrv::weight() const
{
    if (type() != TYPE_SRV) {
        throw runtime_error{"Not a TYPE_SRV"};
    }
    return get16bValueAt(rdata(), 2);
}

uint32_t RrSrv::port() const
{
    if (type() != TYPE_SRV) {
        throw runtime_error{"Not a TYPE_SRV"};
    }
    return get16bValueAt(rdata(), 4);
}


RrOpt::RrOpt(span_t span, uint16_t offset)
    : Rr(span, offset)
{
}

RrOpt::RrOpt(span_t span, uint32_t offset)
    : Rr(span, offset)
{
    if (offset > numeric_limits<uint16_t>::max()) {
        throw runtime_error{"offset out of 16 bit range: "s + to_string(offset)};
    }
}

RrOpt::RrOpt(uint16_t version, uint16_t rcode, uint16_t bufferLen)
{
    buffer_.resize(1 + 2 + 2 + 4 + 2);

    uint16_t offset = 0;
    buffer_[offset] = 0; // Root / no labels
    ++offset;
    offset_to_type_ = offset;
    setValueAt(buffer_, offset, TYPE_OPT);
    offset += 2;
    setValueAt(buffer_, offset, bufferLen);
    offset += 2;

    auto rcb = rcodeBits(rcode);

    TtlBits ttlbits = {};
    ttlbits.extRcode = rcb.opt;
    ttlbits.version = version;

    static_assert(sizeof(ttlbits) == 4);
    memcpy(buffer_.data() + offset, &ttlbits, 4);
    offset += 4;
    setValueAt(buffer_, offset, static_cast<uint16_t>(0)); // rdlength
    offset += 2;
    assert(offset == buffer_.size());
    buffer_view_ = buffer_;
    self_view_ = buffer_;
}

uint16_t RrOpt::version() const
{
    auto bits = reinterpret_cast<const TtlBits *>(self_view_.data() + offset_to_type_ + 4);
    return bits->version;
}

uint8_t RrOpt::rcode() const
{
    auto bits = reinterpret_cast<const TtlBits *>(self_view_.data() + offset_to_type_ + 4);
    return bits->extRcode;
}

uint16_t RrOpt::maxBufferLen() const
{
    return clas();
}

uint16_t RrOpt::fullRcode(u_int8_t hdrRcode) const
{
    return rcodeBits(hdrRcode, rcode());
}

RrOpt::RcodeBits RrOpt::rcodeBits(uint16_t rcode)
{
    RcodeUn bu = {};
    bu.val = rcode;
    bu.bits.unused = 0;
    return bu.bits;
}

uint16_t RrOpt::rcodeBits(RcodeBits bits) {
    return rcodeBits(bits.hdr, bits.opt);
}

uint16_t RrOpt::rcodeBits(uint8_t hdrValue, uint8_t optValue)
{
    RcodeUn bu = {};
    bu.bits.hdr = hdrValue;
    bu.bits.opt = optValue;
    bu.bits.unused = 0;

    return bu.val;
}

MutableRrSoa::MutableRrSoa(const RrSoa &from)
    : RrSoa(span_t(), 0)
{
   *this = from;
}

MutableRrSoa::MutableRrSoa(uint32_t serial)
    : RrSoa(span_t(), 0)
{
    StorageBuilder soaSb;
    std::array<char, 22> rdata = {};
    setValueAt(rdata, rdata.size() - 20, serial);

    const auto nh = soaSb.createRr({}, TYPE_SOA, 0, rdata);

    // No need to call finish(). We don't need the index and the extras.

    // Take ownership of the buffer with the soa,
    buffer_ = move(soaSb.stealBuffer());

    // Now, do what the RR's constructor does to get the internals right...
    reset();
    buffer_view_ = buffer_;
    offset_ = nh.offset();
    parse(false);
}

MutableRrSoa &MutableRrSoa::operator =(const RrSoa &soa)
{
    // For now, use the StorageBuilder to create a copy of the soa
    StorageBuilder soaSb;
    auto soa_fqdn = labelsToFqdnKey(soa.labels());
    const auto nh = soaSb.createRr(soa_fqdn, TYPE_SOA, soa.ttl(), soa.rdata());

    // No need to call finish(). We don't need the index and the extras.

    // Take ownership of the buffer with the soa,
    buffer_ = move(soaSb.stealBuffer());

    // Now, do what the RR's constructor does to get the internals right...
    reset();
    buffer_view_ = buffer_;
    offset_ = nh.offset();
    parse(false);
}

void MutableRrSoa::incVersion()
{
    auto ser = serial();
    ++ser;
    const auto rd = rdata();
    assert(rd.size() >= 24);

    auto rd_offset = rd.data() - buffer_.data();
    assert(rd_offset > 0);
    assert(static_cast<size_t>(rd_offset) < buffer_.size());

    setValueAt(buffer_, (rd_offset + rd.size()) - 20, ser);
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
    return *this;
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
