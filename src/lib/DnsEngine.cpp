
#include <boost/scope_exit.hpp>

#include "nsblast/nsblast.h"
#include "nsblast/DnsEngine.h"
#include "nsblast/logging.h"


using namespace std;

namespace nsblast::lib {

namespace {


struct UdpRequest : public DnsEngine::Request {
    boost::asio::ip::udp::endpoint sender_endpoint;
    std::array<char, MAX_UDP_QUERY_BUFFER> buffer_in;

    void setBufferLen(size_t bytes) {
        span = {buffer_in.data(), bytes};
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

    // Endpoint interface
    void next() override {
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
                next();
            });

            if (error) {
                LOG_WARN << "DNS request from " << req->sender_endpoint
                         << " on UDP " << socket_.local_endpoint()
                         << " for request id " << req->uuid
                         << " failed to receive data: " << error.message();
                return;
            }

            req->setBufferLen(bytes);

            LOG_DEBUG << "Received a UDP message of " << bytes << " bytes from "
                      << req->sender_endpoint.address()
                      << " on UDP " << socket_.local_endpoint()
                      << " as request id " << req->uuid;

            try {
                auto message = make_shared<MessageBuilder>();
                parent().processRequest(*req, *message);

                if (message->empty() || message->header().ancount() == 0) {
                    LOG_DEBUG << "processRequest for request id " << req->uuid
                              << " came back empty. Will not reply.";
                    return;
                }

                boost::asio::const_buffer cb{message->span().data(), message->span().size()};

                LOG_DEBUG << "Sending a UDP message of " << cb.size() << " bytes to "
                          << req->sender_endpoint.address()
                          << " from UDP " << socket_.local_endpoint()
                          << " as a reply to request id " << req->uuid;

                socket_.async_send_to(cb,
                                      req->sender_endpoint,
                                      [this, req=move(req), message=move(message)]
                                      (const boost::system::error_code& error,
                                      std::size_t bytes) mutable {
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

        assert(ep);

        // Start it
        ep->next();

        // The engine assumes ownership for the endpoints
        engine.addEndpoint(move(ep));
    }
}


} // anon ns

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

void DnsEngine::processRequest(const DnsEngine::Request &request, MessageBuilder& mb)
{
    LOG_TRACE << "processRequest: Processing request " << request.uuid;

    mb.setMaxBufferSize(request.maxReplyBytes);

    BOOST_SCOPE_EXIT(&mb) {
        mb.finish();
    } BOOST_SCOPE_EXIT_END

    Message message{request.span}; // Throws if the message is malformed
    auto org_hdr = message.header();
    auto hdr = mb.createHeader(org_hdr.id(), true, org_hdr.opcode(), org_hdr.rd());
    hdr.setAa(true);

    // Copy the questions section to the reply
    for(const auto& query : message.getQuestions()) {
        if (query.clas() != CLASS_IN) {
            hdr.setRcode(Message::Header::RCODE::NOT_IMPLEMENTED);
            return;
        }

        if (!mb.addRr(query, hdr, MessageBuilder::Segment::QUESTION)) {
            return; // Truncated
        }
    }

    auto trx = resource_.transaction();

    // Iterate over the queries and add our answers
    for(const auto& query : message.getQuestions()) {
        assert(query.clas() == CLASS_IN);
        const auto qtype = query.type();
        const auto orig_fqdn = query.labels();
        bool persuing_cname = false;

        auto key = labelsToFqdnKey(orig_fqdn);

again:
        auto rr_set = trx->lookup(key);
        if (!rr_set.empty()) {
            const auto& rr_hdr = rr_set.header();

            if (rr_hdr.flags.cname && qtype != TYPE_CNAME) {

                // RFC 1034 4.2.3 - step 3 a // store CNAME and pursue CNAME

                auto rr_cname = find_if(rr_set.begin(), rr_set.end(), [](const auto& r) {
                    return r.type() == TYPE_CNAME;
                });

                if (rr_cname == rr_set.end()) {
                    throw runtime_error{" DnsEngine::processRequest Internal error: rr_cname == rr.end()"};
                }

                if (!mb.addRr(*rr_cname, hdr, MessageBuilder::Segment::ANSWER)) {
                    return; // Truncated
                }

                persuing_cname = true;
                key = labelsToFqdnKey(rr_cname->labels());
                goto again;
            }

            // Copy all matching entries
            for(const auto& rr : rr_set) {
                if (qtype == QTYPE_ALL || qtype == rr.type()) {
                    if (!mb.addRr(rr, hdr, MessageBuilder::Segment::ANSWER)) {
                        return; // Truncated
                    }
                }
            }
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
                                if (!mb.addRr(rr, hdr, MessageBuilder::Segment::AUTHORITY)) {
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
                                        if (!mb.addRr(rr, hdr, MessageBuilder::Segment::ADDITIONAL)) {
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


} // ns
