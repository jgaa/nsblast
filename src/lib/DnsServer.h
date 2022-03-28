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

namespace nsblast::lib {

class DnsServer;

namespace ns {

class Endpoint;

class Request {
public:
    Request(Endpoint& parent)
        : parent_{parent} {}

    auto queryBuffer() noexcept {
        boost::asio::mutable_buffer b{
            query_buffer_.data(), query_buffer_.size()};

        return b;
    }

    auto& sep() {
        return sender_endpoint_;
    }

    static constexpr size_t max_query_buffer = 512;
private:
    std::array<char, max_query_buffer> query_buffer_ = {};
    boost::asio::ip::udp::endpoint sender_endpoint_;
    Endpoint& parent_;
};


class Endpoint {
public:
    Endpoint(DnsServer& parent, boost::asio::ip::udp::endpoint ep);

    void next();

private:
    void process(Request& req, size_t len);

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

