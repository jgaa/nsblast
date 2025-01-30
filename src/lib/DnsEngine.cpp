

#include <boost/scope_exit.hpp>
#include <boost/chrono.hpp>
#include <boost/asio/spawn.hpp>

#include "nsblast/nsblast.h"
#include "nsblast/DnsEngine.h"
#include "nsblast/logging.h"
#include "nsblast/util.h"

#include "SlaveMgr.h"
#include "Metrics.h"

#include "Notifications.h"

using namespace std;
using namespace std::chrono_literals;

namespace nsblast::lib {

namespace {

struct UdpRequest : public DnsEngine::Request {
    boost::asio::ip::udp::endpoint sender_endpoint;
    std::array<char, MAX_UDP_QUERY_BUFFER> buffer_in{};

    void setBufferLen(size_t bytes) {
        span = {buffer_in.data(), bytes};
    }
};

struct TcpRequest : public DnsEngine::Request {
    std::array<char, 2> size_buffer{};
    std::vector<char> buffer_in;

    void setBufferLen() {
        span = buffer_in;
        is_tcp = true;
    }
};

class UdpEndpoint : public DnsEngine::Endpoint {
public:
    UdpEndpoint(DnsEngine& parent, const DnsEngine::udp_t::endpoint& ep)
        : DnsEngine::Endpoint(parent), socket_{parent.ctx()}
    {
        if (ep.address().is_v4()) {
            socket_.open(boost::asio::ip::udp::v4());
            LOG_TRACE << "Binding on ipv4 to " << ep;
        } else if (ep.address().is_v6()) {
            socket_.open(boost::asio::ip::udp::v6());
            LOG_TRACE << "Binding on ipv6 to " << ep;
        } else {
            assert(false);
        }

        socket_.set_option(boost::asio::socket_base::reuse_address(true));
        socket_.bind(ep);
    }

    void start() override {
        next();
    }

    [[nodiscard]] bool isUdp() const noexcept override {
        return true;
    }

    void send(span_t data,  const DnsEngine::udp_t::endpoint& ep,
              const std::function<void(boost::system::error_code ec)>& /*cb*/) {

        if (socket_.is_open()) {

            LOG_TRACE << "UdpEndpoint::send - Sending DNS message to "
                      << ep << " on UDP " << socket_.local_endpoint();

            boost::asio::const_buffer cb{data.data(), data.size()};
            socket_.async_send_to(cb, ep, [this, ep] (const boost::system::error_code& error,
                                  std::size_t /*bytes*/) mutable {
                if (error) {
                    LOG_DEBUG << "UdpEndpoint::send - DNS message to " << ep
                             << " on UDP " << socket_.local_endpoint()
                             << " failed: " << error.message();
                    return;
                }

                LOG_TRACE << "UdpEndpoint::send - Successfully SENT DNS message to "
                          << ep << " on UDP " << socket_.local_endpoint();
            });

        } else {
            throw runtime_error{"UdpEndpoint::send: Socket is closed"};
        }
    }

    void next() {
        // TODO: Use a pool of requests to speed it up...

        // We have to use shared ptr to pass ownership to the callback.
        // A unique_ptr woun't make it trough all the asio composed trickery
        auto req = make_shared<UdpRequest>();

        boost::asio::mutable_buffer mb{req->buffer_in.data(), req->buffer_in.size()};

        LOG_TRACE << "Ready to receive a new UDP reqest on "
                  << socket_.local_endpoint()
                  << " as " << req->uuid;

        socket_.async_receive_from(mb, req->sender_endpoint,
                                   [this, req=req]
                                   (const boost::system::error_code& error,
                                   std::size_t bytes) mutable {

            // Get ready to receive the next request
            // TODO: Add some logic to prevent us from queuing an infinite number of requests
            boost::asio::post(parent().ctx(), [this] {
                start();
            });

            if (error) {
                LOG_WARN << "DNS request from " << req->sender_endpoint
                         << " on UDP " << socket_.local_endpoint()
                         << " for request id " << req->uuid
                         << " failed to receive data: " << error.message();
                return;
            }

            req->setBufferLen(bytes);
            req->endpoint = req->sender_endpoint;

            LOG_DEBUG << "Received a DNS message of " << bytes << " bytes from "
                      << req->sender_endpoint.address()
                      << " on UDP " << socket_.local_endpoint()
                      << " as request id " << req->uuid;

            try {
                parent().processRequest(*req, [req, this](std::shared_ptr<MessageBuilder>& message, bool /*final */) {
                    if (message->empty()) {
                        LOG_DEBUG << "processRequest for request id " << req->uuid
                                  << " came back empty. Will not reply.";
                        return;
                    }

                    boost::asio::const_buffer cb{message->span().data(), message->span().size()};

                    LOG_DEBUG << "Sending a DNS message of " << cb.size() << " bytes to "
                              << req->sender_endpoint.address()
                              << " from UDP " << socket_.local_endpoint()
                              << " as a reply to request id " << req->uuid;

                    socket_.async_send_to(cb,
                                          req->sender_endpoint,
                                          [this, req=req, message=message]
                                          (const boost::system::error_code& error,
                                          std::size_t /*bytes*/) mutable {
                        if (error) {
                            LOG_WARN << "DNS request from " << socket_.local_endpoint()
                                     << " on UDP " << socket_.local_endpoint()
                                     << " for request id " << req->uuid
                                     << " failed to send reply: " << error.message();
                            return;
                        }

                        LOG_DEBUG << "Successfully replied to UDP message from "
                                  << req->sender_endpoint.address()
                                  << " on UDP " << socket_.local_endpoint()
                                  << " for request id " << req->uuid;
                    });
                });
            } catch (const std::exception& ex) {
                LOG_ERROR << "DNS request from " << socket_.local_endpoint()
                         << " on UDP " << socket_.local_endpoint()
                         << " for request id " << req->uuid
                         << " failed processing: " << ex.what();
            }
        });
    }

    [[nodiscard]] DnsEngine::udp_t::endpoint localEndpoint() const {
        return socket_.local_endpoint();
    }

private:
    DnsEngine::udp_t::socket socket_;
};

class TcpEndpoint : public DnsEngine::Endpoint {
public:
    class Session {
    public:
        Session() = delete;

    private:
        DnsEngine::tcp_t::socket socket_;
    };

    TcpEndpoint(DnsEngine& parent, const DnsEngine::tcp_t::endpoint& ep)
        : DnsEngine::Endpoint(parent)
        , acceptor_{parent.ctx(), ep}

    {
    }

    void start() override {
        LOG_INFO << "Listening for DNS TCP connections on " << acceptor_.local_endpoint();
        accept();
    }

    void accept() {
        acceptor_.async_accept([this](auto ec, auto socket) {
           if (ec) {
               LOG_DEBUG << "Failed to accept TCP connection on " << acceptor_.local_endpoint()
                         << ": " << ec.message();
           } else {
               parent().createTcpSession(std::move(socket));
           }

           accept();
        });
    }


private:
    boost::asio::ip::tcp::acceptor acceptor_;
};


template<typename T>
void doStartEndpoints(DnsEngine& engine, const std::string& endpoint, const std::string& port) {
    using ip_t = T;

    typename ip_t::resolver resolver(engine.ctx());

    auto endpoints = resolver.resolve(endpoint, port);

    for(const auto& addr : endpoints) {
        std::shared_ptr<DnsEngine::Endpoint> ep;
        if constexpr (std::is_same_v<ip_t, DnsEngine::udp_t>) {
            LOG_INFO << "Starting DNS/UDP endpoint: " << addr.endpoint();
            ep = make_shared<UdpEndpoint>(engine, addr);
        }

        if constexpr (std::is_same_v<ip_t, DnsEngine::tcp_t>) {
            LOG_INFO << "Starting DNS/TCP endpoint: " << addr.endpoint();
            ep = make_shared<TcpEndpoint>(engine, addr);
        }

        assert(ep);

        // Start it
        ep->start();

        // The engine assumes ownership for the endpoints
        engine.addEndpoint(std::move(ep));
    }
}

tuple<bool, shared_ptr<MessageBuilder>>
createBuilder(Server& server, const DnsEngine::Request &request, const Message& message,
              uint16_t maxBufferSize, uint16_t maxBufferSizeWithOpt) {
    auto mb = make_shared<MessageBuilder>();
    auto use_buffer_size = maxBufferSize;
    size_t opt_count_ = 0;
    bool ok = true;
    // For now, we are only interested in OPT records.
    for(const auto& rr : message.getAdditional()) {
        if (rr.type() == TYPE_OPT) {
            if (++opt_count_ == 2) {
                mb->setRcode(Message::Header::RCODE::FORMAT_ERROR);
                ok = false;
                continue;
            }

            // Will not add the actual RR to the additional section here.
            // That happens in mb->finish();
            mb->addOpt(maxBufferSizeWithOpt);

            // Now, look at the incoming OPT record
            RrOpt opt{message.span(), static_cast<uint16_t>(rr.offset())};
            if (opt.version() != 0) {
                mb->setRcode(Message::Header::RCODE::BADVERS);
                ok = false;
            }

            // Use whatever the OPT asked for, in the
            // range from MAX_UDP_QUERY_BUFFER to maxBufferSizeWithOpt
            use_buffer_size = max<uint16_t>(
                        min<uint16_t>(opt.maxBufferLen(), maxBufferSizeWithOpt),
                        MAX_UDP_QUERY_BUFFER);
        }
    }

    mb->setMaxBufferSize(use_buffer_size);

    auto org_hdr = message.header();
    auto hdr = mb->createHeader(org_hdr.id(), true, org_hdr.opcode(), org_hdr.rd());
    hdr.setAa(true);

    // Copy the questions section to the reply
    for(const auto& query : message.getQuestions()) {
        if (query.clas() != CLASS_IN) {
            mb->setRcode(Message::Header::RCODE::NOT_IMPLEMENTED);
            return {false, mb};
        }

        const auto qtype = query.type();

        if (qtype == QTYPE_AXFR) {
            LOG_DEBUG << "createBuilder: " << "Processing AXFR query. Request: " << request.uuid;

            if (message.header().qdcount() != 1) {
                LOG_WARN << "Refusing AXFR request " << request.uuid
                         << " because the QUERY section has more than 1 entry. That is not valid DNS!";
                mb->setRcode(Message::Header::RCODE::NAME_ERROR);
                return {false, mb};
            }

            if (!request.is_tcp) {
                LOG_WARN << "Refusing AXFR request " << request.uuid
                         << " because the transport is not TCP.";
                mb->setRcode(Message::Header::RCODE::REFUSED);
                return {false, mb};
            }

            if (!request.is_axfr) {
                LOG_TRACE << "Setting is_axfr flag to true";
                request.is_axfr = true;
            }
        } else if (qtype == QTYPE_IXFR) {
            if (message.header().qdcount() != 1) {
                LOG_WARN << "Refusing IXFR request " << request.uuid
                         << " because the QUERY section has more than 1 entry. That is not valid DNS!";
                mb->setRcode(Message::Header::RCODE::NAME_ERROR);
                return {false, mb};
            }

            if (!request.is_axfr) {
                LOG_TRACE << "Setting is_ixfr flag to true";
                request.is_ixfr = true;
            }
        }

        if (!mb->addRr(query, hdr, MessageBuilder::Segment::QUESTION)) {
             server.metrics().truncated_dns_responses().inc();
            return {false, mb}; // Truncated
        }
    }

    return {ok, mb};
}

} // anon ns


class DnsTcpSession : public std::enable_shared_from_this<DnsTcpSession> {
public:
    DnsTcpSession(DnsEngine& parent,  DnsEngine::tcp_t::socket && socket)
        : parent_{parent}, socket_{std::move(socket)}
        , idle_timer_{socket_.get_executor()}
    {
        setIdleTimer();
    }

    // Make Clang-Tidy shut up!
    DnsTcpSession() = delete;
    DnsTcpSession(const DnsTcpSession&) = delete;
    DnsTcpSession(DnsTcpSession &&) = delete;
    DnsTcpSession& operator = (const DnsTcpSession&) = delete;
    DnsTcpSession& operator = (DnsTcpSession&&) = delete;

    ~DnsTcpSession() {
        LOG_DEBUG << "DnsTcpSession " << uuid_ << " is history...";
    }

    const auto& uuid() const noexcept {
        return uuid_;
    }

    void done() {
        auto self = shared_from_this(); // don't die until we return
        if (!done_) {
            if (socket_.is_open()) {
                boost::system::error_code ec; // don't throw
                socket_.close(ec);
            }
            done_ = true;
            parent_.removeTcpSession(uuid());
        }
    }

    bool validate(const TcpRequest& req, string_view what,
             boost::system::error_code ec = {}) {
        if (done_) {
            LOG_DEBUG << "DnsTcpSession " << uuid()
                      << " for req " << req.uuid << " was done while "
                      << what;
            return false;
        }

        if (ec) {
            if (ec == boost::asio::error::eof) {
                LOG_DEBUG << "DnsTcpSession " << uuid()
                          << " for req " << req.uuid << " closed by peer on " << what;
            } else {
                LOG_DEBUG << "DnsTcpSession " << uuid()
                          << " for req " << req.uuid << " failed with error '"
                          << ec.message() << "' on " << what;
            }
            done();
            return false;
        }

        if (!socket_.is_open()) {
            LOG_DEBUG << "DnsTcpSession " << uuid()
                      << " for req " << req.uuid << " has its socked closed while "
                      << what;
            done();
            return false;
        }

        LOG_TRACE << "DnsTcpSession " << uuid()
                  << " for req " << req.uuid << " proceeding after "
                  << what;

        return true;
    }

    void setIdleTimer() {
        if (!parent_.config().dns_tcp_idle_time) {
            LOG_WARN << "DnsTcpSession: 'dns-tcp-idle-time' is set to 0 (disabled)";
            return;
        }

        idle_timer_.cancel();
        idle_timer_.expires_from_now(boost::posix_time::seconds{parent_.config().dns_tcp_idle_time});

        if (!done_) {
            idle_timer_.async_wait([w=weak_from_this()] (boost::system::error_code ec) {
                if (auto self = w.lock()) {
                    if (ec) {
                       if (ec == boost::asio::error::operation_aborted) {
                           LOG_TRACE << "DnsTcpSession " << self->uuid()
                                     << " idle-timer aborted. Ignoring.";
                           return;
                       }

                       LOG_WARN << "DnsTcpSession " << self->uuid()
                                << " idle-timer - unexpected error " << ec;
                    }

                    if (!self->done_) {
                       if (self->axfr_timeout_ > chrono::steady_clock::now()) {
                           LOG_DEBUG << "DnsTcpSession " << self->uuid()
                                 << " idle-timer expiered but an axfr session is in progress. Resetting the timer.";
                           self->setIdleTimer();
                           return;
                       }

                       LOG_DEBUG << "DnsTcpSession " << self->uuid()
                             << " idle-timer expiered. Closing session.";
                       self->done();
                    }
               }
            });
        }
    }

    void start() {
        auto req = make_shared<TcpRequest>();
        if (!validate(*req, "next")) {
            return;
        }
        req->endpoint = socket_.remote_endpoint();

        // The parent owns this instance, but we need it to exist until we exit the spawn context.
        boost::asio::spawn(parent_.ctx(), [self=shared_from_this(), this, req](auto yield) {
            while(!self->done_) {
                // Read message-length
                boost::system::error_code ec;
                auto bytes = socket_.async_receive(to_asio_buffer(req->size_buffer), yield[ec]);
                if (!validate(*req, "read message-length", ec)) {
                    return;
                }
                assert(bytes == req->size_buffer.size());

                const auto len = get16bValueAt(req->size_buffer, 0);
                if (!len) {
                    LOG_DEBUG << "DnsTcpSession " << uuid()
                              << " for req " << req->uuid
                              << " contains a 0 bytes DNS query. Assuming other end closed the connection.";
                    done();
                    return;
                }

                if (len > MAX_TCP_QUERY_LEN) {
                    LOG_DEBUG << "DnsTcpSession " << uuid()
                              << " for req " << req->uuid
                              << " contains a " << len << " bytes DNS query. "
                              << "My upper limit is " << MAX_TCP_QUERY_LEN << " bytes!";
                    done();
                    return;
                }

                req->buffer_in.resize(len);

                // Read message
                bytes = socket_.async_receive(to_asio_buffer(req->buffer_in), yield[ec]);
                if (!validate(*req, "read message", ec)) {
                    return;
                }

                assert(req->buffer_in.size() == bytes);
                req->setBufferLen();
                req->maxReplyBytes = MAX_TCP_MESSAGE_BUFFER;

                setIdleTimer();

                LOG_DEBUG << "DnsTcpSession " << uuid()
                          << " received a DNS message of " << bytes << " bytes from "
                          << socket_.remote_endpoint()
                          << " on TCP " << socket_.local_endpoint()
                          << " as request id " << req->uuid;

                try {
                    parent_.processRequest(*req, [this, &req, &yield](auto& message, bool final) {

                        LOG_TRACE << "Replying to TCP session " << uuid()
                                  << " for request " << req->uuid
                                  << " is_axfr=" << req->is_axfr
                                  << ", final=" << final
                                  << ", message-size is " << message->size()
                                  << ", Answer-count is " << (message->empty() ? 0 : message->header().ancount());


                        if (message->empty()) {
                            LOG_DEBUG << "processRequest/send for request id " << req->uuid
                                      << " came empty. Will not reply.";
                            return;
                        }

                        if (!validate(*req, "preparing reply")) {
                            return;
                        }

                        LOG_TRACE << "Reply to request: is_axfr =" << req->is_axfr
                                  << ", is_ixfr=" << req->is_ixfr;
                        if (req->is_axfr || req->is_ixfr) {
                            if (final) {
                                axfrResetTimeout();
                            } else {
                                axfrExtendTimeout();
                            }
                        }

                        // Set the length of the message-segment in two bytes before the message
                        // as required by DNS over TCP
                        const auto reply_len = static_cast<uint16_t>(message->span().size());
                        setValueAt(req->size_buffer, 0, reply_len);

                        LOG_DEBUG << "Sending a DNS reply message of "
                                  << reply_len << " + 2 bytes to "
                                  << socket_.remote_endpoint()
                                  << " from TCP " << socket_.local_endpoint()
                                  << " as a reply to request id " << req->uuid
                                  << " on TCP session " << uuid();

                        array<boost::asio::const_buffer, 2> buffers{
                            to_asio_buffer(req->size_buffer),
                            to_asio_buffer(message->span())
                        };

                        // TODO: Change the idle-timer to allow for a much longer idle period when
                        //       the session is doing one or more zone transfers.
                        boost::system::error_code ec;
                        socket_.async_send(buffers, yield[ec]);

                        if (!validate(*req, "sent reply", ec)) {
                            LOG_DEBUG << "Successfully replied to DNS message from "
                                  << socket_.local_endpoint()
                                  << " on TCP " << socket_.local_endpoint()
                                  << " for request id " << req->uuid;
                        }
                    });
                } catch (const std::exception& ex) {
                    LOG_ERROR << "DNS request from " << socket_.local_endpoint()
                             << " on UDP " << socket_.local_endpoint()
                             << " for request id " << req->uuid
                             << " failed with exception: " << ex.what();

                    done();
                } catch(...) {
                    ostringstream estr;
    #ifdef __unix__
                    estr << " of type : " << __cxxabiv1::__cxa_current_exception_type()->name();
    #endif
                    LOG_ERROR << "DNS TCP session " << uuid()
                              << " caught unknow exception in coroutine: " << estr.str();
                } // one request
            } // loop
        }, boost::asio::detached);
    } // start()

    // Currently we don't do parallel processing, so no need to track the individual
    // AXFR replies
    void axfrExtendTimeout() {
        LOG_TRACE << "axfrExtendTimeout - Extending time for ongoing AXFR transfer on session " << uuid();
        axfr_timeout_ = chrono::steady_clock::now() + 3min;
    }

    void axfrResetTimeout() {
        LOG_TRACE << "axfrResetTimeout - Removing extended time for obsolete AXFR transfer on session " << uuid();
        axfr_timeout_  = {};
    }

private:
    const boost::uuids::uuid uuid_ = newUuid();
    DnsEngine& parent_;
    DnsEngine::tcp_t::socket socket_;
    bool done_ = false;
    boost::asio::deadline_timer idle_timer_;

    // If set in the future, leave the session running even if the idle_timer timed out
    chrono::steady_clock::time_point axfr_timeout_ = {};
};


DnsEngine::DnsEngine(Server &server)
    : server_{server}
{
}

DnsEngine::~DnsEngine()
{
    stop();
    LOG_DEBUG << "~DnsEngine(): Done.";
}

void DnsEngine::start()
{
    startEndpoints();
}

void DnsEngine::stop()
{

}


DnsEngine::QtypeAllResponse
DnsEngine::getQtypeAllResponse(const Request &req, uint16_t type) const
{
    if (type != QTYPE_ALL) {
        return QtypeAllResponse::IGNORE;
    }

    static const auto parse = [](string_view val) {
        if (val == "hinfo")
            return QtypeAllResponse::HINFO;
        if (val == "relevant")
            return QtypeAllResponse::RELEVANT;
        if (val == "all")
            return QtypeAllResponse::ALL;

        LOG_WARN << "getQtypeAllResponse: Unknown response type '"
                 << val << "'.";
        return QtypeAllResponse::HINFO;
    };

    return parse(req.is_tcp ? config().tcp_qany_response : config().udp_qany_response);
}

void DnsEngine::send(span_t data, const boost::asio::ip::udp::endpoint& ep,
                     const std::function<void (boost::system::error_code)>& cb)
{
    // TODO: We may need to find the best/correct interface for the message based on
    //       subnet or public/private network
    for(auto& h : endpoints_) {
        if (h->isUdp()) {
            auto& handler = dynamic_cast<UdpEndpoint&>(*h);

            if (handler.localEndpoint().protocol() == ep.protocol()) {
                handler.send(data, ep, cb);
                return;
            }
        }
    }

    LOG_WARN << "DnsEngine::send - Found no appropriate handlerf for message to " << ep;
}

void DnsEngine::handleNotify(const DnsEngine::Request &request,
                             const Message &message,
                             const Message::Header& mhdr,
                             std::shared_ptr<MessageBuilder>& mb)
{
    const auto is_reply = mhdr.qr();

    if (is_reply) {
        mb.reset(); // Don't reply to a reply
    }

    if (mhdr.qdcount() != 1) {
        LOG_DEBUG << "Request " << request.uuid << " has opcode NOTIFY "
                  << " but not excactely 1 query. That is not valid DNS.";

        if (mb) {
            mb->setRcode(Message::Header::RCODE::FORMAT_ERROR);
        }
        return;
    }

    const auto rr = *message.getQuestions().begin();
    if (rr.type() != TYPE_SOA) {
        LOG_DEBUG << "Request " << request.uuid << " has opcode NOTIFY "
                  << " but the query is not for TYPE_SOA. That is not valid DNS.";
        if (mb) {
            mb->setRcode(Message::Header::RCODE::FORMAT_ERROR);
        }
        return;
    }

    if (rr.clas() != CLASS_IN) {
        if (rr.type() != TYPE_SOA) {
            LOG_DEBUG << "Request " << request.uuid << " has opcode NOTIFY "
                      << " but the querry is not for CLASS_IN. I do`n't support that.";
            if (mb) {
                mb->setRcode(Message::Header::RCODE::NOT_IMPLEMENTED);
            }
            return;
        }
    }

    const auto fqdn = toLower(rr.labels().string());

    if (is_reply) {
        LOG_TRACE << "DnsEngine::handleNotify - Dealing with reply for zone "
                  << fqdn << " with id " << mhdr.id();
        server_.notifications().notified(fqdn, request.endpoint, mhdr.id());
    } else {
        LOG_TRACE << "DnsEngine::handleNotify - Dealing with a new NOTIFY message for zone "
                  << fqdn << " with id " << mhdr.id();

        server_.slave().onNotify(fqdn, request.endpoint);
    }
}

void DnsEngine::doAxfr(const DnsEngine::Request &request,
                       const DnsEngine::send_t &send,
                       const Message& message,
                       shared_ptr<MessageBuilder>& mb,
                       const ResourceIf::RealKey& key,
                       ResourceIf::TransactionIf& trx)
{
    LOG_DEBUG << "DnsEngine::doAxfr - Starting request "
              << request.uuid
              << " regarding " << key;

    // TODO: Check if the caller is allowed to do AXFR!
    const auto out_buffer_len = config().dns_max_large_tcp_buffer_size;
    auto hdr = mb->getMutableHeader();

    size_t count = 0;
    vector<char> zone_buffer; // To keep the SOA we need as the latest RR in the reply
    optional<Entry> zone;
    vector<char> cut;
    trx.iterate({key}, [&]
                 (auto db_key, auto value) mutable {

        // Skip child-zones if they happen to be hosted by me (and the keys appears here)
        if (!cut.empty()) {
           if (cut.size() <= db_key.size()) {
               if (memcmp(cut.data(), db_key.data(), cut.size()) == 0) {
                   // The key is after the cut, inside the child-zone
                   // We only want to send RR's from inside the requested zone,
                   // so we have to ignore this key.
                   LOG_TRACE << "DnsEngine::processRequest for request "
                             << request.uuid
                             << " in AXFR; ignoring child Entry at "
                             << db_key;
                   return true;
               }
           }

           // If we got here, we are outside the cut and should be back inside the zone.
           cut.clear();
        }

        Entry entry{value};

        if (++count == 1) {
            if (key != db_key) {
                LOG_WARN << "Cannot do AXFR for " << key
                         << ". Fqdn not found.";
                mb->setRcode(Message::Header::RCODE::NAME_ERROR);
                return false;
            }

            // Expect SOA for the zone
            if (!entry.header().flags.soa) {
                LOG_WARN << "Cannot do AXFR for " << key
                         << ". Not a zone.";
                mb->setRcode(Message::Header::RCODE::NAME_ERROR);
                return false;
            }

            zone_buffer.reserve(value.size());
            std::copy(value.begin(), value.end(), back_inserter(zone_buffer));
            assert(!zone);
            zone.emplace(zone_buffer);
            assert(zone->begin()->type() == TYPE_SOA);
        }

        for(const auto& rr : entry) {
            const auto flags = entry.header().flags;
            if (flags.ns && !flags.soa) {
                // Start of a cut
                cut.reserve(db_key.size());
                copy(db_key.bytes().begin(), db_key.bytes().end(), back_inserter(cut));
            }

            // For now, copy all the RR's. I don't think we have any RR's
            // in the database Entry's that require special treatment.
            flushIf(mb, hdr, rr, request, message, out_buffer_len, send);
            auto ok = mb->addRr(rr, hdr, MessageBuilder::Segment::ANSWER);
            assert(ok);
        }

        return true; // We are ready for the next Entry
    });

    if (!zone) {
        mb->setRcode(Message::Header::RCODE::NAME_ERROR);
        return;
    }

    // Add soa. The transfer must end with the soa record it started with
    assert(zone);
    assert(!zone->empty());
    assert(zone_buffer.size() == zone->buffer().size());
    assert(zone_buffer.data() == zone->buffer().data());
    auto rr = zone->begin();
    assert(rr->type() == TYPE_SOA);
    flushIf(mb, hdr, *rr, request, message, out_buffer_len, send);
    auto ok = mb->addRr(*rr, hdr, MessageBuilder::Segment::ANSWER);
    assert(ok);
}

void DnsEngine::doIxfr(const DnsEngine::Request &request,
                       const DnsEngine::send_t &send,
                       const Message &message,
                       std::shared_ptr<MessageBuilder> &mb,
                       const ResourceIf::RealKey &key,
                       ResourceIf::TransactionIf &trx)
{
    LOG_DEBUG << "DnsEngine::doIxfr - Starting request "
              << request.uuid
              << " regarding " << key;

    // See if the request is OK and get the start serial.
    if (message.getAuthority().count() == 0) {
        LOG_DEBUG << "DnsEngine::doIxfr " << " for request "
                  << request.uuid
                  << " regarding " << key
                  << " does not contain a SOA record. That is not valid DNS.";
        mb->setRcode(Message::Header::RCODE::FORMAT_ERROR);
        return;
    }

    auto hdr = mb->getMutableHeader();

    uint32_t from_serial = 0;
    for(const auto& rr : message.getAuthority()) {
        if (rr.type() == TYPE_SOA) {
            RrSoa soa{message.span(), rr.offset()};
            from_serial = soa.serial();
        }
    }

    // Check that the zone exists
    const auto fqdn = key.dataAsString();
    auto zone = trx.lookup(fqdn);
    if (zone.empty() || !zone.flags().soa) {
        LOG_DEBUG << "DnsEngine::doIxfr " << " for request "
                  << request.uuid
                  << " regarding " << key
                  << ". The zone was not found.";
        mb->setRcode(Message::Header::RCODE::NAME_ERROR);
        return;
    }

    // TODO: Validate that the client has access to this operation on this zone

    // Check if there is a newer version
    const auto currentSoa = zone.getSoa();
    if (from_serial >= currentSoa.serial()) {
        LOG_DEBUG << "DnsEngine::doIxfr " << " for request "
                  << request.uuid
                  << " regarding " << key
                  << ". There is no newer version of the zone.";

        // Send an empty full transfer.
        mb->addRr(currentSoa, hdr, MessageBuilder::Segment::ANSWER);
        return;
    }

    // Create the DIFF key
    const ResourceIf::RealKey dkey{fqdn, from_serial, ResourceIf::RealKey::Class::DIFF};

    auto flush_if = [&](const Rr& rr) -> bool {
        if (request.is_tcp) {
            flushIf(mb, hdr, rr, request, message, config().dns_max_large_tcp_buffer_size, send);
            return true;
        }

        // For UDP, we can't get more buffer-space.
        // Stop, and politely ask the client to re-try over TCP.
        if (mb->size() + rr.size() >= mb->maxBufferSize()) {
            hdr.setTc(true);
            return false;
        }

        return true;
    };

    // Iterate over the diff's
    size_t diff_count = 0;
    trx.iterate(dkey, [&] (auto db_key, auto value) mutable {
        if (!dkey.isSameFqdn(db_key)) {
            return false; // No longer at the relevant key
        }

        if (++diff_count == 1) {
            // Create the first Soa with the current serial.
            // From now on we will complete the IXFR or send an error or TC bit.
            mb->addRr(currentSoa, hdr, MessageBuilder::Segment::ANSWER);
        }

        const Entry entry{value};
        size_t count = 0;
        for(const auto& rr : entry) {
            const auto type = rr.type();
            if (++count == 1) {
                // Most be soa, start of deletions (old version)
                if (type != TYPE_SOA) {
                    LOG_ERROR << "DnsEngine::doIxfr " << " for request "
                              << request.uuid
                              << " regarding " << key
                              << ". The DIFF data is invalid. First entry must be a SOA.";
                    mb->setRcode(Message::Header::RCODE::SERVER_FAILURE);
                    return false;
                }
            }
            if (!flush_if(rr)) {
                return false;
            }
            mb->addRr(rr, hdr, MessageBuilder::Segment::ANSWER);
        }

        // Stop at the current version of the zone in question
        return true;
    }, ResourceIf::Category::DIFF);

    if (!diff_count) {
        LOG_TRACE << "DnsEngine::doIxfr " << " for request "
                  << request.uuid
                  << " regarding " << key
                  << ". No first match was aquired.";

        if (!request.is_tcp) {
            hdr.setTc(true); // Ask the client to use TCP
            return;
        }

        // Do a full zone transfer
        return doAxfr(request, send, message, mb, key, trx);
    }

    // Is the reply valid? Did we get the changes for the current zone?
    // Add the current soa as the end-marker
    if (!flush_if(currentSoa)) {
        return;
    }
    mb->addRr(currentSoa, hdr, MessageBuilder::Segment::ANSWER);
}

void DnsEngine::flushIf(std::shared_ptr<MessageBuilder> &mb,
                        MessageBuilder::NewHeader &hdr,
                        const Rr &rr,
                        const DnsEngine::Request &request,
                        const Message &message,
                        size_t outBufLen,
                        const DnsEngine::send_t &send)
{
    if (mb->size() + rr.size() >= mb->maxBufferSize()) {
        // Flush
        LOG_TRACE << "DnsEngine::flushIf Flushing full reply-buffer";
        mb->finish();
        send(mb, false);

        bool ok = true;
        tie(ok, mb) = createBuilder(server_, request, message, outBufLen,
                                    getMaxUdpBufferSizeWithOpt());
        assert(ok); // Should not fail here when we just call it again!
        hdr = mb->getMutableHeader();
    }
}

void DnsEngine::processRequest(const DnsEngine::Request &request,
                               const DnsEngine::send_t &send)
{
    LOG_TRACE << "processRequest: Processing request " << request.uuid;    

    auto out_buffer_len = request.maxReplyBytes;

    Message message{request.span};
    LOG_DEBUG << "Request " << request.uuid << " from " << request.endpoint
              << ": " << message.toString();

    shared_ptr<MessageBuilder> mb;
    bool ok = true;
    tie(ok, mb) = createBuilder(server_, request, message, out_buffer_len, getMaxUdpBufferSizeWithOpt());
    auto hdr = mb->getMutableHeader();

    bool do_reply = true;

    ScopedExit se{[&mb, &do_reply, &send, &request, &ok, this] {
        if (do_reply && mb) {
            mb->finish();
            LOG_DEBUG << "Request " << request.uuid << " from " << request.endpoint
                      << " is done: " << mb->toString();
            send(mb, true);
            if (ok) {
                server_.metrics().dns_responses_ok().inc();
            }
        }
    }};

    if (!ok) {
        return;
    }

    const auto mhdr = message.header();
    const auto opcode = mhdr.opcode();
    if (opcode == Message::Header::OPCODE::NOTIFY) [[unlikely]] {
        return handleNotify(request, message, mhdr, mb);
    }
    if (opcode != Message::Header::OPCODE::QUERY) [[unlikely]] {
        mb->setRcode(Message::Header::RCODE::NOT_IMPLEMENTED);
        server_.metrics().dns_requests_not_implemented().inc();
        return;
    }

    auto trx = server_.resource().transaction();

    LOG_TRACE << "DnsEngine::processRequest " << request.uuid
              << ". qcount=" << message.header().qdcount();

    // Iterate over the queries and add our answers
    for(const auto& query : message.getQuestions()) {
        if (query.clas() != CLASS_IN) {
            LOG_DEBUG << "I can only handle CLASS_IN. Client requested " << query.clas()
                     << " in request " << request.uuid;
            mb->setRcode(Message::Header::RCODE::NOT_IMPLEMENTED);
            server_.metrics().dns_requests_error().inc();
            return;
        }

        const auto qtype = query.type();
        const auto orig_fqdn = query.labels();
        bool persuing_cname = false;

        const auto qtall_resp = getQtypeAllResponse(request, query.type());

        auto key = labelsToFqdnKey(orig_fqdn);

        if (qtype == QTYPE_AXFR) {
            return doAxfr(request, send, message, mb, {key, key_class_t::ENTRY}, *trx);
        }

        if (qtype == QTYPE_IXFR) {
            return doIxfr(request, send, message, mb, {key, key_class_t::ENTRY}, *trx);
        }

again:
        auto rr_set = trx->lookup(key);
        if (!rr_set.empty()) {
            const auto& rr_hdr = rr_set.header();

            if (rr_hdr.flags.cname
                    && (qtype != TYPE_CNAME)) {

                // RFC 1034 4.2.3 - step 3 a // store CNAME and pursue CNAME

                auto rr_cname = find_if(rr_set.begin(), rr_set.end(), [](const auto& r) {
                    return r.type() == TYPE_CNAME;
                });

                if (rr_cname == rr_set.end()) {
                    do_reply = false;
                    throw runtime_error{" DnsEngine::processRequest Internal error: rr_cname == rr.end()"};
                }

                if (!mb->addRr(*rr_cname, hdr, MessageBuilder::Segment::ANSWER)) {
                    server_.metrics().truncated_dns_responses().inc();
                    return; // Truncated
                }

                persuing_cname = true;
                key = labelsToFqdnKey(rr_cname->labels());
                goto again;
            }

            if (qtall_resp == QtypeAllResponse::HINFO) {
                StorageBuilder sb;
                sb.createHinfo(orig_fqdn.string(), config().dns_hinfo_ttl , "RFC8482", {});
                sb.finish();
                Entry entry{sb.buffer()};
                assert(!entry.empty());
                if (!mb->addRr(*entry.begin(), hdr, MessageBuilder::Segment::ANSWER)) {
                    server_.metrics().truncated_dns_responses().inc();
                    return; // Truncated
                }
                continue;
            }

            // Copy all matching entries
            for(const auto& rr : rr_set) {
                const auto rr_type = rr.type();
                switch(qtall_resp) {
                case QtypeAllResponse::IGNORE:
                    if (qtype != rr_type) {
                        continue; // Only give what the user asked for
                    }
                    break;
                case QtypeAllResponse::HINFO:
                        assert(false); // We were supposed to deal with this before the for-loop
                        continue;
                case QtypeAllResponse::RELEVANT:
                    if (rr_type != TYPE_A
                            && rr_type != TYPE_AAAA
                            && rr_type != TYPE_CNAME
                            && rr_type != TYPE_MX) {
                        continue;
                    }
                case QtypeAllResponse::ALL:
                    ; // Copy the record
                } // qtype

                if (!mb->addRr(rr, hdr, MessageBuilder::Segment::ANSWER)) {
                    server_.metrics().truncated_dns_responses().inc();
                    return; // Truncated
                }
            } // for rr_set
        } else {
            // key not found.
            bool is_referral = false;
            if (auto prev = getNextKey(key); !prev.empty()) {
                // Is it a referral?
                if (auto entry = trx->lookup({prev.data(), prev.size()}); !entry.empty()) {
                    const auto& e_hdr = entry.header();
                    if (e_hdr.flags.ns && !e_hdr.flags.soa) {
                        // Yap, it's a referral
                        is_referral = true;
                        vector<string> ns_list;
                        for(const auto& rr : entry) {
                            if (rr.type() == TYPE_NS) {
                                if (!mb->addRr(rr, hdr, MessageBuilder::Segment::AUTHORITY)) {
                                    server_.metrics().truncated_dns_responses().inc();
                                    return; // Truncated
                                }
                                ns_list.push_back(rr.labels().string());
                            }
                        }

                        // See if we can resolve the NS servers.
                        for(const auto& ns : ns_list) {
                            if (auto ns_rrset = trx->lookup(toFqdnKey(ns)); !ns_rrset.empty()) {
                                for(const auto& rr : ns_rrset) {
                                    const auto type = rr.type();
                                    if (type == TYPE_CNAME || type == TYPE_A || type == TYPE_AAAA) {
                                        if (!mb->addRr(rr, hdr, MessageBuilder::Segment::ADDITIONAL)) {
                                            server_.metrics().truncated_dns_responses().inc();
                                            return; // Truncated
                                        }
                                    } // relevant type
                                } // for ns_rrset
                            } // ns loouup
                        } // nslist
                    } // is referral
                } // if referral lookup block
            } // Next level block

            if (!is_referral && !persuing_cname) {
                mb->setRcode(Message::Header::RCODE::NAME_ERROR);
                server_.metrics().dns_requests_error().inc();
                continue;
            } else {
                server_.metrics().dns_requests_not_found().inc();
            }
        } // fqdn not found with direct lookup
    } // For queries

    // Should we add nameservers in the auth section?

    // Add additional information as appropriate
}

void DnsEngine::startEndpoints()
{
    doStartEndpoints<udp_t>(*this, config().dns_endpoint, config().dns_udp_port);
    doStartEndpoints<tcp_t>(*this, config().dns_endpoint, config().dns_tcp_port);
}

DnsEngine::tcp_session_t DnsEngine::createTcpSession(DnsEngine::tcp_t::socket && socket)
{
    const auto rep = socket.remote_endpoint();
    const auto lep = socket.local_endpoint();
    tcp_session_t session;
    try {
        session = make_shared<DnsTcpSession>(*this, std::move(socket));
    } catch (const exception& ex) {
        LOG_WARN << "Failed to start connection from " << rep
                 << " to " << lep;
        return {};
    }

    // TODO: set up configuration for the session; idle time etc.


    LOG_DEBUG << "Starting new DNS TCP connection from "
              << rep << " to (my interface) " << lep << " as session "
              << session->uuid();

    {
        lock_guard<mutex> lock{tcp_session_mutex_};
        tcp_sessions_.emplace(session->uuid(), session);
    }

    // Since the DnsEngine has ownership of the session, there is
    // a remote possibility that it will be orphaned before we get a
    // chance to run the code in the lambda. Therefore, we use
    // a weak pointer so we can easily detect this corner-case and handle
    // it correctly.
    auto w = session->weak_from_this();
    boost::asio::post(ctx(), [w] {
        if (auto self = w.lock()) {
            // Start receiving messages
            self->start();
        } else {
            LOG_DEBUG << "DnsEngine::createTcpSession / start session lambda: Session was orphaned!";
        }
    });

    return session;
}

void DnsEngine::removeTcpSession(boost::uuids::uuid uuid)
{
    LOG_DEBUG << "Removing TCP connection " << uuid;

    {
        lock_guard<mutex> lock{tcp_session_mutex_};
        tcp_sessions_.erase(uuid);
    }
}

} // ns
