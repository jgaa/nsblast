
#include <algorithm>
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

// Modified from ChatGPT generated code
vector<char> base64Decode(const string_view in) {
    std::vector<char> binary_data;
    binary_data.reserve((in.size() / 4) * 3);

    static constexpr string_view base64_chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";

    for (std::size_t i = 0; i < in.size(); i += 4) {
      const int index1 = base64_chars.find(in[i]);
      const int index2 = base64_chars.find(in[i + 1]);
      const int index3 = base64_chars.find(i + 2 < in.size() ? in[i + 2] : '=');
      const int index4 = base64_chars.find(i + 3 < in.size() ? in[i + 3] : '=');

      const int a = (index1 << 2) | (index2 >> 4);
      const int b = ((index2 & 0xf) << 4) | (index3 >> 2);
      const int c = ((index3 & 0x3) << 6) | index4;

      binary_data.push_back(a);
      if (static_cast<size_t>(index3) != std::string::npos) binary_data.push_back(b);
      if (static_cast<size_t>(index4) != std::string::npos) binary_data.push_back(c);
    }

    return binary_data;
}

// Modified from ChatGPT generated code
string Base64Encode(const span_t in)
{
    static constexpr std::string_view base64_chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";
    string base64_data;
    base64_data.reserve(((in.size() + 2) / 3) * 4);

    for (std::size_t i = 0; i < in.size(); i += 3) {
      const int a = static_cast<uint8_t>(in[i]);
      const int b = i + 1 < in.size() ? in[i + 1] : 0;
      const int c = i + 2 < in.size() ? in[i + 2] : 0;

      const int index1 = (a >> 2) & 0x3f;
      const int index2 = ((a & 0x3) << 4) | ((b >> 4) & 0xf);
      const int index3 = ((b & 0xf) << 2) | ((c >> 6) & 0x3);
      const int index4 = c & 0x3f;

      base64_data.push_back(base64_chars[index1]);
      base64_data.push_back(base64_chars[index2]);
      base64_data.push_back(i + 1 < in.size() ? base64_chars[index3] : '=');
      base64_data.push_back(i + 2 < in.size() ? base64_chars[index4] : '=');
    }

    return base64_data;
}

} // ns
