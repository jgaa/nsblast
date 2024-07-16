#pragma once

#include <limits>
#include <iterator>
#include <cassert>
#include <deque>
#include <boost/asio.hpp>
#include <boost/uuid/string_generator.hpp>
#include "nsblast/nsblast.h"


namespace nsblast::lib {

constexpr char BUFFER_HEADER_LEN = 8;

static constexpr uint32_t TTL_MAX = 2147483647; // RFC 2181 8
uint32_t sanitizeTtl(uint32_t ttl) noexcept;
struct RrInfo;

/// "Magic "uuid" for the nsblast tenant
static const boost::uuids::uuid nsblastTenantUuid = boost::uuids::string_generator()("{85b185fc-6767-11ee-aad2-1bf9c8825814}");


/*! Representation of RFC1035 labels
 *
 *  A label is a single node in the name-tree.
 *  It must start with a ASCII letter, and then contain letters or numbers.
 *  '.' can exist as part of the label, but normally it is used in strings
 *  as a delimiter between labels. If '.' is used, it's escaped with '\'
 *  in the text-representation of the label.
 *
 *  The binary representation is a single unsigned octet as a length
 *  field, immediately followed by "length" characters. Unlike, C
 *  strings, there are no \0 string terminator.
 *
 *  A length of 0 represents the root node in the global DNS name space.
 *
 */
class Labels {
public: 
    /*! Simple forward iterator to allow labels to looped over */
    class Iterator {
    public:
        using iterator_category = std::forward_iterator_tag;
        using difference_type   = std::ptrdiff_t; // best nonsense choise?
        using value_type        = std::string_view;
        using pointer           = const value_type*;
        using reference         = const value_type&;

        Iterator(boost::span<const char> buffer, uint16_t offset);

        Iterator(const Iterator& it);

        Iterator& operator = (const Iterator& it);

        reference operator*() const { return csw_; }

        pointer operator->() { return &csw_; }

        Iterator& operator++();

        Iterator operator++(int);

        friend bool operator== (const Iterator& a, const Iterator& b) {
            return equals(a.buffer_, b.buffer_) && a.current_loc_ == b.current_loc_;
        }

        friend bool operator!= (const Iterator& a, const Iterator& b) {
            return !equals(a.buffer_, b.buffer_) || a.current_loc_ != b.current_loc_;
        }

        auto location() const {
            return current_loc_;
        }

    private:
        static bool equals(const boost::span<const char> a, const boost::span<const char> b);
        void update();
        void increment();
        void followPointers();

        boost::span<const char> buffer_;
        uint16_t current_loc_ = 0;
        std::string_view csw_;
    };


    /*! Constructor
     *
     *  \param buffer Buffer that covers the message.
     *  \param startOffset The location in the buffer where this
     *     labels data start.
     *
     *  The lables may contain pointers to other labels within
     *  the message, and therefore, we need the boundry for the
     *  messages buffer in order to parse and validate the labels.
     *
     *  The constructor parses and validates the buffer, and
     *  sets it local buffer_view_ to exactely match the
     *  labels from the original buffer(?).
     *
     *  Note: This object does not own any buffers.
     */
    Labels(boost::span<const char> buffer, size_t startOffset);
    Labels() = default;

    Labels(const Labels&) = default;
    Labels(Labels&&) = default;

    Labels& operator = (const Labels&) = default;
    Labels& operator = (Labels&&) = default;

    /*! Returns the size of the labels
     *
     *  Equals stringlen, including the trailing dot
     */
    size_t size() const noexcept;


    /*! Returns the bytes used by this label in the buffer
     *
     *  This may be 1 for a root node, 2 for a pointer, or up to
     *  63 bytes for a normal or partial label.
     */

    uint16_t bytes() const noexcept;

    /*! Returns the buffer space occupied by the labels
     *
     *  This is the buffer used by the labels, so we can calculate
     *  the offset of the next segment in a message. A pointer
     *  for example, is 2 bytes, no matter what domain name it
     *  points to. A root node is 1 byte.
     */
    auto buffer() const noexcept {
        return buffer_view_;
    }

    /*! Returns the number of labels in the list, including the trailing root label. */
    size_t count() const noexcept;

    /*! Return the fqdn as a string */
    std::string string(bool showRoot=false) const;

    Iterator begin() const {
        return Iterator(buffer_view_, offset_);
    }

    Iterator end() const {
        return Iterator({}, 0);
    }

    // Buffer used by these labels. A ending pointer will point outside the selfView
    span_t selfView() const noexcept {
        if (buffer_view_.empty()) {
            return {};
        }
        return {buffer_view_.data() + offset_, bytes_};
    }

    bool empty() const noexcept {
        return buffer_view_.empty();
    }

    auto offset() const noexcept {
        return offset_;
    }

private:
    /*! Parse the buffer.
     *
     *  \throws std::runtime_error on buffer-validation errors.
     */
    void parse(boost::span<const char> buffer, size_t startOffset);

    size_t count_ = {}; // Number of labels
    size_t size_ = {}; // Number of bytes for the fqdn (stringlen, including trailing dot)
    uint16_t bytes_ = {}; // Number of bytes this label occupies in the buffer
    uint16_t offset_ = {}; // Offset to the start of the buffer
    boost::span<const char> buffer_view_; // A span over the full buffer
};

/*! Representation for a query or Resource Record.
 *
 *  The rr does not own it's buffer.
 *
 *  Queries contains a sub-set of the ResourceRecord, and the
 *  irrelevant methods will return 0/empty for query entries.
 *
 *  Only the buffer for acrtual Rr's can be used to instaiatye Rr* objects
 *
 */
class Rr {
public:
    using buffer_t = boost::span<const char>;

    Rr() = default;
    Rr(const Rr&) = default;
    Rr(buffer_t bufferView, uint32_t offset, bool isQuery = false)
        : buffer_view_{bufferView}, offset_{offset} {
        if (!bufferView.empty()) [[likely]] {
            parse(isQuery);
        }
    }

    Rr& operator = (const Rr&) = default;

    uint16_t type() const;
    uint16_t clas() const;
    uint32_t ttl() const;
    uint16_t rdlength() const;
    buffer_t rdata() const;

    Labels labels() const;

    size_t size() const {
        return self_view_.size();
    }

    uint32_t offset() const noexcept {
        return offset_;
    }

    auto view() const noexcept {
        return self_view_;
    }

    /*! Return the span for the buffer that RR resides in */
    auto span() const noexcept {
        return buffer_view_;
    }

    auto isQuery() const noexcept {
        return self_view_.size() - offset_to_type_ == 4;
    }

    uint16_t staticDataLen() const noexcept {
        return isQuery() ? 4 : 10;
    }

    /*! Length of the segment excluding the labels part */
    uint32_t dataLen() const noexcept {
        return staticDataLen() + rdlength();
    }

    span_t selfSpan() const noexcept {
        return self_view_;
    }

    /*! The buffer after label and until (including) rdata) */
    span_t dataSpanAfterLabel() const noexcept {
        const auto llen = labels().bytes();
        const uint16_t dlen = dataLen();
        return {self_view_.data() + llen, dlen};
    }

    std::string rdataAsBase64() const;

    void reset() {
        labels_.reset();
        buffer_view_ = {};
        offset_ = 0;
        offset_to_type_ = 0;
        self_view_ = {};
    }

    RrInfo rrInfo() const noexcept;

    std::string_view typeName() const;

protected:
    void parse(bool isQuery);

    mutable std::optional<Labels> labels_;
    buffer_t buffer_view_;
    uint32_t offset_ = 0;
    uint32_t offset_to_type_ = 0;
    buffer_t self_view_;
};

#pragma pack(push, 1)
struct RrInfo {
    uint16_t offset;
    uint16_t size;
    uint16_t labelLen:15;
    uint16_t left; // Tag some times used by comparsion algorithms to select the appropriate buffer

    /*! The buffer after label and until (including) rdata) */
    span_t dataSpanAfterLabel(span_t buffer) const noexcept {
        span_t b{buffer.data() + offset + labelLen,
                    static_cast<size_t>(size - labelLen)};
        assert(b.data() >= buffer.data() && b.data() < buffer.data() + buffer.size());
        assert(b.data() + b.size() <= buffer.data() + buffer.size());
        return b;
    }

    span_t span(span_t buffer) const noexcept {
        span_t b{buffer.data() + offset, static_cast<size_t>(size)};
        assert(b.data() >= buffer.data() && b.data() < buffer.data() + buffer.size());
        assert(b.data() + b.size() <= buffer.data() + buffer.size());
        return b;
    }

    Rr rr(span_t buffer) const {
        return {buffer, offset};
    }
};
#pragma pack(pop)


/*! Wrapper over a RR SOA instance.
 *
 *  Can be used to simply obtain data from the record.
 */
class RrSoa : public Rr {
public:
    RrSoa(buffer_t bufferView, uint32_t offset)
        : Rr(bufferView, offset) {}

    Labels mname() const;
    /*! Convert rname to a normail email */
    std::string email() const;
    Labels rname() const;
    const boost::uuids::uuid& tenant() const;
    uint32_t serial() const;
    uint32_t refresh() const;
    uint32_t retry() const;
    uint32_t expire() const;
    uint32_t minimum() const;
    static constexpr size_t minRrLen = 22;

    /*! Offset of serial from the start of the original buffer */
    uint16_t serialOffset() const;

    /*! Helper function to convert an ordinary email to a rname */
    static std::string fromEmail(const std::string_view& email);

    static std::string_view fromEmailIfEmail(std::string_view rname, std::string& storage);

    /*! Helper function to convert an rname to email */
    static std::string ToEmail(const std::string_view& rname);
};

class MutableRrSoa : public RrSoa {
public:
    MutableRrSoa(const RrSoa& from);
    MutableRrSoa(uint32_t serial = 0);

    MutableRrSoa& operator = (const RrSoa& soa);

    void incVersion();

private:
    std::vector<char> buffer_;
};


/*! Wrapper over a RR RrSrv instance.
 *
 *  Can be used to simply obtain data from the record.
 */
class RrSrv : public Rr {
public:
    RrSrv(buffer_t bufferView, uint32_t offset)
        : Rr(bufferView, offset) {}

    Labels target() const;
    uint32_t priority() const;
    uint32_t weight() const;
    uint32_t port() const;
};

/*! Wrapper over a RR A instance.
 *
 *  Can be used to simply obtain data from the record.
 */
class RrA : public Rr {
public:
    RrA(buffer_t bufferView, uint32_t offset)
        : Rr(bufferView, offset) {}

    boost::asio::ip::address address() const;

    std::string string() const;
};


/*! Wrapper over a RR CNAME instance.
 *
 *  Can be used to simply obtain data from the record.
 */
class RrCname : public Rr {
public:
    RrCname(buffer_t bufferView, uint32_t offset)
        : Rr(bufferView, offset) {}

    Labels cname() const;
};

/*! Wrapper over a RR PTR instance.
 *
 *  Can be used to simply obtain data from the record.
 */
class RrPtr : public Rr {
public:
    RrPtr(buffer_t bufferView, uint32_t offset)
        : Rr(bufferView, offset) {}

    Labels ptrdname() const;
};

/*! Wrapper over a RR NS instance.
 *
 *  Can be used to simply obtain data from the record.
 */
class RrNs : public Rr {
public:
    RrNs(buffer_t bufferView, uint32_t offset)
        : Rr(bufferView, offset) {}

    Labels ns() const;
};

/*! Wrapper over a RR Hinfo instance.
 *
 *  Can be used to simply obtain data from the record.
 */
class RrHinfo : public Rr {
public:
    RrHinfo(buffer_t bufferView, uint32_t offset)
        : Rr(bufferView, offset) {}

    std::string_view cpu() const;
    std::string_view os() const;
};

/*! Wrapper over a RR RP instance.
 *
 *  Can be used to simply obtain data from the record.
 */
class RrRp : public Rr {
public:
    RrRp(buffer_t bufferView, uint32_t offset)
        : Rr(bufferView, offset) {}

    Labels mbox() const;
    Labels txt() const;
};


/*! Wrapper over a RR CNAME instance.
 *
 *  Can be used to simply obtain data from the record.
 */
class RrTxt : public Rr {
public:
    RrTxt(buffer_t bufferView, uint32_t offset)
        : Rr(bufferView, offset) {}

    /*! Get a container with the string elements.
     *
     *  Normally, there will only be one
     */
    template <typename V=std::string_view, typename T=std::vector<V>>
    auto text() const {
        T q;
        auto r = rdata();
        for(auto p = r.begin(); p < r.end();) {
            const auto len = static_cast<uint8_t>(*p);
            if ((p + 1+ len) > r.end()) {
                throw std::runtime_error{"Invalid bounds of string rdata-segment"};
            }
            ++p;
            q.emplace_back(&*p, len);
            p += len;
        }
        return q;
    }

    /*! Get a copy of the text as a normal string
     *
     *  If there are multiple elements, they will all
     *  be appended to the string returned.
     */
    auto string() const {
        const auto t = text();
        size_t len = 0;
        for(const auto& s: t) {
            len += s.size();
        }
        std::string rval;
        rval.reserve(len);
        for(const auto& s: t) {
            rval += s;
        }
        return rval;
    }
};

/*! Wrapper over a RR MX instance.
 *
 *  Can be used to simply obtain data from the record.
 */
class RrMx : public Rr {
public:
    RrMx(buffer_t bufferView, uint32_t offset)
        : Rr(bufferView, offset) {}

    Labels host() const;
    uint32_t priority() const;
};

/*! Wrapper over a RR MX instance.
 *
 *  Can be used to simply obtain data from the record.
 */
class RrAfsdb : public Rr {
public:
    RrAfsdb(buffer_t bufferView, uint32_t offset)
        : Rr(bufferView, offset) {}

    Labels host() const;
    uint32_t subtype() const;
};

/*! Constructor for and wrapper over RR OPT
 *
 *  For now, we only support RFC 6891 / version 0 (buffer-lenght).
 */

class RrOpt : public Rr{
public:
#pragma pack(push, 1)
    // The data in the 32 bit ttl field in network order
    struct TtlBits {
        uint32_t extRcode: 8;
        uint32_t version: 8;
        uint32_t do_: 1;
        uint32_t z: 15;
    };

    struct RcodeBits { // 12 bits
        uint8_t hdr : 4;
        uint8_t opt : 8;
        uint8_t unused: 4;
    };

    union RcodeUn {
        uint16_t val;
        RcodeBits bits;
    };

#pragma pack(pop)

    RrOpt() = delete;

    /*! Construct from existing data, f.eks. received in a query
     */
    explicit RrOpt(span_t span, uint16_t offset);

    explicit RrOpt(span_t span, uint32_t offset);

    /*! Construct self-contained instance
     *
     *  normally to copy to a reply
     *
     *  \param version Currently we support version 0
     *  \param rcode Full, 12 bit rcode in host byte order
     *  \param bufferLen Max buffer len
     */
    RrOpt(uint16_t version, uint16_t rcode, uint16_t bufferLen);

    uint16_t version() const; // from part of ttl field
    uint8_t rcode() const; // from part of ttl field
    uint16_t maxBufferLen() const; // from class field

    /*! Calculate the full rcode
     *
     *  \param hdrRcode 4 bit value from the message header
     *  \return The 12 bit return code in host byte order.
     */
    uint16_t fullRcode(u_int8_t hdrRcode) const;

    /*! Get the rcode bits from a normal rcode in host byte order */
    static RcodeBits rcodeBits(uint16_t rcode);
    static uint16_t  rcodeBits(RcodeBits bits);
    static uint16_t  rcodeBits(uint8_t hdrValue, uint8_t optValue);

private:
    std::vector<char> buffer_; // Used when self-contained
};

/*! Wrapper / view over a list of result sets
 *
 *  Used for example to represent the rr's in a section of a message,
 *  or the rr's stored for a key in the database.
 *
 *  The object does not own it's buffer
 */
class RrList {
public:
    using buffer_t = boost::span<const char>;

#pragma pack(push, 1)
    struct Index {
        Index(uint16_t type, uint16_t offset)
            : type{type}, offset{offset} {}
        Index(const Index&) = default;
        Index(Index&&) = default;

        Index& operator = (const Index&) = default;

        uint16_t type;
        uint16_t offset;
    };
#pragma pack(pop)
    using index_t = std::deque<Index>;

    /*! Simple forward iterator to allow us to iterate over the rr's */
    class Iterator {
    public:
        using iterator_category = std::forward_iterator_tag;
        using difference_type   = std::ptrdiff_t; // best nonsense choise?
        using value_type        = Rr;
        using pointer           = const value_type*;
        using reference         = const value_type&;

        // Empty buffer constructs and end() iterator
        Iterator(boost::span<const char> buffer, uint16_t offset, const index_t& index, bool isQuestion);

        Iterator(const Iterator& it) = default;

        Iterator& operator = (const Iterator& it);

        reference operator*() const { return crr_; }

        pointer operator->() { return &crr_; }

        Iterator& operator++();

        Iterator operator++(int);

        friend bool operator== (const Iterator& a, const Iterator& b) {
            return a.current_ == b.current_;
        }

        friend bool operator!= (const Iterator& a, const Iterator& b) {
            return a.current_ != b.current_;
        }

    private:
        static bool equals(const boost::span<const char> a, const boost::span<const char> b);
        void update();
        void increment();

        const index_t& index_;
        boost::span<const char> buffer_;
        index_t::const_iterator current_;
        Rr crr_;
        bool isQuestion_ = false;
    };

    RrList(buffer_t bufferView, uint16_t offset, uint16_t count, bool isQuestion);

    size_t count() const {
        return count_;
    }

    /*! The number of bytes used by this RrSet.
     *
     */
    size_t bytes() const {
        return bytes_;
    }

    auto buffer() const {
        return view_;
    }

    Iterator begin() const;

    Iterator end() const;

private:
    void parse();

    buffer_t view_;
    const uint16_t offset_ = 0;
    const uint16_t count_ = 0;
    index_t index_;
    uint16_t bytes_ = 0;
    bool isQuestion_ = false; // Those are shorter
};


/*! RFC 1035 message
 *
 *  - Header
 *  - RR sections:
 *      - Question
 *      - Answer
 *      - Authority
 *      - Additional
 */
class Message {
public:
    static constexpr std::uint16_t CLASS_IN = 1;

    class Header {
    public:
        constexpr static size_t SIZE = 12;

        Header(span_t b)
            : span_{b} {

        }

        /*! Randomly generated query ID (from request) */
        uint16_t id() const;

        /*! query or response */
        bool qr() const;

        /*! Query type */
        enum class OPCODE {
            QUERY,
            IQUERY,
            STATUS,
            NOTIFY = 4,
            RESERVED_
        };

        /*! Query type */
        OPCODE opcode() const;

        /*! Authoritive answer flag */
        bool aa() const;

        /*! Truncation flag */
        bool tc() const;

        /*! Recursion desired flag (from request) */
        bool rd() const;

        /*! Recursion available (currently not!) */
        bool ra() const;

        /*! Reserved - must be zero */
        bool z() const;

        enum class RCODE {
            OK,
            FORMAT_ERROR,
            SERVER_FAILURE,
            NAME_ERROR,
            NOT_IMPLEMENTED,
            REFUSED,
            BADVERS = 16
        };

        /*! Reply code from server */
        RCODE rcode() const;

        /*! Number of questions */
        uint16_t qdcount() const;

        /*! Number of answers */
        uint16_t ancount() const;

        /*! Number of name servers */
        uint16_t nscount() const;

        /*! Number of additional records */
        uint16_t arcount() const;

        bool validate() const;

        std::string toString() const;

    private:
        const span_t span_;
    };

    Message() = default;
    Message(Message &&) = default;

    /*! Construct from an existing buffer.
     *
     *  This may be a binary buffer received from the Internet, so we have to
     *  parse and validate it carefully.
     */
    Message(span_t span);

    Header header() const;

    RrList& getQuestions() const {
        return getRrSet(0);
    }

    RrList& getAnswers() const {
        return getRrSet(1);
    }

    RrList& getAuthority() const {
        return getRrSet(2);
    }

    RrList& getAdditional() const {
        return getRrSet(3);
    }

    RrList& getRrSet(size_t index) const {
        auto& rrs = rrsets_.at(index);
        if (!rrs) {
            rrs.emplace(span_t{}, 0, 0, index == 0);
        }
        return *rrs;
    }


    /*! Get the raw data buffer for the message
     *
     *  This is the data in the message, ready to be sent over the wire.
     *
     */
    const span_t& span() const;

    bool empty() const noexcept {
        return span_.empty();
    }
    
    std::optional<RrSoa> getSoa() const;

    std::string toString() const;

protected:
    void createIndex();
    // The data-storage for a complete message.
    span_t span_;
    mutable std::array<std::optional<RrList>, 4> rrsets_;
};


/*! Means to build a new message
 *
 */
class MessageBuilder : public Message {
public:
    using buffer_t = std::vector<char>;
    /*! Allocates space in the buffer for the header
     *
     *  \returns Mutable header where some properties can be updated.
     */

    // The segments of a message
    enum class Segment {
        QUESTION,
        ANSWER,
        AUTHORITY,
        ADDITIONAL
    };

    struct OptValues {
        uint16_t bufferSize = 0;
        uint16_t version = 0;
    };

    class NewHeader {
    public:
        NewHeader(buffer_t& b)
            : mutable_buffer_{&b} {}

        NewHeader(NewHeader&) = default;
        NewHeader(NewHeader&&) = default;

        NewHeader& operator = (NewHeader&) = default;
        NewHeader& operator = (NewHeader&&) = default;

        void incQdcount();
        void incAncount();
        void incNscount();
        void incArcount();
        void increment(Segment segment);

        void setAa(bool flag);
        void setTc(bool flag);
        void setRa(bool flag);
        void setRcode(uint8_t rcode);
        void setRcode(Header::RCODE rcode);
        void setOpcode(Header::OPCODE code);


    private:
        buffer_t *mutable_buffer_;
    };

    class NewOpt {
        NewOpt(buffer_t& b, uint16_t offset)
                    : mutable_buffer_{&b}, offset_{offset} {}

        // Set the high 12 bits of the rcode
        void setRcode(uint16_t partial);

        void setVersion(uint16_t version);
        void setMaxBufLen(uint16_t buflen);

    private:
        buffer_t *mutable_buffer_ = {};
        uint16_t offset_ = {};
    };

    MessageBuilder() = default;

    NewHeader createHeader(uint16_t id, bool qr, Header::OPCODE opcode, bool rd);

    NewHeader getMutableHeader() {
        return {buffer_};
    }

    /*! Add a rr to the buffer.
     *
     *  Labels are re-aligned to the new buffer and compressed if applicable.
     *  The ttl is set to the ttl in the first entry in a rrset;
     *
     *  Sets the truncate flag in the headfer if the buffer was too small to add the entry.
     *
     *  \param rr Resource Record to add.
     *  \param hdr Mutable Header for the Message.
     *  \param segment The segment the rr belogns in.
     *
     * \return true if the RR was added. False if the available buffer-space was exeeded.
     */
    bool addRr(const Rr& rr, NewHeader& hdr, Segment segment);

    bool addQuestion(std::string_view fqdn, uint16_t type);

    /*! Add and enable OPT in the reply.
     *
     * The actual RrOpt will be added by finish().
     */
    void addOpt(uint16_t maxBufferSize, uint16_t version = 0);

    void setMaxBufferSize(uint32_t limit) {
        maxBufferSize_ = limit;
        buffer_.reserve(limit);
    }

    void setRcode(uint16_t rcode);
    void setRcode(Header::RCODE rcode);

    void finish();

    size_t size() const noexcept {
        return buffer_.size();
    }

    size_t maxBufferSize() const {
        return maxBufferSize_;
    }

    // Can be used duing construction to check if a RR already exists
    bool exists(const Rr& rr, Segment segment = Segment::ANSWER) const;

protected:
    void increaseBuffer(size_t bytes) {
        if (bytes) {
            buffer_.resize(buffer_.size() + bytes);
        }
        span_ = buffer_;
    }
    void handleOpt();

    buffer_t buffer_;
    size_t maxBufferSize_ = 0; // Not enforced if zero
    std::deque<Labels> labels_; // Labels in the buffer. For compression.
    uint16_t rcode_ = 0;
    std::optional<OptValues> opt_;
};


struct StorageTypes {

#pragma pack(push, 1)
    struct Flags {
        uint8_t soa: 1;
        uint8_t ns: 1;
        uint8_t a: 1;
        uint8_t aaaa: 1;
        uint8_t cname: 1;
        uint8_t txt: 1;
        uint8_t reserved: 1;
        uint8_t tenantId: 1; // Has tenant id
    };

    struct Index {
        uint16_t type = 0;
        uint16_t offset = 0;
    };

    struct Header {
        uint8_t version = CURRENT_STORAGE_VERSION;
        Flags flags = {};
        uint16_t rrcount = 0;
        uint8_t labelsize = 0;
        uint8_t zonelen = 0;
        uint16_t ixoffset = 0;
    };

#pragma pack(pop)
};

/*! Wrapper over a storage buffer
 *
 *
 * The instance don't own it's buffer
 */
class Entry : public StorageTypes {
public:
    static constexpr size_t tenantIdLen = 16;

    using span_t = boost::span<const char>;
    using index_t = boost::span<const Index>;

    /*! Simple forward iterator to allow us to iterate over the rr's */
    class Iterator {
    public:
        using iterator_category = std::forward_iterator_tag;
        using difference_type   = std::ptrdiff_t; // best nonsense choise?
        using value_type        = Rr;
        using pointer           = const value_type*;
        using reference         = const value_type&;


        Iterator(const Entry& entry, bool begin);

        Iterator(const Iterator& it) = default;

        Iterator& operator = (const Iterator& it) = default;

        reference operator*() const { return crr_; }

        pointer operator->() const { return &crr_; }

        Iterator& operator++();

        Iterator operator++(int);

        friend bool operator== (const Iterator& a, const Iterator& b) {
            return a.ix_ == b.ix_;
        }

        friend bool operator!= (const Iterator& a, const Iterator& b) {
            return a.ix_ != b.ix_;
        }

    private:
        //static bool equals(const boost::span<const char> a, const boost::span<const char> b);
        void update();
        void increment();

        const Entry *entry_ = {};
        index_t::const_iterator ix_;
        Rr crr_;
    };

    Entry() = default;
    Entry(const Entry&) = default;
    Entry(Entry&&) = default;

    Entry& operator = (const Entry&) = default;
    Entry& operator = (Entry&&) = default;

    Entry(boost::span<const char> buffer);

    inline bool empty() const noexcept{
        return span_.empty();
    }

    std::optional<boost::uuids::uuid> tenantId() const noexcept;

    bool hasTenantId() const noexcept {
        return !empty() && flags().tenantId;
    }

    Flags flags() const noexcept {
        return header().flags;
    }

    inline const Header& header() const noexcept {
        assert(span_.size() >= BUFFER_HEADER_LEN);
        return *reinterpret_cast<const Header *>(span_.data());
    }

    Iterator begin() const {
        return Iterator{*this, true};
    }

    Iterator end() const {
        return Iterator{*this, false};
    }

    size_t count() const noexcept {
        return count_;
    }

    boost::span<const char> buffer() const noexcept {
        return span_;
    }

    const index_t& index() const noexcept {
        return index_;
    }

    bool hasSoa() const noexcept {
        assert(!empty());
        return header().flags.soa;
    }

    RrSoa getSoa() const;

    inline span_t dataSpan() const noexcept {
        if (empty()) {
            return {};
        }

        return span_.subspan(BUFFER_HEADER_LEN + header().flags.tenantId ? tenantIdLen : 0);
    }

private:
    static index_t mkIndex(span_t b, const Header& h, size_t count) {
         const auto p = b.data() + ntohs(h.ixoffset);
         return {reinterpret_cast<const Index *>(p), count};
    }

    // In span_, first comes the header. Then comes the tenant uuid (if the tenantId flag is set in the header).
    // After there follows the rr's
    span_t span_;
    size_t count_ = {};
    index_t index_;
};


/*! Class to build a message in our own storage format.
 *
 *  See \ref binary_storage_format
 */
class StorageBuilder : public StorageTypes {
public:
    using buffer_t = std::vector<char>;

    /*! Non-owning reference to a newly created RR
     *
     * Can be used to get relevant information for
     * sanity checks and information required by unit-tests.
     */
    class NewRr {
    public:
        NewRr(buffer_t& b, uint16_t offset, uint16_t rdataOffset, uint16_t size)
            : buffer_{b}, offset_{offset}, rdataOffset_{rdataOffset}
            , size_{size}  {}

        /// Size of the RR's buffer-space
        size_t size() const noexcept {
            return size_;
        }

        /// Start-offset into the shared buffer used by thes RR
        uint16_t offset() const noexcept {
            return offset_;
        }

        /// Returns a view of the buffer for this RR only
        auto span() const  {
            return boost::span{buffer_.data() + offset_, size()};
        }

        /// Returns a view of the binary RDATA section of the RR
        const auto rdata() const {
            const auto b = span();
            auto len = size_ - rdataOffset_;
            auto rval = boost::span<const char>(b.data() + rdataOffset_, len);
            return rval;
        }

        /*! Returns the Labels for this RR
         *
         *  Note that it will resolve pointers and return
         *  the effective labels, even if those are stored
         *  in another RR's buffer-view.
         */
        const Labels labels() {
            return Labels{buffer_, offset_};
        }

        boost::span<const char> view() const noexcept {
            return {buffer_.data() + offset_, size_};
        }

        RrInfo rrInfo() const noexcept {
            assert(rdataOffset_ > 10);
            const auto llen = static_cast<uint16_t>(rdataOffset_ - 10);
            return {offset_, size_, llen};
        }

    private:
        const uint16_t size_ = {};
        const uint16_t offset_ = {};
        const uint16_t rdataOffset_ = {};
        const buffer_t& buffer_;
    };


    StorageBuilder() {
        prepare();
    }

    /*! Set tenant id. */
    void setTenantId(const boost::uuids::uuid& tid);

    void setTenantId(const std::optional<boost::uuids::uuid> tid) {
        if (auto id = tid) {
            setTenantId(*id);
        }
    }

    /*! Create a SOA record. */
    NewRr createSoa(std::string_view fqdn,
                    uint32_t ttl,
                    std::string_view mname, // primary NS
                    std::string_view rname, // email
                    uint32_t serial,
                    uint32_t refresh,
                    uint32_t retry,
                    uint32_t expire,
                    uint32_t minimum);

    /*! Create a CNAME record. */
    NewRr createCname(std::string_view fqdn,
                      uint32_t ttl,
                      std::string_view cname);

    /*! Create a PTR record. */
    NewRr createPtr(std::string_view fqdn,
                    uint32_t ttl,
                    std::string_view hostname);

    /*! Create a NS record. */
    NewRr createNs(std::string_view fqdn,
                      uint32_t ttl,
                      std::string_view ns);

    /*! Create a MX record. */
    NewRr createMx(std::string_view fqdn,
                   uint32_t ttl,
                   uint16_t priority,
                   std::string_view host);

    /*! Create a AFSDB record. */
    NewRr createAfsdb(std::string_view fqdn,
                   uint32_t ttl,
                   uint16_t subtype,
                   std::string_view host);



    /*! Create a TXT record.
     *
     * \param fqdn Fully Qualified Domanin Name
     * \param ttl Time To Live
     * \param txt Text to add.
     *            The text is treated as binary data by the dns server
     * \param split A text-segment is up to 255 bytes. If more data is
     *            submitted, it must be split into multiple segments of
     *            0 - 255 bytes. If `split` is true, this will be handled
     *            automatically.
     */
    NewRr createTxt(std::string_view fqdn,
                      uint32_t ttl,
                      std::string_view txt, bool split = false);

    /*! Create a HINFO record.
     *
     * \param fqdn Fully Qualified Domanin Name
     * \param ttl Time To Live
     * \param cpu specified CPU type
     * \param os Os type
     */
    NewRr createHinfo(std::string_view fqdn,
                      uint32_t ttl,
                      std::string_view cpu,
                      std::string_view os);

    /*! Create a Rp record.
     *
     * \param fqdn Fully Qualified Domanin Name
     * \param ttl Time To Live
     * \param mbox Mail address for the responsible person (labels)
     * \param txt Pointer to fdqn for text RR with more info (labels)
     */
    NewRr createRp(std::string_view fqdn,
                   uint32_t ttl,
                   std::string_view mbox,
                   std::string_view txt);

    /*! Create a Srv record.
     *
     * \param fqdn Fully Qualified Domanin Name
     * \param ttl Time To Live
     * \param priority The priority of this target host
     * \param weight   A server selection mechanism.
     * \param port     The port on this target host of this service.
     * \param target   The domain name of the target host.
     */
    NewRr createSrv(std::string_view fqdn,
                   uint32_t ttl,
                   uint16_t priority,
                   uint16_t weight,
                   uint16_t port,
                   std::string_view target);

    /*! Create a rdata segment composed of one or more strings/views from a container.
     *
     *  Each item must be 0 - 255 bytes.
     *  Total length must be < TXT_MAX
     */
    template <typename T>
    auto createTxtRdata(std::string_view fqdn,
                     uint32_t ttl,
                     const T& txt,
                     uint16_t type = TYPE_TXT) {
        size_t len = 0;
        for(const auto& segment : txt) {
            if (segment.size() > TXT_SEGMENT_MAX) {
                throw std::runtime_error{"Text segment is too large!"};
            }
            len += segment.size();
            ++len; // size byte
            if (len > TXT_MAX) {
                throw std::runtime_error{"Text entry is too large!"};
            }
        }

        std::vector<char> rdata;
        rdata.resize(len);

        auto p = rdata.begin();
        for(const auto& segment : txt) {
            // Set length
            *p = static_cast<char>(segment.size());

            // Copy text
            p = std::copy(segment.begin(), segment.end(), ++p);
        }

        return createRr(fqdn, type, ttl, rdata);
    }


    /*! Create the appropriate A or AAAA record from boost::asio::ip::address, v4 or v6
     *
     *  (This may be a little too clever to my taste...)
     */
    template <typename T>
    NewRr createA(std::string_view fqdn, uint32_t ttl, T ip) {
        if constexpr (std::is_same_v<T, boost::asio::ip::address>) {
            if (ip.is_v4()) {
                return createA(fqdn, ttl, ip.to_v4());
            } else if (ip.is_v6()) {
                return createA(fqdn, ttl, ip.to_v6());
            } else {
                throw std::runtime_error{"createRrA: unsupported boost::asio::ip::address type"};
            }
        } else {
            const auto bytes = ip.to_bytes();
            auto b = boost::span(reinterpret_cast<const char *>(bytes.data()), bytes.size());
            if constexpr (std::is_same_v<T, boost::asio::ip::address_v4>) {
                assert(b.size() == 4);
                return createRr(fqdn, TYPE_A, ttl, b);
            }
            else if constexpr (std::is_same_v<T, boost::asio::ip::address_v6>) {
                assert(b.size() == 16);
                return createRr(fqdn, TYPE_AAAA, ttl, b);
            } else {
                // TODO: How to get the compiler to bail out here?
                throw std::runtime_error{"createRrA: unsupported IP type"};
            }
        }
    }

    NewRr createA(std::string_view fqdn, uint32_t ttl, std::string_view ip);

    NewRr createA(std::string_view fqdn, uint32_t ttl, const std::string& ip);

    /*! General method to construct RR's that contain binary data in the rdata section.
     *
     */
    NewRr createBase64(std::string_view fqdn, uint16_t type, uint32_t ttl, std::string_view base64EncodedBlob);

    /*! Create a new resource record
     *
     *  The resource records in a storage buffer is a RrSet, which means
     *  that they all refer to the same name (fqdn), and have the same ttl.
     *  For that reason, those two arguments are only relevant when adding
     *  the first RR.
     *
     *  \param fqdn Fully qualified domain name (not ending with .)
     *              This is ignored for all but the first record.
     *  \param type The type of the record
     *  \param ttl time to live in seconds. This is ignored for all but the first record.
     *  \param rdata The data to store. The data is in binary format, ready to be send in
     *         DNS answers.
     *  \param isOneEntity The fdqn for the Rr is the same as the other Rr's
     *
     *  \return An NewRr object referencing the new record.
     *  \exception std::runtime_error and other exceptions thrown by the C++ standard library and asio.
     */
    NewRr createRr(span_t fqdn, uint16_t type, uint32_t ttl, span_t rdata,
                   bool isOneEntity = true);

    /*! Create a new resource record
     *
     *  Same as above, except that nameOffset replaces the fqdn.
     *
     *  \param nameOffset The offset into the storages buffer to the
     *         NAME (labels) for the RrSet.
     *  \param type The type of the record
     *  \param ttl time to live in seconds. This is ignored for all but the first record.
     *  \param rdata The data to store. The data is in binary format, ready to be send in
     *         DNS answers.
     *
     *  \return An NewRr object referencing the new record.
     *  \exception std::runtime_error and other exceptions thrown by the C++ standard library and asio.
     *
     */
    NewRr createRr(uint16_t nameOffset, uint16_t type, uint32_t ttl, boost::span<const char> rdata);

    /*! Add a copy of an existing rr */
    NewRr addRr(const Rr& rr);

    /*! In place replace of the soa
     *
     *  The labels are not touched. Only the fixed size
     *  data after the labels are copied.
     *
     *  \param soa New soa.
     */
    void replaceSoa(const RrSoa& soa);

    /*! Get the raw data buffer for the message
     *
     *  This is the data in the message, ready to be sent over the wire or written to a database.
     *
     */
    const buffer_t& buffer() const noexcept {
        return buffer_;
    }

    /*! Call when all rr's are added.
     */
    void finish();

    size_t size() const {
        return buffer_.size();
    }

    size_t rrCount() const {
        return index_.size();
    }

    Header header() const;

    void setZoneLen(size_t len);

    /*! Convenience method to increment the soa version from the version in the original soa */
    uint32_t incrementSoaVersion(const Entry& entry);

    auto && stealBuffer() {
        return std::move(buffer_);
    }

    /*! Get the soa record if it exist */
    std::optional<RrSoa> soa() const {
        if (soa_offset_) {
            return RrSoa{buffer_, soa_offset_};
        }
        return {};
    }

    Labels defaultLabels() const {
        if (name_ptr_) {
            return {buffer_, name_ptr_};
        }
        return {};
    }

    void doSort(bool sort) {
        sort_ = sort;
    }

    void oneSoa(bool value) {
        one_soa_ = value;
    }

    // Can be used to check if a RR exists before the buffer is finished
    bool exists(const Rr& rr);

private:
    NewRr createDomainNameInRdata(std::string_view fqdn,
                                  uint16_t type,
                                  uint32_t ttl,
                                  std::string_view dname);

    NewRr finishRr(uint16_t startOffset, uint16_t labelLen, uint16_t type, uint32_t ttl, boost::span<const char> rdata);
    size_t calculateLen(uint16_t labelsLen, size_t rdataLen) const ;
    void prepare();
    void adding(uint16_t startOffset,  uint16_t type);
    NewRr createInt16AndLabels(std::string_view fqdn,
                   uint16_t type,
                   uint32_t ttl,
                   uint16_t val,
                   std::string_view label);

    buffer_t buffer_;
    uint16_t name_ptr_ = 0;
    uint16_t label_len_ = 0;
    Flags flags_ = {};
    std::deque<Index> index_;
    uint16_t index_offset_ = 0;
    uint8_t zonelen_ = 0;
    uint16_t soa_offset_ = 0;
    bool finished_ = false;
    bool sort_ = true;
    bool one_soa_ = true;
    std::optional<boost::uuids::uuid> tenantId_;
};

} // ns

std::ostream& operator << (std::ostream& o, const nsblast::lib::Message::Header::OPCODE& opcode);
std::ostream& operator << (std::ostream& o, const nsblast::lib::Message::Header::RCODE& rcode);

