
#include <boost/asio.hpp>

#include "AnswerBase.h"

#include "nsblast/nsblast.h"
#include "nsblast/logging.h"

using namespace std;

std::ostream& operator << (std::ostream& out,
                           const nsblast::lib::ns::AnswerBase::fragments_t& v) {
    bool virgin = true;
    for(const auto s : v) {

        if (virgin)
            virgin = false;
        else
            out << '/';

        out << s;
    }

    return out;
}

namespace nsblast::lib::ns {

AnswerBase::AnswerBase(const std::string& name,
            uint16_t rType,
            uint16_t rDlength,
            existing_labels_t& existingLabels)
: name_{name}, type_{rType}, rdlength_{rDlength}
, label_start_points_{existingLabels} {
}

AnswerBase::AnswerBase(uint16_t namePtr,
            uint16_t rType,
            uint16_t rDlength,
            existing_labels_t& existingLabels)
: name_ptr_(namePtr | 0xC000), type_{rType}, rdlength_{rDlength}
, label_start_points_{existingLabels} {
}

void AnswerBase::WriteHeader(buffer_t& buffer) {
    orig_len_ = size_;

    // We can use name_ or name_ptr. Not both at the same time.
    assert(name_.empty() || name_ptr_);
    assert(!name_.empty() && !name_ptr_);

    if (!name_.empty()) {
        WriteDomainName(buffer, name_);
    }

    size_t remaining = 10; // Bytes remaining to be written
    if (name_ptr_) {
        remaining += 2;
    }

    buffer.resize(buffer.size() + remaining);
    auto * last_segment = &buffer[buffer.size() - remaining];

    // Write the numbers
    uint16_t *p = reinterpret_cast<uint16_t *>(last_segment);

    if (name_ptr_) {
        LOG_TRACE << "Writing name_ptr_: " << (name_ptr_ & ~0xC000);
        *p++ = htons(name_ptr_);
        AddLabelForCompression(name_ptr_, &buffer[0]);
    }

    *p++ = htons(type_);
    *p++ = htons(class_);
    *reinterpret_cast<uint32_t *>(p) = htonl(ttl_);

    rdlength_hdr_ = buffer.size() - 2;
    WriteRdlength(buffer);

    // Update size_ with the fixed bytes at the end of this section;
    //size_ = buffer.size() - orig_len_;
}

void AnswerBase::WriteRdlength(buffer_t& buffer) {
    LOG_TRACE << "Writing rdlength_ = " << rdlength_ << " at position " << rdlength_hdr_;
    char *write_pos = &buffer[rdlength_hdr_];
    *reinterpret_cast<uint16_t *>(write_pos) = htons(rdlength_);
}

std::size_t AnswerBase::WriteDomainName(buffer_t& buffer,
                                        std::string_view domainName) {
    std::size_t start_offset = buffer.size();
    std::string my_name {domainName};

    if (!domainName.empty() && (domainName.back() != '.')) {
        for(auto z = zone; z ; z = z->parent()) {
            my_name += '.';
            my_name += z->label();
        }
    }

    WriteDomainName(buffer, my_name);
    AddLabelForCompression(start_offset, &buffer[0]);
    return buffer.size() - start_offset;
}

void AnswerBase::AddLabelForCompression(uint16_t offset, const char * start) {
    const uint16_t my_offset = offset | 0xC000;
    const char *ofs = (start + (my_offset & ~0xC000));
    const uint16_t dst_offset = ((*ofs & 0xC0) == 0xC0) ?
        ntohs(*reinterpret_cast<const uint16_t *>(ofs)) : my_offset;

    if (std::find(label_start_points_.begin(), label_start_points_.end(),
        dst_offset) != label_start_points_.end()) {

        LOG_TRACE << "Already have offset " << (dst_offset & ~0xC000);
        return;
    }

    LOG_TRACE << "Adding offset " << (my_offset & ~0xC000)
        << ' ' << GetNameFromOffset(my_offset, start);

    label_start_points_.push_back(my_offset);
}

std::size_t AnswerBase::WriteDomainName(buffer_t& buffer, const std::string& domainName) {
    auto frags = ToFraments(domainName);
    const auto *start = buffer.data();
    uint16_t label {0};
    std::size_t frag_index {0};
    bool use_compression = true;
    if (!SearchForDomainName(frags, label, frag_index, start)) {
        frag_index = frags.size();
        use_compression = false;
    }

    const std::size_t start_size = buffer.size();
    for(std::size_t i = 0; i < frag_index; i++) {
        LOG_TRACE << "Writing name with " << frags[i].size() << " bytes: "
            << war::log::Esc(frags[i]);
        buffer.push_back(static_cast<uint8_t>(frags[i].size())); // one byte with the string-lenght
        std::copy(frags[i].begin(), frags[i].end(), std::back_inserter(buffer));
    }

    if (use_compression) {
        LOG_TRACE << "Append compressed data: " << (label & ~0xC000);
        buffer.resize(buffer.size() + 2);
        uint16_t *plabel = reinterpret_cast<uint16_t *>(&buffer[buffer.size() -2]);
        *plabel = htons(label | 0xC000);
    } else {
        LOG_TRACE << "Adding terminating zero";
        buffer.push_back(0); // end mark
    }

    return buffer.size() - start_size;
}

bool AnswerBase::SearchForDomainName(const fragments_t& frags,
                             uint16_t& labelStart, std::size_t& pos,
                             const char *start) const {
    bool have_match = false;

    // Reverse-compare the labels and the frags to find the best match
    for(auto label : label_start_points_) {

        LOG_TRACE << "Searching label: " << (label & ~0xC000)
            << ' ' << war::log::Esc(GetNameFromOffset(label, start))
            << " for "
            << frags;

        int curr_frag {-1};
        uint16_t last_matched_offset {0};
        ReverseCompare(label, frags, curr_frag, start, last_matched_offset);
        if ((curr_frag >= 0) && (curr_frag < static_cast<int>(frags.size()))) {
            LOG_TRACE << "Possible match: curr_frag=" << curr_frag
            << ", pos=" << pos
            << ", have_match=" << have_match;

            if (!have_match || (curr_frag < static_cast<int>(pos))) {
                have_match = true;
                pos = curr_frag;
                labelStart = last_matched_offset;

                LOG_TRACE << "Best match so far at offset: " << labelStart;

                if (pos == 0) {
                    LOG_TRACE << "Full match!";
                    return true; // No need to search further
                }
            }
        }
    }

    return have_match;
}

bool AnswerBase::ReverseCompare(uint16_t label,
                                const fragments_t& frags,
                                int &currFrag, const char *start,
                                uint16_t& bestMatchedOffset) const {

    const int label_offset = (label & ~0xC000);
    const char *buffer = start + label_offset;
    const size_t len = static_cast<uint8_t>(*buffer );
    if (len == 0) {
        // We are at the bottom. Start comparsion
        currFrag = frags.size();
        return true;
    }
    if (len < 64) { // Normal string
        const boost::string_ref my_name(buffer + 1, len);
        if (ReverseCompare(label + len + 1, frags, currFrag, start, bestMatchedOffset)) {
            if (--currFrag < 0) {
                return true; // Success
            }

            if (frags[currFrag] == my_name) {
                bestMatchedOffset = label_offset;
                return true;
            }

            ++currFrag; // Roll back
            return false;
        }
    } else if ((len & 0xC0) == 0xC0) {
        // Compressed label 2 byte header
        const uint16_t pointer = (ntohs(*reinterpret_cast<const uint16_t *>(buffer))) & ~0xC000;

        LOG_TRACE << "Re-trying at offset : " << pointer;

        // Just try again and may be we resolve the real string then.
        return ReverseCompare(pointer, frags, currFrag, start, bestMatchedOffset);
    }

    return false; // We missed
}

std::string AnswerBase::GetNameFromOffset(uint16_t offset,
                                          const char *start) const {
    std::string name;

    for(;;) {
        uint16_t my_offset = offset & ~0xC000;
        const char *pos = reinterpret_cast<const char *>(start + my_offset);
        uint8_t len = *reinterpret_cast<const uint8_t *>(pos);
        if (!len)
            break;

        if (!name.empty())
            name += '.';

        if ((len & 0xC0) == 0xC0) {
            const uint16_t ofs = ntohs(*reinterpret_cast<const uint16_t *>(pos)) & ~0xC000;
            return name + GetNameFromOffset(ofs, start);
        } else if (len && (len <= 64)) {
            const boost::string_ref label(pos + 1, len);
            name += std::string(label);
            offset = my_offset + (len + 1);
        } else {
            break; // error
        }
    }

    return name;
}

AnswerBase::fragments_t AnswerBase::ToFraments(const std::string& domainName)  {
    fragments_t frags;

    const char *p = domainName.c_str();
    const auto end = p + domainName.size();

    while(p != end) {
        const auto dot = std::find(p, end, '.');

        frags.push_back(boost::string_ref(p, dot - p));

        p = dot;
        if (p != end)
            ++p;
    }

    return frags;
}


} // namespace



} // ns
