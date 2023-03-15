
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

// Modified from https://stackoverflow.com/questions/180947/base64-decode-snippet-in-c
vector<uint8_t> base64Decode(const string_view in) {
  // table from '+' to 'z'
  static constexpr array<uint8_t, 80> lookup = {
      62,  255, 62,  255, 63,  52,  53, 54, 55, 56, 57, 58, 59, 60, 61, 255,
      255, 0,   255, 255, 255, 255, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,
      10,  11,  12,  13,  14,  15,  16, 17, 18, 19, 20, 21, 22, 23, 24, 25,
      255, 255, 255, 255, 63,  255, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35,
      36,  37,  38,  39,  40,  41,  42, 43, 44, 45, 46, 47, 48, 49, 50, 51};
  static_assert(lookup.size() == 'z' - '+' + 1);

  vector<uint8_t> out;
  out.reserve((in.size() / 4) * 3);
  int val = 0, valb = -8;
  for (uint8_t c : in) {
    if (c < '+' || c > 'z')
      break;
    c -= '+';
    if (lookup.at(c) >= 64)
      break;
    val = (val << 6) + lookup.at(c);
    valb += 6;
    if (valb >= 0) {
      out.push_back(char((val >> valb) & 0xFF));
      valb -= 8;
    }
  }
  return out;
}

// modified from http://stackoverflow.com/questions/180947/base64-decode-snippet-in-c
string Base64Encode(const span_t in)
{
    // Silence the cursed clang-tidy...
    constexpr auto magic_4 = 4;
    constexpr auto magic_6 = 6;
    constexpr auto magic_8 = 8;
    constexpr auto magic_3f = 0x3F;

    static constexpr string_view alphabeth{"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"};
    string out;
    out.reserve((in.size() * 4 + 2)/3);

    int val = 0;
    int valb = -magic_6;
    for (const uint8_t c : in) {
        val = (val<<magic_8) + c;
        valb += magic_8;
        while (valb>=0) {
            out.push_back(alphabeth[(val>>valb)&magic_3f]);
            valb-=magic_6;
        }
    }
    if (valb>-magic_6) out.push_back(alphabeth[((val<<magic_8)>>(valb+magic_8))&magic_3f]);
    while (out.size()%magic_4) out.push_back('=');
    return out;
}

} // ns
