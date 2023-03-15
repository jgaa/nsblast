

#include <boost/scope_exit.hpp>
#include <boost/chrono.hpp>
#include <boost/asio/spawn.hpp>

#include "nsblast/nsblast.h"
#include "nsblast/DnsEngine.h"
#include "nsblast/logging.h"
#include "nsblast/util.h"


using namespace std;
using namespace std::chrono_literals;

namespace nsblast::lib {

namespace {

struct UdpRequest : public DnsEngine::Request {
    boost::asio::ip::udp::endpoint sender_endpoint;
    std::array<char, MAX_UDP_QUERY_BUFFER> buffer_in;

    void setBufferLen(size_t bytes) {
        span = {buffer_in.data(), bytes};
    }
};

struct TcpRequest : public DnsEngine::Request {
    std::array<char, 2> size_buffer;
    std::vector<char> buffer_in;

    void setBufferLen() {
        span = buffer_in;
        is_tcp = true;
    }
};

class UdpEndpoint : public DnsEngine::Endpoint {
public:
    UdpEndpoint(DnsEngine& parent, DnsEngine::udp_t::endpoint ep)
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
            parent().ctx().post([this] {
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

private:
    DnsEngine::udp_t::socket socket_;
};

class TcpEndpoint : public DnsEngine::Endpoint {
public:
    class Session {
        Session();

    private:
        DnsEngine::tcp_t::socket socket_;
    };

    TcpEndpoint(DnsEngine& parent, DnsEngine::tcp_t::endpoint ep)
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
               parent().createTcpSession(move(socket));
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
        engine.addEndpoint(move(ep));
    }
}

tuple<bool, shared_ptr<MessageBuilder>>
createBuilder(const DnsEngine::Request &request, const Message& message, uint16_t maxBufferSize) {
    auto mb = make_shared<MessageBuilder>();
    mb->setMaxBufferSize(maxBufferSize);

    auto org_hdr = message.header();
    auto hdr = mb->createHeader(org_hdr.id(), true, org_hdr.opcode(), org_hdr.rd());
    hdr.setAa(true);

    // Copy the questions section to the reply
    for(const auto& query : message.getQuestions()) {
        if (query.clas() != CLASS_IN) {
            hdr.setRcode(Message::Header::RCODE::NOT_IMPLEMENTED);
            return {false, mb};
        }

        if (query.type() == QTYPE_AXFR) {
            LOG_DEBUG << "createBuilder: " << "Processing AXFR query. Request: " << request.uuid;

            if (message.header().qdcount() != 1) {
                LOG_WARN << "Refusing AXFR request " << request.uuid
                         << " because the QUERY section has more than 1 entry. That is not valid DNS!";
                hdr.setRcode(Message::Header::RCODE::NAME_ERROR);
                return {false, mb};
            }

            if (!request.is_tcp) {
                LOG_WARN << "Refusing AXFR request " << request.uuid
                         << " because the transport is not TCP.";
                hdr.setRcode(Message::Header::RCODE::REFUSED);
                return {false, mb};
            }

            if (!request.is_axfr) {
                LOG_TRACE << "Setting is_axfr flag to true";
                request.is_axfr = true;
            }
        }

        if (!mb->addRr(query, hdr, MessageBuilder::Segment::QUESTION)) {
            return {false, mb}; // Truncated
        }
    }

    return {true, mb};
}

} // anon ns


class DnsTcpSession : public std::enable_shared_from_this<DnsTcpSession> {
public:
    DnsTcpSession(DnsEngine& parent,  DnsEngine::tcp_t::socket && socket)
        : parent_{parent}, socket_{move(socket)}
        , idle_timer_{socket_.get_executor()}
    {
        setIdleTimer();
    }

    ~DnsTcpSession() {
        LOG_DEBUG << "DnsTcpSession " << uuid_ << " is history...";
    }

    const auto& uuid() const noexcept {
        return uuid_;
    }

    void done() {
        if (!done_) {
            auto self = shared_from_this(); // don't die until we return
            if (socket_.is_open()) {
                boost::system::error_code ec; // don't throw
                socket_.close(ec);
            }
            parent_.removeTcpSession(uuid());
            done_ = true;
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
            idle_timer_.async_wait([this] (boost::system::error_code ec) {
               if (ec) {
                   if (ec == boost::asio::error::operation_aborted) {
                       LOG_TRACE << "DnsTcpSession " << uuid()
                                 << " idle-timer aborted. Ignoring.";
                       return;
                   }

                   LOG_WARN << "DnsTcpSession " << uuid()
                            << " idle-timer - unexpected error " << ec;
               }

               if (!done_) {
                   if (axfr_timeout_ > chrono::steady_clock::now()) {
                       LOG_DEBUG << "DnsTcpSession " << uuid()
                             << " idle-timer expiered but an axfr session is in progress. Resetting the timer.";
                       setIdleTimer();
                       return;
                   }

                   LOG_DEBUG << "DnsTcpSession " << uuid()
                         << " idle-timer expiered. Closing session.";
                   done();
               }
            });
        }
    }

    void start() {
        auto req = make_shared<TcpRequest>();
        if (!validate(*req, "next")) {
            return;
        }

        boost::asio::spawn([this, req](auto yield) {

            while(!done_) {
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

                        LOG_TRACE << "Reply to request: is_axfr =" << req->is_axfr;
                        if (req->is_axfr) {
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
        });
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


DnsEngine::DnsEngine(const Config &config, ResourceIf &resource)
    : resource_{resource}, config_{config}
{

}

DnsEngine::~DnsEngine()
{
    stop();

    LOG_DEBUG << "~DnsEngine(): Waiting for workers to end...";
    for(auto& thd : workers_) {
        thd.join();
    }

    LOG_DEBUG << "~DnsEngine(): Done.";
}

void DnsEngine::start()
{
    startEndpoints();
    startIoThreads();
}

void DnsEngine::stop()
{
    call_once(stop_once_, [this]{
        ctx_.stop();
    });
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

    return parse(req.is_tcp ? config_.tcp_qany_response : config_.udp_qany_response);
}

void DnsEngine::processRequest(const DnsEngine::Request &request, const DnsEngine::send_t &send)
{
    LOG_TRACE << "processRequest: Processing request " << request.uuid;

    auto out_buffer_len = request.maxReplyBytes;

    Message message{request.span}; // TODO: Return error code. Now throws if the message is malformed

    shared_ptr<MessageBuilder> mb;
    bool ok = true;
    tie(ok, mb) = createBuilder(request, message, out_buffer_len);
    auto hdr = mb->getMutableHeader();

    bool do_reply = true;
    BOOST_SCOPE_EXIT(&mb, &send, &do_reply) {
        if (do_reply) {
            mb->finish();
            send(mb, true);
        }
    } BOOST_SCOPE_EXIT_END

    if (!ok) {
        return;
    }

    auto trx = resource_.transaction();

    LOG_TRACE << "DnsEngine::processRequest " << request.uuid
              << ". qcount=" << message.header().qdcount();

    // Iterate over the queries and add our answers
    for(const auto& query : message.getQuestions()) {
        assert(query.clas() == CLASS_IN);
        const auto qtype = query.type();
        const auto orig_fqdn = query.labels();
        bool persuing_cname = false;

        const auto qtall_resp = getQtypeAllResponse(request, query.type());

        auto key = labelsToFqdnKey(orig_fqdn);

        if (query.type() == QTYPE_AXFR) {
            // TODO: Check if the caller is allowed to do AXFR!
            out_buffer_len = config_.dns_max_large_tcp_buffer_size;

            auto flush_if = [&](const Rr& rr) {
                if (mb->size() + rr.size() >= mb->maxBufferSize()) {
                    // Flush
                    LOG_TRACE << "DnsEngine::processRequest Flushing full reply-buffer";
                    mb->finish();
                    send(mb, false);

                    bool ok = true;
                    tie(ok, mb) = createBuilder(request, message, out_buffer_len);
                    assert(ok); // Should not fail here when we just call it again!
                    hdr = mb->getMutableHeader();
                }
            };

            size_t count = 0;
            vector<char> zone_buffer; // To keep the SOA we need as the latest RR in the reply
            optional<Entry> zone;
            vector<char> cut;
            trx->iterate(key, [&]
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
                                     << toPrintable(db_key);
                           return true;
                       }
                   }

                   // If we got here, we are outside the cut and should be back inside the zone.
                   cut.clear();
                }

                Entry entry{value};

                if (++count == 1) {
                    if (key != db_key) {
                        LOG_WARN << "Cannot do AXFR for " << query.labels().string()
                                 << ". Fqdn not found.";
                        hdr.setRcode(Message::Header::RCODE::NAME_ERROR);
                        return false;
                    }

                    // Expect SOA for the zone
                    if (!entry.header().flags.soa) {
                        LOG_WARN << "Cannot do AXFR for " << query.labels().string()
                                 << ". Not a zone.";
                        hdr.setRcode(Message::Header::RCODE::NAME_ERROR);
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
                        copy(db_key.begin(), db_key.end(), back_inserter(cut));
                    }

                    // For now, copy all the RR's. I don't think we have any RR's
                    // in the database Entry's that require special treatment.
                    flush_if(rr);
                    auto ok = mb->addRr(rr, hdr, MessageBuilder::Segment::ANSWER);
                    assert(ok);
                }

                return true; // We are ready for the next Entry
            });

            if (!zone) {
                hdr.setRcode(Message::Header::RCODE::NAME_ERROR, false);
                return;
            }

            // Add soa. The transfer must end with the soa record it started with
            assert(zone);
            assert(!zone->empty());
            assert(zone_buffer.size() == zone->buffer().size());
            assert(zone_buffer.data() == zone->buffer().data());
            auto rr = zone->begin();
            assert(rr->type() == TYPE_SOA);
            flush_if(*rr);
            ok = mb->addRr(*rr, hdr, MessageBuilder::Segment::ANSWER);
            assert(ok);
            return;
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
                    return; // Truncated
                }

                persuing_cname = true;
                key = labelsToFqdnKey(rr_cname->labels());
                goto again;
            }

            if (qtall_resp == QtypeAllResponse::HINFO) {
                StorageBuilder sb;
                sb.createHinfo(orig_fqdn.string(), 86400 /* one day */, "RFC8482", {});
                sb.finish();
                Entry entry{sb.buffer()};
                assert(!entry.empty());
                if (!mb->addRr(*entry.begin(), hdr, MessageBuilder::Segment::ANSWER)) {
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
                    return; // Truncated
                }
            } // for rr_set
        } else {
            // key not found.
            bool is_referral = false;
            if (auto prev = getNextKey(key); !prev.empty()) {
                // Is it a referral?
                if (auto entry = trx->lookup(prev); !entry.empty()) {
                    const auto& e_hdr = entry.header();
                    if (e_hdr.flags.ns && !e_hdr.flags.soa) {
                        // Yap, it's a referral
                        is_referral = true;
                        vector<string> ns_list;
                        for(const auto& rr : entry) {
                            if (rr.type() == TYPE_NS) {
                                if (!mb->addRr(rr, hdr, MessageBuilder::Segment::AUTHORITY)) {
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
                hdr.setRcode(Message::Header::RCODE::NAME_ERROR);
                continue;
            }
        } // fqdn not found with direct lookup
    } // For queries

    // Should we add nameservers in the auth section?

    // Add additional information as appropriate

    return;
}

void DnsEngine::startEndpoints()
{
    doStartEndpoints<udp_t>(*this, config_.dns_endpoint, config_.dns_udp_port);
    doStartEndpoints<tcp_t>(*this, config_.dns_endpoint, config_.dns_tcp_port);
}

void DnsEngine::startIoThreads()
{
    for(size_t i = 0; i < config_.num_dns_threads; ++i) {
        workers_.emplace_back([this, i] {
                LOG_DEBUG << "DNS worker thread #" << i << " starting up.";
                try {
                    ctx_.run();
                } catch(const exception& ex) {
                    LOG_ERROR << "DNS worker #" << i
                              << " caught exception: "
                              << ex.what();
                } catch(...) {
                    ostringstream estr;
#ifdef __unix__
                    estr << " of type : " << __cxxabiv1::__cxa_current_exception_type()->name();
#endif
                    LOG_ERROR << "DNS worker #" << i
                              << " caught unknow exception" << estr.str();
                }
                LOG_DEBUG << "DND worker thread #" << i << " done.";
        });
    }
}

DnsEngine::tcp_session_t DnsEngine::createTcpSession(DnsEngine::tcp_t::socket && socket)
{
    const auto rep = socket.remote_endpoint();
    const auto lep = socket.local_endpoint();
    tcp_session_t session;
    try {
        session = make_shared<DnsTcpSession>(*this, move(socket));
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
    ctx().post([w] {
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
