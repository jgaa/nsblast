
#include <random>
#include "nsblast/util.h"
#include "nsblast/logging.h"

using namespace std;

namespace nsblast::lib {

namespace {

template <typename T>
T getRandomNumberT()
{
    static random_device rd;
    static mt19937 mt(rd());
    static mutex mtx;
    static uniform_int_distribution<T> dist; //dist(1, numeric_limits<T>::max);

    lock_guard<mutex> lock{mtx};
    return dist(mt);
}

} // anon ns

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

uint64_t getRandomNumber64()
{
    return getRandomNumberT<uint64_t>();
}

uint32_t getRandomNumber32()
{
    return getRandomNumberT<uint32_t>();
}

uint16_t getRandomNumber16()
{
    return getRandomNumberT<uint32_t>();
}


}
