#pragma once

#include <map>
#include <functional>
#include <filesystem>
#include <string_view>
#include <future>

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
    std::string target; // The actual target
    std::string_view mime_type;
    std::string_view mimeType() const;
    bool close = false;
};

class RequestHandler {
public:
    virtual ~RequestHandler() = default;

    virtual Response onReqest(const Request& req) = 0;
};

template <typename T>
class EmbeddedHandler : public RequestHandler {
public:
    EmbeddedHandler(const T& content, std::string prefix)
        : content_{content}, prefix_{std::move(prefix)} {}

    Response onReqest(const Request& req) override {
        // Remove prefix
        auto t = std::string_view{req.target};
        if (t.size() < prefix_.size()) {
            throw std::runtime_error{"Invalid targert. Cannot be shorted than prefix!"};
        }

        t = t.substr(prefix_.size());

        while(!t.empty() && t.front() == '/') {
            t = t.substr(1);
        }

        if (t.empty()) {
            t = {"index.html"};
        }

        if (auto it = content_.find(std::string{t}) ; it != content_.end()) {
            std::filesystem::path served = prefix_;
            served /= t;

            return {200, "OK", std::string{it->second}, served.string()};
        }
        return {404, "Document not found"};
    }

private:
    const T& content_;
    const std::string prefix_;
};

// Very general HTTP server so we can easily swap it out with something better later...
class HttpServer
{
public:
    using handler_t = std::shared_ptr<RequestHandler>;//std::function<Response (const Request& req)>;


    HttpServer(const Config& config);

    std::future<void> start();
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
    std::promise<void> promise_;
};

}
