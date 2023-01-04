#pragma once

#include <vector>
#include <string_view>
#include <memory>
//#include <numeric>
//#include <arpa/inet.h>
//#include <boost/concept_check.hpp>
//#include <boost/regex.hpp>
#include <regex>


namespace nsblast::lib::ns  {

class LabelHeader
{
public:
    struct LabelHeaderException : public std::runtime_error {
        LabelHeaderException(const std::string& message)
            : std::runtime_error (message) {};
    };

    struct NoLabelsException : public LabelHeaderException {
        NoLabelsException(const std::string& message)
            : LabelHeaderException(message) {};
    };

    struct IllegalLabelException : public LabelHeaderException {
        IllegalLabelException(const std::string& message)
            : LabelHeaderException(message) {};
    };

    struct IllegalPointereException: public LabelHeaderException {
        IllegalPointereException(const std::string& message)
            : LabelHeaderException(message) {};
    };

    using labels_t = std::vector<std::string_view>;

    LabelHeader(std::string_view buffer, // Buffer for this segment (and may be more)
                labels_t& knownLabels,
                std::string_view messageBuffer)
    : message_buffer_{messageBuffer}
    {
        if (buffer.size() < 1) {
            throw LabelHeaderException{"Label-length underflow"};
        }

        //const char *end = buffer + size;
        //const char *p = buffer;
        bool is_traversing_pointer = false;
        auto b = buffer;
        while(true) {
            // *p is the length-field in the next label.
            if (b.front() == 0) {
                ++size_;
                if (!is_traversing_pointer) {
                    ++buffer_size_;
                }
                validateSize();

                // Root label. We are basically done
                if (names_.empty()) {
                    throw NoLabelsException{"No labels found in header"};
                }
                break;
            }
            const size_t len = static_cast<uint8_t>(b.front());
            if (len < 64) {
                // Normal label with the name as bytes

                if (len > b.size()) {
                    throw LabelHeaderException{
                        "Buffer underflow in label (potential hostile request)"};
                }
                std::string_view label{b.data() + 1, len};

                size_ += label.size();
                validateSize();

                if (!is_traversing_pointer) {
                    buffer_size_ += label.size() + 1;
                }

                //boost::string_ref label{p, len};

                if (!is_traversing_pointer) {
                    // If we are traversing a pointer, the label is already known.
                    knownLabels.push_back(label);
                }
                names_.push_back(label);

                // Validate the name
                static const std::regex pat{ R"([a-z0-9\.\-_]+)",
                    std::regex::icase | std::regex::optimize};
                if (!std::regex_match(label.begin(), label.end(), pat)) {
                    throw IllegalLabelException{"Invalid char in label"};
                }

                if (label.end() == b.end()) {
                    throw LabelHeaderException{"Labels end with no trailing NUL"};
                }

                auto full_label_size = label.size() + 1; // size char + name
                if (full_label_size <= b.size()) {
                    throw LabelHeaderException{"Labels end with no trailing NUL"};
                }
                // Shrink `b` by the size of the consumed label
                b = b.substr(full_label_size);
            } else if ((len & 0xC0) == 0xC0) {
                // Compressed label 2 byte header
                if (b.size() < 2) {
                    throw LabelHeaderException{
                       "Buffer underflow in label (potential hostile request"};
                }

                size_ += 2;
                validateSize();

                if (!is_traversing_pointer) {
                    buffer_size_ += 2;
                }

                const size_t pointer = *reinterpret_cast<const uint16_t *>(b.data()) & ~0xC000;
                names_.push_back(findPointer(pointer, knownLabels));

                // Point to the item after the resolved pointer.
                is_traversing_pointer = true;
                //p = names_.back().end();
                // TODO: Check the RFC if this assumption is correct.
                // Add some unit tests for this
                b = b.substr(2);

            } else {
                throw LabelHeaderException{"Invalid label length!"};
            }
        }
    }

    size_t getSize() const { return buffer_size_; }

    /*! Return the calculated size of the labels. Max 255.

        TODO: Check if this includes the size header of each label.
     */
    size_t getLabelSize() const { return size_; }

    /*! Return a normal domain name as a string, starting with the first label */
    std::string getDomainName() const {
        static const std::string empty;
        static const std::string dot{"."};
        std::ostringstream out;
        int cnt = 0;
        for(auto & label : names_) {
            out << (++cnt == 1 ? empty : dot) << label;
        }

        return out.str();
    }

    const labels_t& getLabels() const { return names_; }

private:
    std::string_view findPointer(uint16_t pointer, const labels_t& labels)
    {
        // The pointer is an offset into the message buffer
        const char *key = message_buffer_.data() + pointer;
        for(const auto& v : labels) {
            if (v.data() == key) {
                return v;
            }
        }

        throw IllegalPointereException{
            "Invalid label pointer (potential hostile request)!"};
    }

    void validateSize() {
        if (size_ > 255) {
            throw LabelHeaderException{"The labels exeeds the 255 bytes limit"};
        }
    }

    labels_t names_;
    std::size_t size_ = 0;
    std::size_t buffer_size_ = 0; // Bytes used by the labes in /this/ buffer
    std::string_view message_buffer_;
};


} // namespace
