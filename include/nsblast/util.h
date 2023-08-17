#pragma once

#include <variant>
#include <locale>

#include <boost/asio.hpp>
#include <boost/core/span.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_serialize.hpp>
#include <boost/endian/conversion.hpp>
#include <boost/type_index.hpp>
#include <boost/type_index/runtime_cast/register_runtime_class.hpp>



#include "nsblast/DnsMessages.h"

namespace nsblast::lib {
    boost::uuids::uuid newUuid();
    std::string newUuidStr();

    uint64_t getRandomNumber64();
    uint32_t getRandomNumber32();
    uint16_t getRandomNumber16();
    std::string getRandomStr(size_t len);

    /*! Calculate a sha256 checksum on the input
     *
     *  \param what Input, data to calculate a has from
     *  \param encodeToBase64 If true, return a base64 encoded string. If false, \
     *          return the binary hash value.
     *  \throws InternalErrorException If for some reason the hash calculatioon fails.
     */
    std::string sha256(span_t what, bool encodeToBase64 = true);

    template <typename T>
    auto makeUniqueFrom(T *ptr) {
        return std::unique_ptr<T>{ptr};
    }

    template <typename I, typename T>
    I getValueAt(const T& b, size_t loc) {
        const auto tlen = sizeof(I);
        if (loc + (tlen -1) >= b.size()) {
            throw std::runtime_error{"getValueAt: Cannot get value outside range of buffer!"};
        }

        auto *v = reinterpret_cast<const I *>(b.data() + loc);
        return boost::endian::big_to_native(*v);
    }

    template <typename T>
    auto get16bValueAt(const T& b, size_t loc) {
        return getValueAt<uint16_t>(b, loc);
    }

    template <typename T>
    auto get32bValueAt(const T& b, size_t loc) {
        return getValueAt<uint32_t>(b, loc);
    }

    template <typename T, typename I>
    void setValueAt(const T& b, size_t loc, I value) {
        if (loc + (sizeof(I) -1) >= b.size()) {
            throw std::runtime_error{"setValueAt: Cannot set value outside range of buffer!"};
        }

        auto *v = reinterpret_cast<I *>(const_cast<char *>(b.data() + loc));
        *v = boost::endian::native_to_big(value);
    }

    // ASCII tolower
    template <typename T>
    std::string toLower(const T& v) {
        std::string out;
        out.resize(v.size());
        auto p = out.begin();

        for(const char ch : v) {
            assert(p != out.end());
            static constexpr char diff = 'A' - 'a';
            if (ch >= 'A' && ch <= 'Z') {
                *p = ch - diff;
            } else {
                *p = ch;
            }
            ++p;
        }

        return out;
    }

    /*! ASCII compare character sequence, case insensitive
     *
     * \param start Character sequence we want to match
     * \param full Character sequence we want to look at
     * \param fullMatch If true, require the two striungs to be equalk.
     *          If false, require the two strings to be eual until the end of `start`.
     */
    template <typename startT, typename fullT>
    bool compareCaseInsensitive(const startT& start, const fullT& full, bool fullMatch = true) {
        static const std::locale loc{"C"};

        auto r = full.begin();
        for(auto ch : start) {
            if (r == full.end()) {
                return false;
            }
            if (tolower(ch, loc) != tolower(*r, loc)) {
                return false;
            }
            ++r;
        }

        if (fullMatch) {
            if (r != full.end()) {
                return false;
            }
        }

        return true;
    }

    template <typename T>
    void trim(T& str) {
        static const std::locale loc{"C"};

        size_t num_front = 0, num_end = 0;
        for (auto it = str.begin(); it != str.end() && isspace(*it, loc); ++it, ++num_front)
            ;

        if (num_front < str.size()) {
            for (auto it = str.rbegin(); it != str.rend() && isspace(*it, loc); ++it, ++num_end)
                ;
        }

        str = str.substr(num_front, str.size() - num_front - num_end);
    }

    // Simple return value that may or may not own it's buffer.
    // The caller must assume that they owns the buffer, unless
    // it's constructed with a r-value reference string
    struct FqdnKey {
        using data_t = std::variant<span_t, std::string>;

        FqdnKey()
            : d_{span_t{}} {}


        explicit FqdnKey(const std::string& s)
            : d_{span_t{s}} {}

        explicit FqdnKey(std::string&& s)
            : d_{std::move(s)} {}

        explicit FqdnKey(std::string_view s)
            : d_{span_t{s}} {}

        explicit FqdnKey(span_t s)
            : d_{s} {}

        FqdnKey(const FqdnKey&) = default;
        FqdnKey(FqdnKey&&) = default;

        FqdnKey& operator = (const FqdnKey&) = default;
        FqdnKey& operator = (FqdnKey&&) = default;

        span_t key() const noexcept {
            if (std::holds_alternative<span_t>(d_)) {
                return std::get<span_t>(d_);
            }

            assert(std::holds_alternative<std::string>(d_));
            return std::get<std::string>(d_);
        }

        operator span_t () const noexcept {
            return key();
        }

        operator std::string_view () const noexcept {
            auto s = key();
            return {s.data(), s.size()};
        }

        bool ownsBuffer() const noexcept {
            return std::holds_alternative<std::string>(d_);
        }

        bool operator == (span_t span) const noexcept {
            const auto mine = key();
            if (mine.size() != span.size()) {
                return false;
            }

            return std::memcmp(mine.data(), span.data(), mine.size()) == 0;
        }

        bool operator != (span_t span) const noexcept {
            return ! operator == (span);
        }

        std::string string() const {
            const auto k = key();
            return {k.data(), k.size()};
        }

    private:
        data_t d_;
    };

    template <typename T>
    bool hasUppercase(const T& str) noexcept {
        for(char ch : str) {
            if (ch >= 'A' && ch <= 'Z') {
                return true;
            }
        }
        return false;
    }

    // Get a key for a fqdn
    template <typename T>
    FqdnKey toFqdnKey(T && w) {
        if (hasUppercase(w)) {
            return FqdnKey{toLower(w)};
        }
        return FqdnKey{std::move(w)};
    }

    FqdnKey labelsToFqdnKey(const Labels& labels);

    /*! Get the next level down a fqdn path
     *
     *  For example
     *     getNextKey("www.example.com") returns "example.com"
     */
    span_t getNextKey(span_t fqdn) noexcept;

    template <typename T> auto to_asio_buffer(T& b) {
        return boost::asio::mutable_buffer{b.data(), b.size()};
    }

    template <typename T> auto to_asio_buffer(const T& b) {
        return boost::asio::const_buffer{b.data(), b.size()};
    }

    // Very simple, does not handle utf8
    template<typename T>
    std::string toPrintable(const T& data) {
        std::ostringstream o;

        for(auto ch : data) {
            if (ch >= ' ' && ch <= '~') {
                o << ch;
            } else if (ch == '\t') {
                o << ' ';
            } else {
                o << '.';
            }
        }

        return o.str();
    }

    /*! Get a text segment from rdata.
     *
     *  For RR's where there are one or more text segments
     *  in the rdata, like HINFO.
     */
    template <size_t Num>
    std::string_view getTextFromRdata(span_t rd, size_t index) {
        if (index >= Num) {
            throw std::runtime_error{"getTextFromRdata: Index out of range"};
        }
        std::string_view rval;
        for(size_t i = 0; i <= index; ++i) {
            if (rd.empty()) {
                throw std::runtime_error{"getTextFromRdata: text field has no length byte!"};
            }
            size_t len = rd[0];
            if (len >= rd.size()) {
                throw std::runtime_error{"getTextFromRdata - Length exeeds buffer-len!"};
            }

            rval = {rd.data() + 1, len};
            rd = rd.subspan(len + 1);
        }

        return rval;
    }

    /*! Get labels from rdata.
     *
     *  For RR's where there are one or more labels segments
     *  in the rdata, like RP.
     */
    template <size_t Num>
    Labels getLabelsFromRdata(span_t rd, size_t index) {
        size_t offset = 0;
        if (index >= Num) {
            throw std::runtime_error{"getTLabelsFromRdata: Index out of range"};
        }
        std::string_view rval;
        Labels label;
        for(size_t i = 0; i <= index; ++i) {
            if (rd.empty()) {
                throw std::runtime_error{"getTextFromRdata: field can not be empty!"};
            }
            label = {rd, 0};
            rd = rd.subspan(label.bytes());
        }

        return label;
    }

    boost::asio::ip::tcp::socket TcpConnect(
            boost::asio::io_context& ctx,
            const std::string& endpoint,
            const std::string& port,
            boost::asio::yield_context& yield);

    std::vector<char> base64Decode(const std::string_view in);
    std::string Base64Encode(const span_t in);

    // BOOST_SCOPE_EXIT confuses Clang-Tidy :/
    template <typename T>
    struct ScopedExit {
        ScopedExit(T&& fn)
            : fn_{std::move(fn)} {}

        ~ScopedExit() {
            fn_();
        }

    private:
        T fn_;
    };

} // ns
