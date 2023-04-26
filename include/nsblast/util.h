#pragma once

#include <variant>
#include <boost/asio.hpp>
#include <boost/core/span.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_serialize.hpp>

#include "nsblast/DnsMessages.h"

namespace nsblast::lib {
    boost::uuids::uuid newUuid();

    uint64_t getRandomNumber64();
    uint32_t getRandomNumber32();
    uint16_t getRandomNumber16();

    template <typename T>
    auto make_unique_from(T *ptr) {
        return std::unique_ptr<T>{ptr};
    }

    template <typename T, typename I>
    I getValueAt(const T& b, size_t loc) {
        const auto tlen = sizeof(I);
        if (loc + (tlen -1) >= b.size()) {
            throw std::runtime_error{"getValueAt: Cannot get value outside range of buffer!"};
        }

        auto *v = reinterpret_cast<const I *>(b.data() + loc);

        auto constexpr ilen = sizeof(I);

        if constexpr (ilen == 1) {
            return *v;
        } else if constexpr (ilen == 2) {
            return ntohs(*v);
        } else if constexpr (ilen == 4) {
            return ntohl(*v);
        } else {
            static_assert (ilen <= 0 || ilen == 3 || ilen > 4, "getValueAt: Unexpected integer length");
        }

        throw std::runtime_error{"getValueAt: Something is very, very wrong..."};
    }

    template <typename T>
    auto get16bValueAt(const T& b, size_t loc) {
        return getValueAt<T, uint16_t>(b, loc);
    }

    template <typename T>
    auto get32bValueAt(const T& b, size_t loc) {
        return getValueAt<T, uint32_t>(b, loc);
    }

    template <typename T, typename I>
    void setValueAt(const T& b, size_t loc, I value) {
        if (loc + (sizeof(I) -1) >= b.size()) {
            throw std::runtime_error{"setValueAt: Cannot set value outside range of buffer!"};
        }

        auto *v = reinterpret_cast<I *>(const_cast<char *>(b.data() + loc));

        auto constexpr ilen = sizeof(I);

        if constexpr (ilen == 1) {
            *v = value;
        } else if constexpr (ilen == 2) {
            *v = htons(value);
        } else if constexpr (ilen == 4) {
            *v = htonl(value);
        } else {
            static_assert (ilen <= 0 || ilen == 3 || ilen > 4, "setValueAt: Unexpected integer length");
        }
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

//    // Very simple, does not handle utf8
//    template<typename T>
//    std::string toPrintable(const T& data) {
//        std::ostringstream o;

//        for(auto ch : data) {
//            if (ch >= ' ' && ch <= '~') {
//                o << ch;
//            } else if (ch == '\t') {
//                o << ' ';
//            } else {
//                o << '.';
//            }
//        }

//        return o.str();
//    }

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
        ScopedExit(const T& fn)
            : fn_{fn} {}

        ~ScopedExit() {
            fn_();
        }

    private:
        const T& fn_;
    };

} // ns
