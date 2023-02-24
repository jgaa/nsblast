#pragma once

#include "nsblast/DnsMessages.h"
#include "nsblast/logging.h"

namespace nsblast::lib::detail {

using namespace ::std::string_literals;
using namespace ::std;

constexpr int MAX_PTRS_IN_A_ROW = 16;
constexpr auto START_OF_POINTER_TAG = 0xC0; // Binary: 11000000
constexpr char BUFFER_HEADER_LEN = 8;

// Write a fdqn in text representation as a RFC1035 name (array of labels)
template <typename T>
uint16_t writeName(T& buffer, uint16_t offset, string_view fdqn, bool commit = true) {
    const auto start_offset = offset;

    if (commit) {
        assert((offset + fdqn.size() + 2) <= buffer.size());
    }

    if (fdqn.size() > 255) {
        throw runtime_error{"writeName: fdqn must be less than 256 bytes. This fdqn is: "s
                            + to_string(fdqn.size())};
    }

    for(auto dot = fdqn.find('.'); !fdqn.empty(); dot = fdqn.find('.')) {
        const auto label = (dot != string_view::npos) ? fdqn.substr(0, dot) : fdqn;

        // The root should not appear here as a dot
        assert(label != ".");

        if (label.size() > 63) {
            throw runtime_error{"writeName: labels must be less 64 bytes. This label: "s
                                + to_string(label.size())};
        }
        if (commit) {
            buffer[offset] = static_cast<uint8_t>(label.size());
        }
        ++offset;
        if (commit) {
            copy(label.begin(), label.end(), buffer.begin() + offset);
        }
        offset += label.size();

        if (dot == string_view::npos) {
            break;
        }

        // Strip the label off `fdqn` so we can go hunting for the next label!
        fdqn = fdqn.substr(label.size());
        if (!fdqn.empty()) {
            assert(fdqn.front() == '.');
            fdqn = fdqn.substr(1);
        }
    }

    // Always add root
    if (commit) {
        buffer[offset] = 0;
    }
    return ++offset - start_offset;
}


template <typename T>
void writeNamePtr(T& buffer, uint16_t offset, uint16_t namePtr) {
    auto *w = reinterpret_cast<uint16_t *>(buffer.data() + offset);
    *w = htons(namePtr);
    buffer[offset] |= START_OF_POINTER_TAG;
}

template <typename T>
uint16_t resolvePtr(const T& buffer, uint16_t offset) {
    // Loose the 2 bits signaling the pointer and get a uint16 pointer to the pointer value
    auto *ch = buffer.data() + offset;
    array<char, 2> ptr_buf;
    ptr_buf[0] = *ch & ~START_OF_POINTER_TAG;
    ptr_buf[1] = *++ch;
    const auto *v = reinterpret_cast<const uint16_t *>(ptr_buf.data());
    // Convert to host representation

    const auto ptr = ntohs(*v);
    return ptr;
}

template <typename T, typename B>
boost::asio::ip::address bufferToAddr(const B& buffer) {
    typename T::bytes_type bytes;

    assert(buffer.size() == bytes.size());
    copy(buffer.begin(), buffer.end(), bytes.begin());
    return T{bytes};
}



// Try to compress and add the labels from `fqdn` to buffer. Update existing if we write anything but a pointer.
// returns 0 if we needed to exeed maxLen.
// NB: existing breaks (undefined behaviour) if the buffer is re-allocated as all
template <typename P, typename B>
uint16_t writeLabels(const Labels& fqdn, P& existing, B& buffer, size_t maxLen) {

    // We need to search from the end, while Labels only provide forward iterators

    vector<Labels::Iterator> needle;

    int32_t best_match = -1;
    uint16_t best_count = 0;
    bool full_match = false;

    needle.reserve(fqdn.count());
    for(auto it = fqdn.begin(); it != fqdn.end(); ++ it) {
        needle.emplace_back(it);
    }

    for(const auto& e : existing) {
        if (e.count() <= best_count) {
            // irrelevant
            continue;
        }

        vector<Labels::Iterator> haystack;
        haystack.reserve(e.count());
        for(auto it = e.begin(); it != e.end(); ++ it) {
            haystack.emplace_back(it);
        }

        auto count = 0;
        auto h = haystack.rbegin();
        for(auto n = needle.rbegin(); n != needle.rend() && **n == **h; ++n, ++h) {
            if (++count > best_count) {
                best_match = h->location();
                best_count = count;
            }
        }

        if (best_count == fqdn.count()) {
            full_match = true;
            break;
        }
    }

    // Now, if we have a best_match, that's the pointer we will use for compression.
    const size_t orig_buffer_size = buffer.size();
    size_t len = 0;

    if (best_match >= 0) {
        len += 2; // Pointer
    }

    vector<span_t> segments;
    segments.reserve(fqdn.count() - best_count);

    auto it = fqdn.begin();
    for(auto i = best_count; i < fqdn.count(); ++i, ++it) {
        len += it->size() + 1;
        segments.emplace_back(*it);
    }

    if (maxLen && (len >= maxLen)) {
        LOG_TRACE << "writeLabels: Exeeded mexLen";
        return 0;
    }

    buffer.reserve(orig_buffer_size + len);
    for(auto segment: segments) {
        buffer.push_back(segment.size());
        copy(segment.begin(), segment.end(), back_inserter(buffer));
    }

    if (best_match >= 0) {
        buffer.resize(buffer.size() + 2);
        writeNamePtr(buffer, buffer.size() - 2, best_match);
    }

    if (!full_match) {
        existing.emplace_back(buffer, orig_buffer_size);
    }
    return len;
}


} // ns
