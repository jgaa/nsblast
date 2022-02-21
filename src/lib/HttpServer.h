#pragma once

#include <map>
#include <functional>

#include <boost/asio.hpp>

#include "nsblast/nsblast.h"

namespace nsblast::lib {

struct Request {
    enum class Type {
        GET,
        PUT,
        PATCH,
        POST,
        DELETE
    };

    std::string target;
    std::string auth; // from Authorization header
    std::string body;
    Type type;
};

struct Response {
    int code = 200;
    std::string reason = "OK";
    std::string body;
    bool close = false;
};

// Very general HTTP server so we can easily swap it out with something better later...
class HttpServer
{
public:
    using handler_t = std::function<Response (const Request& req)>;

    HttpServer(const Config& config);

    void start();
    void stop();

    void addRoute(std::string_view target, handler_t handler);

    std::pair<bool, std::string_view /* user name */> Authenticate(const std::string_view& authHeader);

    // Called by the HTTP server implementation template
    Response onRequest(const Request& req) noexcept;

private:
    void startWorkers();

    const Config& config_;
    std::map<std::string, handler_t> routes_;
    boost::asio::io_context ctx_;
    std::vector<std::thread> workers_;
};

}
