#pragma once

#include <locale>

#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_serialize.hpp>

namespace nsblast::lib {
    boost::uuids::uuid newUuid();

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
} // ns
