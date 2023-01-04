#pragma once

#include <memory>
#include <string_view>
#include <stdexcept>

#include <numeric>
//#include <arpa/inet.h>
#include <boost/asio.hpp>

namespace nsblast::lib::ns  {

// See RFC1035 4.1.1 Header section format
class MessageHeader
{
public:
    struct MessageHeaderException : public std::runtime_error {
        MessageHeaderException(const std::string& message) : std::runtime_error(message) {}
    };

    enum class Rcode : uint8_t {
        OK = 0,
        FORMAT_ERROR = 1,
        SERVER_FAILURE = 2,
        NAME_ERROR = 3,
        NOT_IMPLEMENTED = 4,
        REFUSED = 5
    };

    MessageHeader(std::string_view binary)
    {
        if (binary.size() < getSize()) {
            throw MessageHeaderException{"Header-length underflow"};
        }

        const uint16_t *p = reinterpret_cast<const uint16_t *>(binary.data());

        id_ = ntohs(*p); ++p;
        flags_ = ntohs(*p); ++p;
        qdcount_ = ntohs(*p); ++p;
        ancount_ = ntohs(*p); ++p;
        nscount_ = ntohs(*p); ++p;
        arcount_ = ntohs(*p);
    }

    MessageHeader(const MessageHeader& v) = default;

    /*! Get the length of the header */
    size_t getSize() const { return 2 * 6; }

    /*! Get the ID in native byte order */
    uint16_t getId() const { return id_; }

    bool getQr() const { return (flags_ & (static_cast<uint16_t>(1) << 15)) != 0; }
    uint8_t getOpcode() const { return (flags_ >> 11) & 0xF; }
    bool getAa() const { return (flags_ & (1 << 10)) != 0; }
    bool getTc() const { return (flags_ & (1 << 9)) != 0; }
    bool getRd() const { return (flags_ & (1 << 8)) != 0; }
    bool getRa() const { return (flags_ & (1 << 7)) != 0; }
    uint8_t getZ() const { return (flags_ >> 4) & 0xF; }
    uint8_t getRcode() const { return flags_ & 0xF; }
    uint16_t getQdCount() const { return qdcount_; }
    uint16_t getAnCount() const { return ancount_; }
    uint16_t getNsCount() const { return nscount_; }
    uint16_t getArCount() const { return arcount_; }

    void setQr(bool qr) {
        flags_ &= ~(1 << 15);
        if (qr)
            flags_ |= static_cast<uint16_t>(1) << 15;
    }

    void setTc(bool tc) {
        flags_ &= ~(1 << 9);
        if (tc)
            flags_ |= static_cast<uint16_t>(1) << 9;
    }

    void setRa(bool ra) {
        flags_ &= ~(1 << 7);
        if (ra)
            flags_ |= static_cast<uint16_t>(1) << 7;
    }

    void setAa(bool aa) {
        flags_ &= ~(1 << 10);
        if (aa)
            flags_ |= static_cast<uint16_t>(1) << 10;
    }

    void setOpcode(uint8_t opcode) {
        flags_ &= ~(0xF << 11);
        flags_ |= (opcode & 0xF) << 11;
    }

    void setRcode(Rcode opcode) {
        flags_ &= ~0xF;
        flags_ |= (static_cast<uint16_t>(opcode) & 0xF);
    }

    void setQdCount(uint16_t val) { qdcount_ = val; }
    void setAnCount(uint16_t val) { ancount_ = val; }
    void setNsCount(uint16_t val) { nscount_ = val; }
    void setArCount(uint16_t val) { arcount_ = val; }

    void sesetAllCounters() {
        qdcount_ = ancount_ = nscount_ = arcount_ = 0;
    }

    size_t write(void *binary, size_t len) const {
        uint16_t *p = static_cast<uint16_t *>(binary);

        if (len < getSize()) {
            throw MessageHeaderException{"out buffer-length underflow"};
        }

        *p = htons(id_); ++p;
        *p = htons(flags_); ++p;
        *p = htons(qdcount_); ++p;
        *p = htons(ancount_); ++p;
        *p = htons(nscount_); ++p;
        *p = htons(arcount_);

        return getSize();
    }

private:
    std::uint16_t id_ = 0;
    std::uint16_t flags_ = 0;
    std::uint16_t qdcount_ = 0;
    std::uint16_t ancount_ = 0;
    std::uint16_t nscount_ = 0;
    std::uint16_t arcount_ = 0;
};


} // namespace

