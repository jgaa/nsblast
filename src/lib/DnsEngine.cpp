
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
public:
    UdpEndpoint(DnsEngine& parent, DnsEngine::udp_t::endpoint ep)
        : DnsEngine::Endpoint(parent), socket_{parent.ctx()}
    {
        socket_.bind(ep);
    }

    // Endpoint interface
    void next() override {
        // TODO: Use a pool of requests to speed it up...
        auto req = make_unique<UdpRequest>();

        boost::asio::mutable_buffer mb{req->buffer_in.data(), req->buffer_in.size()};

        LOG_TRACE << "Ready to receive a new UDP reqest on "
                  << socket_.local_endpoint()
                  << " as " << req->uuid;

        socket_.async_receive_from(mb, req->sender_endpoint,
                                   [this, req=move(req)](const boost::system::error_code& error,
                                   std::size_t bytes) {

            // Get ready to receive the next request
            // TODO: Add some logic to prevent us from queuing an infinite number of requests
            parent().ctx().post([this] {
                next();
            });

            if (error) {
                LOG_WARN << "DNS request from " << socket_.local_endpoint()
                         << " on UDP " << socket_.local_endpoint()
                         << " failed to receive data: " << error.message();
                return;
            }

            req->setBufferLen(bytes);

            LOG_DEBUG << "Received a UDP message of " << bytes << " bytes from "
                      << req->sender_endpoint.address()
                      << " on UDP " << socket_.local_endpoint()
                      << " as request id " << req->uuid;

            try {
                auto message = parent().processRequest(*req);
            } catch (const std::exception& ex) {
                // TODO: deal with it
                assert(false);
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
            auto ep = make_shared<UdpEndpoint>(engine, addr);
        }

        assert(ep);

        // Start it
        ep->next();

        // The engine assumes ownership for the endpoints
        engine.addEndpoint(move(ep));
    }
}


} // anon ns

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

    // Start IO threads
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

void DnsEngine::stop()
{
    call_once(stop_once_, [this]{
        ctx_.stop();
    });
}

Message DnsEngine::processRequest(const DnsEngine::Request &request)
{
    LOG_TRACE << "processRequest: Processing request " << request.uuid;

    return {};
}

void DnsEngine::startEndpoints()
{
    doStartEndpoints<udp_t>(*this, config_.dns_endpoint, config_.dns_udp_port);
}


} // ns
