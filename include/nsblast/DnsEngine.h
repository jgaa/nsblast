#pragma once

#include "nsblast/nsblast.h"
#include "nsblast/DnsMessages.h"
#include "nsblast/ResourceIf.h"
#include "nsblast/util.h"
#include "yahat/HttpServer.h"


namespace nsblast::lib {

class DnsTcpSession;

class DnsEngine {
public:
    using udp_t = boost::asio::ip::udp;
    using tcp_t = boost::asio::ip::tcp;
    using tcp_session_t = std::shared_ptr<DnsTcpSession>;

    struct Request {
        virtual ~Request() = default;

        boost::span<const char> span;
        boost::uuids::uuid uuid = newUuid();
        // We set the truncate flag if we reach this limit.
        uint32_t maxReplyBytes = MAX_UDP_QUERY_BUFFER;
    };

    class Endpoint {
    public:
        Endpoint(DnsEngine& parent)
            : parent_{parent} {}

        virtual ~Endpoint() = default;

        virtual void start() = 0;

        auto& parent() {
            return parent_;
        }

    private:
//        void process(Request& req, size_t len);
//        void processQuestions(Request& req, const MessageHeader& header);
//        void createErrorReply(MessageHeader::Rcode errCode,
//                              const MessageHeader& hdr,
//                              Request& req);

        DnsEngine& parent_;
    };


    DnsEngine(const Config& config, ResourceIf& resource);

    ~DnsEngine();

    void addEndpoint(std::shared_ptr<Endpoint> ep) {
        endpoints_.emplace_back(std::move(ep));
    }

    void start();
    void stop();

    /*! Process a request
     *
     *  \param request RFC1035 Message in raw, binary format
     *  \param mb MessageBuilder instance where the reply will be constructed
     *
     */
    void processRequest(const Request& request, MessageBuilder& mb);

    boost::asio::io_context& ctx() {
        return ctx_;
    }

    auto& config() const noexcept {
        return config_;
    }

    /*! Create and start a TCP session */
    tcp_session_t createTcpSession(tcp_t::socket && socket);

    void removeTcpSession(boost::uuids::uuid uuid);

private:
    using endpoints_t = std::vector<std::shared_ptr<Endpoint>>;

    void startEndpoints();
    void startIoThreads();

    ResourceIf& resource_;
    boost::asio::io_context ctx_;
    const Config config_;
    endpoints_t endpoints_;
    std::vector<std::thread> workers_;
    std::once_flag stop_once_;

    std::map<boost::uuids::uuid, tcp_session_t> tcp_sessions_; // Own the TCP session instances
    std::mutex tcp_session_mutex_;
};


} // ns
