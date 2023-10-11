#pragma once

#include <regex>

#include "nsblast/DnsMessages.h"
#include "nsblast/logging.h"

namespace nsblast::lib::detail {

using namespace ::std::string_literals;
using namespace ::std;

constexpr int MAX_PTRS_IN_A_ROW = 16;
constexpr auto START_OF_POINTER_TAG = 0xC0;     // Binary: 11000000
constexpr auto START_OF_EXT_LABEL_TAG =  0x40;  // Binary: 01000000;

inline constexpr auto createLookupTableForCharsInLabelName() {
    array<bool, 255> table{};
    for (size_t i = 0; i < table.size(); i++) {
        if ((i >= '0' && i <= '9') || (i >= 'a' && i <= 'z') || (i >= 'A' && i <= 'Z') || i == '-' || i == '.') {
            table[i] = true;
        } else {
            table[i] = false;
        }
    }
    return table;
}

/*! Parse and valdate part of a domain name (to the first dot)
 *
 *  \param name The remainder of a domain-name to work on
 *  \param buffer Optional buffer (normaly a string or vector to write to).
 *                The buffer will only be populated if the name cannot
 *                directly be copied to a destination-buffer by the caller,
 *                for example for an email with "\." sequence(s)
 *
 *  Template parameter `email`. Turns on handling of \. sequences.
 *
 *  \returns lengt of the name consumed. length can be used by substring() to start at the dot
 *                tor the next segment.
 */
template <bool email = false, typename T, typename bufferT = string>
size_t /* lenght */ parseDomainNameSegment(T name, bufferT * buffer = nullptr) {

    static constexpr auto valid = createLookupTableForCharsInLabelName();

    if (name.empty()) {
        throw runtime_error{"parseDomainNameSegment: Invalid name-segment of zero bytes!"};
    }

    char prev = 0;
    char len = 0; // Bytes used by this segment in name. If all bytes are consumed, name.size() == len.
    for(auto ch : name) {
        ++len;
        if constexpr (email) {
            if (prev == '\\') [[unlikely]] {
                if (ch != '.') {
                    throw runtime_error{"parseDomainNameSegment: Label contains backslash not followed by a dot!"};
                }
                prev = ch;

                if (buffer && buffer->empty()) {
                    // Populate the buffer with what we have so far
                    // After this, we will populate the buffer at the end of the loop from now on.
                    copy(name.begin(), name.begin() + (len -1), back_inserter(*buffer));
                }

                goto go_on;
            }
            if (ch == '\\') [[unlikely]] {
                prev = ch;
                continue;
            }
            prev = ch;
        }

        if (ch == '.') {
            --len;
            break; // End of segment
        }

        if (len == 1 && ch == '-') [[unlikely]] {
            throw runtime_error{"parseDomainNameSegment: domain-name segment cannot start with a dash!"};
        }

        if (len == 1 && ch == '_') [[unlikely]] {
            // Used in some special applications like SRV records. Fow now, just allow them.
            ;
        }
        else if (!valid[static_cast<uint8_t>(ch)]) [[unlikely]] {
            throw runtime_error{"parseDomainNameSegment: Invalid character in name-segment!"};
        }

go_on:
        if (buffer && !buffer->empty()) {
            buffer->push_back(ch);
        }
    }

    assert(len <= name.size());
    assert(len != 0);

    return len;
}

template <bool commit = true, bool email = false, typename T>
uint16_t writeName(T& buffer, const uint16_t startOffset, const string_view fqdn) {

    static const auto save = [](T& buffer, uint16_t& offset, const string_view label) {
        if (label.size() > 63) {
            throw runtime_error{"writeName: labels must be less 64 bytes. This label: "s
                                + to_string(label.size())};
        }
        if constexpr (commit) {
            buffer[offset] = static_cast<uint8_t>(label.size());
            copy(label.begin(), label.end(), buffer.begin() + offset + 1);
        }
        offset += label.size() + 1;
    };

    string emailbuffer;
    string_view email_segment;
    string_view segment = fqdn;
    size_t min_buffer_len = 0;
    uint16_t offset = startOffset;
    if constexpr (email) {
        const auto slen = parseDomainNameSegment<email>(segment, &emailbuffer);

        if (emailbuffer.empty()) {
            email_segment = segment.substr(0, slen);
        } else {
            email_segment = emailbuffer;
        }

        segment = segment.substr(min<size_t>(slen + 1, segment.size()));
        min_buffer_len = email_segment.size() + segment.size() + startOffset + 2; // lenght email, root
        if (!segment.empty()) {
            ++min_buffer_len; // length 2.nd segment
        }
    } else {
        min_buffer_len = startOffset + fqdn.size() + 2;
    }

    if constexpr(commit) {
        if (min_buffer_len > buffer.size()) {
            throw runtime_error("writeName: buffer_size is less than the required size to add this domain-name: "s
                + to_string(min_buffer_len) + ", buffer-len: "s + to_string(buffer.size()));
        }
    }

    // We want to validate the buffer-length (above) before we commit the first segment.
    if constexpr (email) {
        assert(!email_segment.empty());
        save(buffer, offset, email_segment);
    }

    const auto req_bytes = min_buffer_len - startOffset;
    if (req_bytes >= 256) {
        throw runtime_error{"writeName: fdqn must be less than 256 bytes. This fdqn require "s
                            + to_string(req_bytes) + " bytes."};
    }

    while(!segment.empty()) {
        assert(segment.at(0) != '.');
        const auto len = parseDomainNameSegment(segment);
        if (len) {
            assert(len <= segment.size());
            save(buffer, offset, segment.substr(0, len));
        }
        segment = segment.substr(min<size_t>(len + 1, segment.size()));
    }

    if constexpr(commit) {
        buffer[offset] = 0;
    }
    return ++offset - startOffset;
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
    // Is it a root-label?
    if (fqdn.bytes() == 1) {
        if (buffer.size() + 1 > maxLen) {
            LOG_TRACE << "writeLabels: Exeeded maxLen";
            return 0;
        }
        buffer.push_back(0);
        return 1;
    }

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
        for(auto n = needle.rbegin(); n != needle.rend() && h != haystack.rend() && **n == **h; ++n, ++h) {
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
        LOG_TRACE << "writeLabels: Exeeded maxLen";
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
