#pragma once

#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>

#include "nsblast/nsblast.h"
#include "nsblast/DnsMessages.h"
#include "nsblast/ResourceIf.h"
#include "nsblast/util.h"
#include "yahat/HttpServer.h"


namespace nsblast::lib {

class DnsTcpSession;
class Notifications;

class DnsEngine {
public:
    using udp_t = boost::asio::ip::udp;
    using tcp_t = boost::asio::ip::tcp;
    using tcp_session_t = std::shared_ptr<DnsTcpSession>;

    enum class QtypeAllResponse {
        IGNORE, // Not Qtype ALL
        ALL,
        RELEVANT,
        HINFO
    };

    struct Request {
        virtual ~Request() = default;

        boost::span<const char> span;
        boost::uuids::uuid uuid = newUuid();
        // We set the truncate flag if we reach this limit.
        uint32_t maxReplyBytes = MAX_UDP_QUERY_BUFFER;
        bool is_tcp = false;
        mutable bool is_axfr = false;
        mutable bool is_ixfr = false;
    };

    class Endpoint {
    public:
        Endpoint(DnsEngine& parent)
            : parent_{parent} {}

        virtual ~Endpoint() = default;

        virtual void start() = 0;
        virtual bool isUdp() const noexcept {
            return false;
        }

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

    /*! Functor used to send a DNS reply
     *
     *  \param data Contains a message to send
     *  \param final true if this is the sole or final message in the reply.
     */
    using send_t = std::function<void(std::shared_ptr<MessageBuilder>& data, bool final)>;

    /*! Process a request
     *
     *  \param request RFC1035 Message in raw, binary format
     *  \param send Functor that will send the reply. Fort TCP connection, the
     *              functor may be called several times to send a series with
     *              replied, for example for an AXFR reply.
     *
     */
    void processRequest(const Request& request, const send_t& send);

    boost::asio::io_context& ctx() {
        return ctx_;
    }

    auto& config() const noexcept {
        return config_;
    }

    ResourceIf& resource() noexcept {
        return resource_;
    }

    /*! Create and start a TCP session */
    tcp_session_t createTcpSession(tcp_t::socket && socket);

    void removeTcpSession(boost::uuids::uuid uuid);

    QtypeAllResponse getQtypeAllResponse(const Request& req, uint16_t type) const;

    uint16_t getMaxUdpBufferSizeWithOpt() const noexcept {
        return std::max<uint16_t>(config_.udp_max_buffer_size_with_opt_, MAX_UDP_QUERY_BUFFER);
    }

    /*! Get an unused ID for a request */
    uint32_t getNewId();

    /*! Release the id from the repository of active ID's */
    void idDone(uint32_t id);

    // Send an UDP message
    void send(span_t data, udp_t::endpoint ep,
              std::function<void(boost::system::error_code ec)> cb);

    Notifications& notifications() {
        return *notifications_;
    }

private:
    using endpoints_t = std::vector<std::shared_ptr<Endpoint>>;

    void startEndpoints();
    void startIoThreads();
    void doAxfr(const Request& request,
                const send_t& send,
                const Message& message,
                std::shared_ptr<MessageBuilder>& mb,
                const ResourceIf::RealKey& key,
                ResourceIf::TransactionIf& trx);
    void doIxfr(const Request& request,
                const send_t& send,
                const Message& message,
                std::shared_ptr<MessageBuilder>& mb,
                const ResourceIf::RealKey& key,
                ResourceIf::TransactionIf& trx);

    // For TCP IXFR/AXFR - Send what's in the buffer and
    // create a new buffer for 'rr' if the buffer is
    // too small to add 'rr'.
    void flushIf(std::shared_ptr<MessageBuilder>& mb,
                 MessageBuilder::NewHeader& hdr,
                 const Rr& rr,
                 const DnsEngine::Request &request,
                 const Message& message,
                 size_t outBufLen,
                 const DnsEngine::send_t &send);

    ResourceIf& resource_;
    boost::asio::io_context ctx_;
    const Config config_;
    endpoints_t endpoints_;
    std::vector<std::thread> workers_;
    std::once_flag stop_once_;

    boost::unordered_flat_map<boost::uuids::uuid, tcp_session_t> tcp_sessions_; // Own the TCP session instances
    std::mutex tcp_session_mutex_;
    boost::unordered_flat_set<uint32_t> current_request_ids_;
    std::mutex ids_mutex_;
    std::shared_ptr<Notifications> notifications_;
};


} // ns
