#pragma once

#include <limits>
#include <iterator>
#include <cassert>
#include <boost/core/span.hpp>
#include <boost/asio.hpp>
#include "nsblast/nsblast.h"


namespace nsblast::lib {


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

    // The buffer-type we present to the world.
    using buffer_t = std::vector<char>;

    class Header {
    public:
        constexpr static size_t SIZE = 12;

        Header(const buffer_t& b)
            : buffer_{b} {}

        /*! Randomly generated query ID (from request) */
        uint16_t id() const;

        /*! query or response */
        bool qr() const;

        /*! Query type */
        enum class OPCODE {
            QUERY,
            IQUERY,
            STATUS,
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
            SERVER_FAILURE,
            NAME_ERROR,
            NOT_IMPLEMENTED,
            REFUSED,
            RESERVED_
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

    private:
        const buffer_t& buffer_;
    };

    const Header header() const;


    /*! Get the raw data buffer for the message
     *
     *  This is the data in the message, ready to be sent over the wire.
     *
     */
    const buffer_t& buffer() const;

protected:
    // The data-buffer for a complete message.
    buffer_t buffer_;
};

/*! Representation of RFC1035 labels
 *
 *  A label is a single node in the name-tree.
 *  It must start with a ASCII letter, and then contain letters or numbers.
 *  '.' can exist as part of the label, but normally it is used in strings
 *  as a delimiter between labels. If '.' is used, it's escaped with '\'
 *  in the text-representation of the label.
 *
 *  The binary representation is a single unsigned octet as a length
 *  field, immediately followed by <length> cxharacters. Unlike, C
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
        using pointer           = const std::string_view*;
        using reference         = const std::string_view&;

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
     *  @param buffer Buffer that covers the message.
     *  @param startOffset The location in the buffer where this
     *     labels data start.
     *
     *  The lables may contain pointers to other labels within
     *  the message, and therefore, we need the boundry for the
     *  messages buffer in order to parse and validate the labels.
     *
     *  The constructor parses and validates the buffer, and
     *  sets it local buffer_view_ to exactely match the
     *  labels from the original buffer.
     *
     *  Note: This object does not own any buffers.
     */
    Labels(boost::span<const char> buffer, size_t startOffset);

    /*! Returns the size of the labels buffer in bytes */
    size_t size() const noexcept;

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

private:
    /*! Parse the buffer.
     *
     *  @throws std::runtime_error on buffer-validation errors.
     */
    void parse(boost::span<const char> buffer, size_t startOffset);

    size_t count_ = {}; // Number of labels
    size_t size_ = {}; // Number of bytes for the fqdn
    boost::span<const char> buffer_view_;
    uint16_t offset_ = {}; // Offset to the start of the buffer
};

/*! Means to build a new message
 *
 */
class MessageBuilder : public Message {
public:
    /*! Allocates space in the buffer for the header
     *
     *  @return Mutable header where some properties can be updated.
     */
    class NewHeader {
    public:
        NewHeader(buffer_t& b)
            : mutable_buffer_{b} {}

        void incQdcount();
        void incAncount();
        void incNscount();
        void incArcount();

        void setTc(bool flag);
        void setRa(bool flag);
        void setRcode(Header::RCODE rcode);

    private:
        buffer_t& mutable_buffer_;
    };

    MessageBuilder() = default;

    NewHeader createHeader(uint16_t id, bool qr, Header::OPCODE opcode, bool rd);
    //NewRr createRr(std::string_view fqdn, uint16_t type, uint32_t ttl, boost::span<const char> rdata);
};

/* Storage format

    The idea is to store the data as Rr's, (almost) ready to be
    sent on the wire in a DNS reply, after they are copied
    to the outgoing message buffer. We have our own header
    to understand the data and to work efficiently with it.

    When we copy data to a DNS reply buffer, we must handle
    the NAME (labels) gracefully (it must be copied at least once,
    and pointers in other records in a RrSet must be updated
    accordingly).

    version     Version of the data frmat
    flags       Flags to quickly check if a popular type of RR's are present
    labelsize   Size of the labels buffer (in the first RR)
    zonelen     Offset to the start of labels that identifies the zone
    rrcount     Number of RR's in the RRSet

    0                   1
    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    | version       | flags         |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    | rrcount                       |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    | labelsize     | zonelen       |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |                               |
    /            index              /
    |                               |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |                               |
    / RRset entries                 /
    |                               |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+


    The index is sorted so that rr's with the same type are clustered
    and most popular types (in lookups) are first

    Index format
    0                   1
    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    | Type                          |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    + Offset                        +
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

    RR format (from RFC 1035)
    The first entry has a NAME. All other entries
    have just a pointer to the first entries NAME.

                                    1  1  1  1  1  1
      0  1  2  3  4  5  6  7  8  9  0  1  2  3  4  5
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |                                               |
    /                                               /
    /                      NAME                     /
    |                                               |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |                      TYPE                     |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |                     CLASS                     |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |                      TTL                      |
    |                                               |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |                   RDLENGTH                    |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--|
    /                     RDATA                     /
    /                                               /
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

*/

/*! Class to build a message in our storage format.
 *
 */
class StorageBuilder {
public:
    using buffer_t = std::vector<char>;

    class NewRr {
    public:
        NewRr(buffer_t& b, uint16_t offset, uint16_t rdataOffset, uint16_t size)
            : buffer_{b}, offset_{offset}, rdataOffset_{rdataOffset}
            , size_{size}  {}

        size_t size() const noexcept {
            return size_;
        }

        uint16_t offset() const noexcept {
            return offset_;
        }

        // Return a view of the buffer for this RR only
        auto span() const  {
            return boost::span{buffer_.data() + offset_, size()};
        }

        const auto rdata() const {
            const auto b = span();
            auto len = size_ - rdataOffset_;
            auto rval = boost::span<const char>(b.data() + rdataOffset_, len);
            return rval;
        }

        const Labels labels() {
            return Labels{buffer_, offset_};
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

    /*! Create the appropriate A or AAAA record from boost::asio::ip::address, v4 or v6
     *
     *  (This may be a little too "smart" to my taste...)
     */
    template <typename T>
    NewRr createRrA(std::string_view fqdn, uint32_t ttl, T ip) {
        if constexpr (std::is_same_v<T, boost::asio::ip::address>) {
            if (ip.is_v4()) {
                return createRrA(fqdn, ttl, ip.to_v4());
            } else if (ip.is_v6()) {
                return createRrA(fqdn, ttl, ip.to_v6());
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
     *
     *  \return An NewRr object referencing the new record.
     *  \exception std::runtime_error and other exceptions thrown by the C++ standard library and asio.
     */
    NewRr createRr(std::string_view fqdn, uint16_t type, uint32_t ttl, boost::span<const char> rdata);

    /*! Create a new resource record
     *
     *  Same as above, except that nameOffset replaces the fqdn.
     *
     *  \param nameOffset The offset into the storages buffer to the
     *         NAME (labels) for the RrSet.
     *
     */
    NewRr createRr(uint16_t nameOffset, uint16_t type, uint32_t ttl, boost::span<const char> rdata);

    /*! Get the raw data buffer for the message
     *
     *  This is the data in the message, ready to be sent over the wire or written to a database.
     *
     */
    const buffer_t& buffer() const noexcept {
        return buffer_;
    }


private:
    NewRr finishRr(uint16_t startOffset, uint16_t labelLen, uint16_t type, uint32_t ttl, boost::span<const char> rdata);
    size_t calculateLen(uint16_t labelsLen, size_t rdataLen) const ;
    void prepare();
    buffer_t buffer_;
    size_t num_rr_ = 0;
    uint16_t name_ptr_ = 0;
};

} // ns

