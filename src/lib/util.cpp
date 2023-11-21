
#include <algorithm>
#include <random>
#include <regex>

#include "boost/locale.hpp"

#include "nsblast/util.h"
#include "nsblast/logging.h"
#include "nsblast/errors.h"

#include <openssl/evp.h>

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

    const lock_guard lock{mtx};
    return dist(mt);
}

} // namespace

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
    for(const auto& ch : fqdn) {
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

string getRandomStr(size_t len)
{
    string rval;
    rval.reserve(len);
    while(rval.size() < (len)) {
        auto v = getRandomNumberT<char>();
        if (v < ' ' || v > '~' || v == '\"' || v == '\'' || v == '`') {
            continue;
        }
        rval.push_back(v);
    }
    return rval;
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
      const auto index1 = base64_chars.find(in[i]);
      const auto index2 = base64_chars.find(in[i + 1]);
      const auto index3 = base64_chars.find(i + 2 < in.size() ? in[i + 2] : '=');
      const auto index4 = base64_chars.find(i + 3 < in.size() ? in[i + 3] : '=');

      const auto a = (index1 << 2) | (index2 >> 4);
      const auto b = ((index2 & 0xf) << 4) | (index3 >> 2);
      const auto c = ((index3 & 0x3) << 6) | index4;

      binary_data.push_back(a);
      if (static_cast<size_t>(index3) != std::string::npos) {
          binary_data.push_back(b);
      }
      if (static_cast<size_t>(index4) != std::string::npos) {
          binary_data.push_back(c);
      }
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
      const auto a = static_cast<uint8_t>(in[i]);
      const auto b = i + 1 < in.size() ? in[i + 1] : 0;
      const auto c = i + 2 < in.size() ? in[i + 2] : 0;

      const auto index1 = (a >> 2) & 0x3f;
      const auto index2 = ((a & 0x3) << 4) | ((b >> 4) & 0xf);
      const auto index3 = ((b & 0xf) << 2) | ((c >> 6) & 0x3);
      const auto index4 = c & 0x3f;

      base64_data.push_back(base64_chars[index1]);
      base64_data.push_back(base64_chars[index2]);
      base64_data.push_back(i + 1 < in.size() ? base64_chars[index3] : '=');
      base64_data.push_back(i + 2 < in.size() ? base64_chars[index4] : '=');
    }

    return base64_data;
}

string newUuidStr()
{
    return toLower(boost::uuids::to_string(newUuid()));
}

// Impl. based from https://stackoverflow.com/questions/2262386/generate-sha256-with-openssl-and-c
string sha256(span_t what, bool encodeToBase64)
{
    EVP_MD_CTX* context = EVP_MD_CTX_new();
    const ScopedExit bye{[context] {
        EVP_MD_CTX_free(context);
    }};

    if (context != nullptr) {
        if (EVP_DigestInit_ex(context, EVP_sha256(), nullptr) != 0) {
            if (EVP_DigestUpdate(context, what.data(), what.size()) != 0) {
                array<uint8_t, EVP_MAX_MD_SIZE> hash{};
                unsigned int lengthOfHash = 0;
                if (EVP_DigestFinal_ex(context, hash.data(), &lengthOfHash) != 0) {
                    if (encodeToBase64) {
                        return Base64Encode({reinterpret_cast<const char *>(hash.data()), lengthOfHash});
                    }
                    return {reinterpret_cast<const char *>(hash.data()), lengthOfHash};
                }
            }
        }
    }
    throw InternalErrorException{"sha256 failed!"};
}

bool validateFqdn(std::string_view fqdn)
{
    static const std::regex pattern{R"(^[_a-zA-Z0-9][a-zA-Z0-9-]*(\.[_a-zA-Z0-9][a-zA-Z0-9-]*)*?$)"};
    return std::regex_match(fqdn.begin(), fqdn.end(), pattern);
}

HashedKey getHashFromKeyAndSeed(std::string_view key, std::string seed) {
    if (seed.empty()) {
        seed = getRandomStr(16);
    }

    auto seeded_key = format("{}\t{}", seed, key);
    return {seed, sha256(seeded_key)};
}

HashedKey getHashFromKeyInFile(const std::filesystem::path& file, std::string seed)
{
    if (file.empty()) {
        LOG_WARN << "getHashFromKeyInFile - key-file argument is empty!";
        throw runtime_error{"key-file is empty"};
    }

    auto in = ifstream{file, std::ios_base::in & std::ios_base::binary};
    if (!in.is_open()) {
        auto err = errno;
        LOG_WARN << "Failed to open " << file << " for read: " << strerror(err);
        throw runtime_error{format("Failed to open file {} for read", file.string())};
    }

    std::string key;
    const auto len = filesystem::file_size(file);
    if (len < 8 || len > 1024) {
        LOG_WARN << "Key in file " << file << " must be 1 - 1024 bytes long!";
        throw runtime_error{format("Failed to open file {} for read", file.string())};
    }
    key.resize(len);
    in.read(key.data(), len);
    in.close();

    ScopedExit const bye{[&key] {
        //Erase the key from memory
        for(auto &c : key) {
            c = 0;
        }
        key.clear();
    }};

    return getHashFromKeyAndSeed(key, std::move(seed));
}

HashedKey getHashFromKeyInEnvVar(const std::string& name, std::string seed)
{
    if (auto *key = std::getenv(name.c_str())) {
        return getHashFromKeyAndSeed(key, std::move(seed));
    }

    LOG_WARN << "getHashFromKeyInEnvVar - Missing environment variable: " << name;
    throw runtime_error{format("Missing environment variable: {}", name)};
}

HashedKey getHashFromKeyInFileOrEnvVar(const std::filesystem::path& file, const std::string &envName, std::string seed)
{
    if (!file.empty()) {
        return getHashFromKeyInFile(file, std::move(seed));
    }

    return getHashFromKeyInEnvVar(envName, std::move(seed));
}

string readFileToBuffer(const std::filesystem::path &path)
{
    LOG_TRACE << "readFileToBuffer - reading file: " << path;
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        throw runtime_error{format("Faied to open file {} for read", path.string())};
    }

    auto len = std::filesystem::file_size(path);
    string b;
    b.resize(len);
    file.read(b.data(), b.size());
    return b;
}

string toBytes(const boost::uuids::uuid &uuid) {
    std::string b;
    b.assign(uuid.begin(), uuid.end());
    assert(b.size() == 16);
    return b;
}

bool isValidUuid(std::string_view uuid)
{
    try {
        auto result = boost::uuids::string_generator()(uuid.begin(), uuid.end());
        return result.version() != boost::uuids::uuid::version_unknown;
    } catch(const runtime_error&) {
        return false;
    }
}

string utf8FoldCase(std::string_view from)
{
    static const std::locale loc{"en_US.UTF-8"};

    if (from.empty()) {
        return {};
    }

    //auto& facet = std::use_facet<std::ctype<wchar_t>>(loc);
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
    auto ws = converter.from_bytes(from.begin(), from.end());

    // Don't work. Consitently throws std::bad_cast due to lack of some "facet".
    // The state of utf8/unicode support in C++20 in an embarrassment!
    // auto folded = boost::locale::fold_case(ws, loc);

    for(auto& ch: ws) {
        ch = tolower(ch, loc);
    }

    auto utf8_str = converter.to_bytes(ws);

    return utf8_str;
}

bool isSameZone(std::string_view zone, std::string_view fqdn)
{
    if (fqdn.size() < zone.size()) {
        return false;
    }

    const auto start = fqdn.size() - zone.size();
    const auto overlap = fqdn.substr(start);
    if (compareCaseInsensitive(zone, overlap, true)) {
        if (zone.size() == fqdn.size()) {
            return true;
        }

        assert(start > 0);
        if (fqdn[start - 1] == '.') {
            return true;
        }
    }

    return false;
}

} // namespace nsblast::lib
