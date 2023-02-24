#pragma once

#include "nsblast/nsblast.h"
#include "nsblast/DnsMessages.h"
#include "nsblast/ResourceIf.h"
#include "nsblast/util.h"
#include "yahat/HttpServer.h"


namespace nsblast::lib {

class DnsEngine {
public:
    using udp_t = boost::asio::ip::udp;
    using tcp_t = boost::asio::ip::tcp;

    struct Request {
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

        virtual void next() = 0;

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
};


} // ns
