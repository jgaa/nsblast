#pragma once

#include <array>
#include <map>
#include <deque>
#include <functional>
#include <filesystem>
#include <string_view>
#include <future>

#include <boost/asio.hpp>
#include <boost/asio/buffer.hpp>

#include "nsblast/nsblast.h"
#include "MessageHeader.h"

namespace nsblast::lib {

class DnsServer;

namespace ns {

struct InvalidQuery : public std::runtime_error {
    InvalidQuery(const std::string& message) : std::runtime_error (message) {};
};

struct Refused : public std::runtime_error {
    Refused(const std::string& message) : std::runtime_error (message) {};
};

struct UnimplementedQuery : public std::runtime_error {
    UnimplementedQuery(const std::string& message) : std::runtime_error (message) {};
};

struct UnknownDomain : public std::runtime_error {
    UnknownDomain(const std::string& message) : std::runtime_error (message) {};
};

struct UnknownSubDomain : public std::runtime_error {
    UnknownSubDomain(const std::string& message) : std::runtime_error (message) {};
};

struct NoSoaRecord : public std::runtime_error {
    NoSoaRecord(const std::string& message) : std::runtime_error (message) {};
};

struct Truncated : public std::runtime_error {
    Truncated(const std::string& message) : std::runtime_error (message) {};
};

class Endpoint;

class Request {
public:
    Request(Endpoint& parent)
        : parent_{parent} {}

    template <typename T = boost::asio::mutable_buffer>
    auto queryBuffer() noexcept {
        T b{query_buffer_.data(), query_buffer_.size()};
        return b;
    }

    template <typename T = boost::asio::mutable_buffer>
    auto replyBuffer() noexcept {
        assert(reply_size_ <= reply_buffer_.size());
        T b{reply_buffer_.data(), reply_size_};
        return b;
    }

    auto replyBufferCapacity() {
        return reply_buffer_.size();
    }

    void setReplySize(size_t bytes) {
        if (reply_size_ > reply_buffer_.size()) {
            throw std::runtime_error{"Overflow in reply_buffer_"};
        }
        reply_size_ = bytes;
    }

    auto& sep() {
        return sender_endpoint_;
    }

    auto logName() const noexcept {
        std::ostringstream out;

        out << "Request #" << req_id_ << " from " << sender_endpoint_;
        return out.str();
    }

    static constexpr size_t max_query_buffer = 512;
    static constexpr size_t max_reply_buffer_ = 512;
private:
    std::array<char, max_query_buffer> query_buffer_ = {};
    std::array<char, max_reply_buffer_> reply_buffer_ = {};
    size_t reply_size_ = 0;
    boost::asio::ip::udp::endpoint sender_endpoint_;
    const uint64_t req_id_ = ++next_id_;
    Endpoint& parent_;
    static std::atomic_uint64_t next_id_;
};


class Endpoint {
public:
    Endpoint(DnsServer& parent, boost::asio::ip::udp::endpoint ep);

    void next();

private:
    void process(Request& req, size_t len);
    void processQuestions(Request& req, const MessageHeader& header);
    void createErrorReply(MessageHeader::Rcode errCode,
                          const MessageHeader& hdr,
                          Request& req);

    boost::asio::ip::udp::socket socket_;
    DnsServer& parent_;
};

} // ns

class DnsServer
{
public:
    using endpoints_t = std::vector<std::shared_ptr<ns::Endpoint>>;

    DnsServer(const Config& config);

    std::future<void> start();
    void stop();

    auto& ctx() {
        return ctx_;
    }

private:
    void startWorkers();

    const Config& config_;
    boost::asio::io_context ctx_;
    std::vector<std::thread> workers_;
    std::promise<void> promise_;
    endpoints_t endpoints_;

    //static constexpr size_t max_query_buffer_ = 512;
    static constexpr size_t max_reply_buffer_ = 512;
};

} // ns

