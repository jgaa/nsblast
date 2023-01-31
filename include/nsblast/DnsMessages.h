#pragma once

#include <limits>
#include <iterator>
#include <cassert>
#include <deque>
#include <boost/core/span.hpp>
#include <boost/asio.hpp>
#include "nsblast/nsblast.h"


namespace nsblast::lib {

class RrSet;

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

    RrSet getQuestions() const;
    RrSet getAnswers() const;


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

private:
    /*! Parse the buffer.
     *
     *  \throws std::runtime_error on buffer-validation errors.
     */
    void parse(boost::span<const char> buffer, size_t startOffset);

    size_t count_ = {}; // Number of labels
    size_t size_ = {}; // Number of bytes for the fqdn (stringlen, including trailing dot)
    uint16_t bytes_ = {}; // Bumber of bytes this label occupies in the buffer
    uint16_t offset_ = {}; // Offset to the start of the buffer
    boost::span<const char> buffer_view_; // A span over the full buffer
};

/*! Representation for a Resource Record.
 *
 *  The rr does not own it's buffer.
 *
 */
class Rr {
public:
    using buffer_t = boost::span<const char>;

    Rr() = default;
    Rr(const Rr&) = default;
    Rr(buffer_t bufferView, uint32_t offset)
        : buffer_view_{bufferView}, offset_{offset} {
        parse();
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

protected:
    void parse();

    mutable std::optional<Labels> labels_;
    buffer_t buffer_view_;
    uint32_t offset_ = 0;
    uint32_t offset_to_type_ = 0;
    buffer_t self_view_;
};

/*! Wrapper over a RR SOA instance.
 *
 *  Can be used to simply obtain data from the record.
 */
class RrSoa : public Rr {
public:
    RrSoa(buffer_t bufferView, uint32_t offset)
        : Rr(bufferView, offset) {}

    Labels mname();
    Labels rname();
    uint32_t serial() const;
    uint32_t refresh() const;
    uint32_t retry() const;
    uint32_t expire() const;
    uint32_t minimum() const;
};

/*! Wrapper / view over a RrSet
 *
 *  The object does not own it's buffer
 */
class RrSet {
public:
    using buffer_t = boost::span<const char>;

#pragma pack(1)
    struct Index {
        Index(uint16_t type, uint16_t offset)
            : type{type}, offset{offset} {}
        Index(const Index&) = default;
        Index(Index&&) = default;

        Index& operator = (const Index&) = default;

        uint16_t type;
        uint16_t offset;
    };
#pragma pack(0)
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
        Iterator(boost::span<const char> buffer, uint16_t offset, const index_t& index);

        Iterator(const Iterator& it) = default;

        Iterator& operator = (const Iterator& it);

        reference operator*() const { return crr_; }

        pointer operator->() { return &crr_; }

        Iterator& operator++();

        Iterator operator++(int);

        friend bool operator== (const Iterator& a, const Iterator& b) {
            return equals(a.buffer_, b.buffer_) && a.current_ == b.current_;
        }

        friend bool operator!= (const Iterator& a, const Iterator& b) {
            return !equals(a.buffer_, b.buffer_) || a.current_ != b.current_;
        }

    private:
        static bool equals(const boost::span<const char> a, const boost::span<const char> b);
        void update();
        void increment();

        const index_t& index_;
        boost::span<const char> buffer_;
        index_t::const_iterator current_;
        Rr crr_;
    };

    RrSet(buffer_t bufferView, uint16_t offset, uint16_t count);

    size_t count() const {
        return count_;
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
};

/*! Means to build a new message
 *
 */
class MessageBuilder : public Message {
public:
    /*! Allocates space in the buffer for the header
     *
     *  \returns Mutable header where some properties can be updated.
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


/*! Class to build a message in our own storage format.
 *
 *  See \ref binary_storage_format
 */
class StorageBuilder {
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

    private:
        const uint16_t size_ = {};
        const uint16_t offset_ = {};
        const uint16_t rdataOffset_ = {};
        const buffer_t& buffer_;
    };


    StorageBuilder() {
        prepare();
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

    /*! Create the appropriate A or AAAA record from boost::asio::ip::address, v4 or v6
     *
     *  (This may be a little too clever to my taste...)
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

