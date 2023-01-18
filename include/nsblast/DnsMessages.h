#pragma once

#include <boost/core/span.hpp>
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
    class Iterator {
    public:
        Iterator(const Labels&);
    }


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

private:
    /*! Parse the buffer.
     *
     *  @throws std::runtime_error on buffer-validation errors.
     */
    void parse(boost::span<const char> buffer, size_t startOffset);

    size_t count_ = {}; // Number of labels
    size_t size_ = {}; // Number of bytes for the fqdn
    boost::span<const char> buffer_view_;
};

/*! Means to build a new message */
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


};

} // ns

