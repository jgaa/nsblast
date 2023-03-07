#include "nsblast/util.h"
#include "nsblast/logging.h"

using namespace std;

namespace nsblast::lib {

boost::uuids::uuid newUuid()
{
    static boost::uuids::random_generator uuid_gen_;
    return uuid_gen_();
}

FqdnKey labelsToFqdnKey(const Labels &labels) {
    return toFqdnKey(labels.string());
}

span_t getNextKey(span_t fqdn) noexcept {
    bool bs = false;
    size_t pos = 0;
    for(auto& ch : fqdn) {
        if (bs) {
            ; // Ignore this character
        } else {
            if (ch == '.') {
                return {fqdn.subspan(pos + 1)};
            }
            bs = ch == '\\';
        }
        ++pos;
    }

    return {};
}

boost::asio::ip::tcp::socket TcpConnect(boost::asio::io_context& ctx,
                                        const std::string &endpoint,
                                        const std::string &port,
                                        boost::asio::yield_context &yield)
{
    typename boost::asio::ip::tcp::resolver resolver(ctx);
    boost::system::error_code ec;

    auto endpoints = resolver.async_resolve(endpoint, port, yield[ec]);
    if (ec) {
        LOG_WARN << "TcpConnect: Failed to resolve: " << endpoint;;
        throw runtime_error{"Failed to resolve address"};
    }

    for(const auto& addr : endpoints) {
        LOG_INFO << "Connecting to TCP endpoint: " << addr.endpoint();

        boost::asio::ip::tcp::socket socket{yield.get_executor()};

        // TODO: Set up timer

        // TODO: bind?
        socket.async_connect(addr, yield[ec]);
        if (ec) {
            LOG_DEBUG << "TcpConnect: Failed to connect to " << addr.endpoint()
                      << ". Will try alternatives if they exists.";
            continue;
        }

        return socket;
    }

    LOG_WARN << "TcpConnect: Failed to connect to: " << endpoint;;
    throw runtime_error{"Failed to connect"};
}



}
