
#include <array>

#include "DnsServer.h"

#include "nsblast/logging.h"
#include "HttpServer.h"
#include "swagger_res.h"

using namespace std;
using namespace std;
using boost::asio::ip::tcp;
using boost::asio::ip::udp;
namespace net = boost::asio;            // from <boost/asio.hpp>
using namespace std::placeholders;
using namespace std::string_literals;
using namespace std::chrono_literals;

namespace nsblast::lib {

namespace ns {

Endpoint::Endpoint(DnsServer &parent, udp::endpoint ep)
    : socket_{parent.ctx()}, parent_{parent} {

    socket_.open(ep.protocol());
    socket_.set_option(boost::asio::socket_base::reuse_address(true));
    socket_.bind(ep);
}

void Endpoint::next()
{
    auto request = make_unique<ns::Request>(*this);

    auto r = request.get();
    assert(r);
    socket_.async_receive_from(r->queryBuffer(), r->sep(),
                               [this, request=move(request)](const boost::system::error_code& error,
                               std::size_t bytes_transferred) {

        if (error) {
            LOG_WARN << "DNS request on " << socket_.local_endpoint()
                      << " failed to receive data: " << error.message();

            // TODO: Don't let this become a vector for DoS
            this_thread::sleep_for(2ms); // Prevent tight loop.
        }

        // Don't recurse, and also allow any available thread to pick up the
        // next incoming request.
        parent_.ctx().post([this] {
            next();
        });

        if (!error) {
            try {
                process(*request, bytes_transferred);
            } catch (const exception& ex) {
                LOG_ERROR << "Caught exception on DNS::process on "
                          << socket_.local_endpoint()
                          << ": " << ex.what();
            }
        }
    });
}

void Endpoint::process(Request &req, const size_t len)
{
    // This is where the DNS server begins
}


} // ns

DnsServer::DnsServer(const Config &config)
    : config_{config}
{

}

std::future<void> DnsServer::start()
{
    udp::resolver resolver(ctx_);

    auto port = config_.dns_port;
    if (port.empty()) {
        port = "53";
    }

    LOG_DEBUG << "Preparing to use UDP  "
              << config_.dns_endpoint << " on "
              << " port " << port;


    auto endpoint = resolver.resolve({config_.dns_endpoint, port});
    udp::resolver::iterator end;
    for(; endpoint != end; ++endpoint) {
        udp::endpoint ep = endpoint->endpoint();
        LOG_INFO << "Starting DNS endpoint: " << ep;

        auto e = make_shared<ns::Endpoint>(*this, ep);
        e->next();
        endpoints_.emplace_back(e);
    }

}


};
