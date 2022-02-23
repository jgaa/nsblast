#pragma once

#include <map>
#include <functional>
#include <filesystem>
#include <string_view>

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

class RequestHandler {
public:
    virtual ~RequestHandler() = default;

    virtual Response onReqest(const Request& req) = 0;
};

// Very general HTTP server so we can easily swap it out with something better later...
class HttpServer
{
public:
    using handler_t = std::shared_ptr<RequestHandler>;//std::function<Response (const Request& req)>;

    HttpServer(const Config& config);

    void start();
    void stop();

    void addRoute(std::string_view target, handler_t handler);

    std::pair<bool, std::string_view /* user name */> Authenticate(const std::string_view& authHeader);

    // Called by the HTTP server implementation template
    Response onRequest(const Request& req) noexcept;

    // Serve a directory.
    // handles `index.html` by default. Lists the directory if there is no index.html.
    class FileHandler : public RequestHandler {
    public:
        FileHandler(std::filesystem::path root);

        Response onReqest(const Request &req) override;

        std::filesystem::path resolve(std::string_view target);
    private:
        Response readFile(const std::filesystem::path& path);
        Response handleDir(const std::filesystem::path& path);
        Response listDir(const std::filesystem::path& path);
        std::string getMimeType(const std::filesystem::path& path);

        const std::filesystem::path root_;
    };

private:
    void startWorkers();

    const Config& config_;
    std::map<std::string, handler_t> routes_;
    boost::asio::io_context ctx_;
    std::vector<std::thread> workers_;
};

}
