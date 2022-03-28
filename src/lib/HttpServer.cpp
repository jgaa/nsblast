
#include <boost/beast/http/string_body.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/version.hpp>

#include "nsblast/logging.h"
#include "HttpServer.h"
#include "swagger_res.h"

using namespace std;
using namespace std;
using boost::asio::ip::tcp;
namespace beast = boost::beast;         // from <boost/beast.hpp>
namespace http = beast::http;           // from <boost/beast/http.hpp>
namespace ssl = boost::asio::ssl;
namespace net = boost::asio;            // from <boost/asio.hpp>
using namespace std::placeholders;
using namespace std::string_literals;

namespace nsblast::lib {

namespace {

struct LogRequest {
    LogRequest() = default;
    LogRequest(const LogRequest& ) = delete;
    LogRequest(LogRequest&& ) = delete;

    boost::asio::ip::tcp::endpoint local, remote;
    string type;
    string location;
    string user;
    int replyValue = 0;
    string replyText;

private:
    std::once_flag done_;

public:

    void set(const http::response<http::string_body>& res) {
        replyValue = res.result_int();
        replyText = res.reason().to_string();
        flush();
    }

    void flush() {
        if (location.empty()) {
            LOG_TRACE << "Called out of order!";
            return;
        }
        call_once(done_, [&] {
            LOG_INFO << remote << " --> " << local << " [" << user << "] " << type << ' ' << location.data() << ' ' << replyValue << ' ' << replyText;
        });
    }

    void cancel() {
        call_once(done_, [&] {
            LOG_TRACE << "Log event cancelled";
        });
    }

    ~LogRequest() {
        flush();
    }
};

auto to_type(const http::verb& verb) {
    switch(verb) {
    case http::verb::get:
        return Request::Type::GET;
    case http::verb::post:
        return Request::Type::POST;
    case http::verb::patch:
        return Request::Type::PATCH;
    case http::verb::put:
        return Request::Type::PUT;
    case http::verb::delete_:
        return Request::Type::DELETE;
    default:
        throw runtime_error{"Unknown verb"};
    }
}

template <bool isTls, typename streamT>
void DoSession(streamT& stream,
               HttpServer& instance,
               boost::asio::yield_context& yield)
{
    LOG_TRACE << "Processing session: " << beast::get_lowest_layer(stream).socket().remote_endpoint()
              << " --> " << beast::get_lowest_layer(stream).socket().local_endpoint();

    bool close = false;
    beast::error_code ec;
    beast::flat_buffer buffer{1024 * 64};

    if constexpr(isTls) {
        beast::get_lowest_layer(stream).expires_after(std::chrono::seconds(5));
        stream.async_handshake(ssl::stream_base::server, yield[ec]);
        if(ec) {
            LOG_ERROR << "TLS handshake failed: " << ec.message();
            return;
        }
    }

    while(!close) {
        LogRequest lr;
        lr.remote =  beast::get_lowest_layer(stream).socket().remote_endpoint();
        lr.local = beast::get_lowest_layer(stream).socket().local_endpoint();

        beast::get_lowest_layer(stream).expires_after(std::chrono::seconds(30));
        http::request<http::string_body> req;
        http::async_read(stream, buffer, req, yield[ec]);
        if(ec == http::error::end_of_stream)
            break;
        if(ec) {
            LOG_ERROR << "read failed: " << ec.message();
            lr.cancel();
            return;
        }

        if (!req.keep_alive()) {
            close = true;
        }

        lr.location = req.base().target().to_string();
        lr.type = req.base().method_string().to_string();

        bool authorized = false;
        std::string auth_value;
        if (auto it = req.base().find(http::field::authorization) ; it != req.base().end()) {
            auto [a, u] = instance.Authenticate({it->value().data(), it->value().size()});
            lr.user = auth_value = u;
            authorized = a;
        }

//        if (!authorized) {
//            LOG_TRACE << "Request was unauthorized!";

//            http::response<http::string_body> res;
//            res.body() = "Access denied";
//            res.result(401);
//            res.base().set(http::field::server, "nsblast "s + NSBLAST_VERSION);
//            res.base().set(http::field::connection, close ? "close" : "keep-alive");
//            res.prepare_payload();
//            lr.set(res);

//            http::async_write(stream, res, yield[ec]);
//            if(ec) {
//                LOG_ERROR << "write failed: " << ec.message();
//                break;
//            }
//        }

//        if (!req.body().empty()) {
//            if (auto it = req.base().find(http::field::content_type) ; it != req.base().end()) {
//                // TODO: Check that the type is json
//                LOG_TRACE << "Request has content type: " << it->value();
//            }
//        }

        // TODO: Check that the client accepts our json reply
        Request request {
                req.base().target().to_string(),
                {},
                "auth",
                req.body(),
                to_type(req.base().method())
        };

        const auto reply = instance.onRequest(request);
        if (reply.close) {
            close = true;
        }

        http::response<http::string_body> res;
        res.body() = reply.body;
        res.result(reply.code);
        res.base().set(http::field::server, "nsblast "s + NSBLAST_VERSION);

        if (auto mime = reply.mimeType(); !mime.empty()) {
            res.base().set(http::field::content_type, {mime.data(), mime.size()});
        }
        res.base().set(http::field::connection, close ? "close" : "keep-alive");
        res.prepare_payload();

        lr.set(res);
        http::async_write(stream, res, yield[ec]);
        if(ec) {
            LOG_WARN << "write failed: " << ec.message();
            return;
        }
    }

    if constexpr(isTls) {
        beast::get_lowest_layer(stream).expires_after(std::chrono::seconds(30));

        // Perform the SSL shutdown
        stream.async_shutdown(yield[ec]);
        if (ec) {
            LOG_TRACE << "TLS shutdown failed: " << ec.message();
        }
    } else {
        // Send a TCP shutdown
        stream.socket().shutdown(tcp::socket::shutdown_send, ec);
    }
}

} // ns

HttpServer::HttpServer(const Config &config)
    : config_{config}
{

}

std::future<void> HttpServer::start()
{

    // Start listening
    tcp::resolver resolver(ctx_);

    const bool is_tls = !config_.http_tls_key.empty();

    auto port = config_.http_port;
    if (port.empty()) {
        if (is_tls) {
            port = "https";
        } else {
            port = "http";
        }
    }

    LOG_DEBUG << "Preparing to listen to: "
              << config_.http_endpoint << " on "
              << (is_tls ? "HTTPS" : "HTTP")
              << " port " << port;

    auto endpoint = resolver.resolve({config_.http_endpoint, port});
    tcp::resolver::iterator end;
    for(; endpoint != end; ++endpoint) {
        tcp::endpoint ep = endpoint->endpoint();
        LOG_INFO << "Starting " << (is_tls ? "HTTPS" : "HTTP") << " endpoint: " << ep;

        boost::asio::spawn(ctx_, [this, ep, is_tls] (boost::asio::yield_context yield) {
            beast::error_code ec;

            tcp::acceptor acceptor{ctx_};
            acceptor.open(ep.protocol(), ec);
            if(ec) {
                LOG_ERROR << "Failed to open endpoint " << ep << ": " << ec;
                return;
            }

            acceptor.set_option(net::socket_base::reuse_address(true), ec);
            if(ec) {
                LOG_ERROR << "Failed to set option reuse_address on " << ep << ": " << ec;
                return;
            }

            auto sslCtx = make_shared<ssl::context>(ssl::context::tls_server);
            if (is_tls) {
                try {
                    sslCtx->use_certificate_chain_file(config_.http_tls_cert);
                    sslCtx->use_private_key_file(config_.http_tls_key, ssl::context::pem);
                } catch(const exception& ex) {
                    LOG_ERROR << "Failed to initialize tls context: " << ex.what();
                    return;
                }
            }

            // Bind to the server address
            acceptor.bind(ep, ec);
            if(ec) {
                LOG_ERROR << "Failed to bind to " << ep << ": " << ec;
                return;
            }

            // Start listening for connections
            acceptor.listen(net::socket_base::max_listen_connections, ec);
            if(ec) {
                LOG_ERROR << "Failed to listen to on " << ep << ": " << ec;
                return;
            }

            size_t errorCnt = 0;
            const size_t maxErrors = 64;
            for(;!ctx_.stopped() && errorCnt < maxErrors;) {
                tcp::socket socket{ctx_};
                acceptor.async_accept(socket, yield[ec]);
                if(ec) {
                    // I'm unsure about how to deal with errors here.
                    // For now, allow `maxErrors` to occur before giving up
                    LOG_WARN << "Failed to accept on " << ep << ": " << ec;
                    ++errorCnt;
                    continue;
                }

                errorCnt = 0;

                if (is_tls) {
                    boost::asio::spawn(acceptor.get_executor(), [this, sslCtx, socket=move(socket)](boost::asio::yield_context yield) mutable {
                        beast::ssl_stream<beast::tcp_stream> stream{std::move(socket), *sslCtx};
                        try {
                            DoSession<true>(stream, *this,yield);
                        } catch(const exception& ex) {
                            LOG_ERROR << "Caught exception from DoSession [HTTPS]: " << ex.what();
                        }
                    });

                } else {
                    boost::asio::spawn(acceptor.get_executor(), [this, socket=move(socket)](boost::asio::yield_context yield) mutable {
                        beast::tcp_stream stream{move(socket)};
                        try {
                            DoSession<false>(stream, *this, yield);
                        } catch(const exception& ex) {
                            LOG_ERROR << "Caught exception from DoSession [HTTP]: " << ex.what();
                        }
                    });
                }
            }

        });
    }; // for resolver endpoint


    startWorkers();
    return promise_.get_future();
}

void HttpServer::stop()
{
    ctx_.stop();
    for(auto& worker : workers_) {
        worker.join();
    }
    promise_.set_value();
}

void HttpServer::addRoute(std::string_view target, handler_t handler)
{
    if (target.size() == 0) {
        throw runtime_error{"A target's route cannot be empty"};
    }
    string key{target};
    routes_[move(key)] = handler;
}

std::pair<bool, string_view> HttpServer::Authenticate(const std::string_view &authHeader)
{
    static const string_view teste{"teste"};
    return {true, teste};
}

Response HttpServer::onRequest(Request &req) noexcept
{
    // Find the route!
    string_view tw{req.target.data(), req.target.size()};

    RequestHandler *best_handler = {};
    string_view best_route;

    for(const auto& [route, handler] : routes_) {
        const auto len = route.size();

        // Target must be at least the lenght of the route
        if (tw.size() < len) {
            continue;
        }

        // Target is only relevant if it's the same size as the route
        // or if it has a slash at the location where target ends
        if (tw.size() == len || tw.at(len) == '/') {
            auto relevant = tw.substr(0, len);
            if (relevant == route) {
                // We need the longest possible match
                if (!best_handler || (best_route.size() < route.size())) {
                    best_handler = handler.get();
                    best_route = route;
                }
            }
        }
    }

    if (best_handler) {
        try {
            LOG_TRACE << "Found route '" << best_route << "' for target '" << tw << "'";
            req.route = best_route;
            return best_handler->onReqest(req);
        } catch (const exception& ex) {
            LOG_ERROR << "Caught unexpected exception "
                      << typeid(ex).name() << " from request: " << ex.what();
            return {500, "Internal server error"};
        }
    }

    return {404, "Document not found"};
}

void HttpServer::startWorkers()
{
    for(size_t i = 0; i < config_.num_http_threads; ++i) {
        workers_.emplace_back([this, i] {
                LOG_DEBUG << "HTTP worker thread #" << i << " starting up.";
                try {
                    ctx_.run();
                } catch(const exception& ex) {
                    LOG_ERROR << "HTTP worker #" << i
                              << " caught exception: "
                              << ex.what();
                }
                LOG_DEBUG << "HTTP worker thread #" << i << " done.";
        });
    }
}

HttpServer::FileHandler::FileHandler(std::filesystem::path root)
    : root_{root}
{
    LOG_DEBUG << "Ready to serve path: " << root;
}

Response HttpServer::FileHandler::onReqest(const Request &req)
{
    static const Response not_found{404, "Document not found"};
    auto path = resolve(req.target);

    std::error_code ec;
    auto what = filesystem::status(path, ec);
    if (ec) {
        LOG_DEBUG << "Path " << path << ": " << ec.message();
        return not_found;
    }

    if (what.type() == filesystem::file_type::regular) {
        return readFile(path);
    }

    if (what.type() == filesystem::file_type::directory) {
        return handleDir(path);
    }


    return not_found;
}

std::filesystem::path HttpServer::FileHandler::resolve(std::string_view target)
{

    while(!target.empty() && target[0] == '/') {
        target = target.substr(1);
    }

    auto t = filesystem::path{target}.lexically_normal();
    if (!t.empty() &&  t.native().front() == '/') {
        throw runtime_error{"Invalid target. Normalized target cannot start with slash!"};
    }

    auto raw = root_;

    if (!target.empty()) {
        raw /= t;
    }

    auto r = raw.lexically_normal();

    // Remove trailing slash. It sometimes occur...
    if (auto& n = r.native() ; !n.empty()) {
        if (n.back() == '/') {
            r = {r.native().substr(0, r.native().size() -1)};
        }
    }

    // validate
    auto a = root_.begin();
    auto b = r.begin();
    for(; a != root_.end(); ++a, ++b) {
        if (b == r.end()) {
            throw runtime_error{"Invalid target. Tries to access filesystem above root level"};
        }
        if (*b != *a) {
            throw runtime_error{"Invalid target. Tries to access filesystem outside root path"};
        }
    }

    return r;
}

Response HttpServer::FileHandler::readFile(const std::filesystem::path &path)
{
    ifstream file{path};
    if (file.is_open()) {
        Response r;
        const auto len = filesystem::file_size(path);
        r.body.resize(len);
        file.read(r.body.data(), len);
        r.target = path.string();
        return r;
    }

    return {500, "Failed to open file for read"};
}

Response HttpServer::FileHandler::handleDir(const std::filesystem::path &path)
{
    auto index = path;
    index /= "index.html";
    if (filesystem::is_regular_file(index)) {
        return readFile(index);
    }

    return listDir(path);
}

Response HttpServer::FileHandler::listDir(const std::filesystem::__cxx11::path &path)
{
    return {404, "Directoty listings are not supported"};
}

string_view Response::mimeType() const
{
    static const std::unordered_map<string_view, string_view> mime_types = {
        {"json", "application/json; charset=utf-8"},
        {"bin", "application/octet-stream"},
        {"bz", "application/x-bzip"},
        {"bz2", "application/x-bzip2"},
        {"css", "text/css"},
        {"csv", "text/csv"},
        {"gz", "application/gzip"},
        {"gif", "image/gif"},
        {"htm", "text/html"},
        {"html", "text/html"},
        {"ico", "image/vnd.microsoft.icon"},
        {"jar", "application/java-archive"},
        {"jpeg", "image/jpeg"},
        {"jpg", "image/jpeg"},
        {"js", "text/javascript"},
        {"mjs", "text/javascript"},
        {"otf", "font/otf"},
        {"png", "image/png"},
        {"svg", "image/svg+xml"},
        {"tar", "application/x-tar"},
        {"tiff", "image/tiff"},
        {"ttf", "font/ttf"},
        {"txt", "text/plain; charset=utf-8"},
        {"xhtml", "application/xhtml+xml"},
        {"xml", "application/xml"},
        {"zip", "application/zip"},
        {"7z", "application/x-7z-compressed"},
        {"jsonld", "application/ld+json"}
    };

    if (!mime_type.empty()) {
        return mime_type;
    }

    if (!target.empty()) {
        if (const auto pos = target.find_last_of('.'); pos != string::npos) {
            if (target.size() > (pos + 1)) {
                auto type = target.substr(pos + 1);
                if (auto mt = mime_types.find(type); mt != mime_types.end()) {
                    return mt->second;
                }
            }
        }
    }

    return {};
}



} // ns
