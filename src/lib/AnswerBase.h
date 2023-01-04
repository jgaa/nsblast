#pragma once

#include <memory>
#include <string_view>
#include <vector>
#include <array>
#include <boost/asio/buffer.hpp>

//#include <numeric>
//#include <arpa/inet.h>

#include "type_values.h"

namespace nsblast::lib::ns  {

/*! Common part of a RR entry
 */
class AnswerBase
{
public:
    using fragments_t = std::vector<std::string_view>;
    using buffer_t = std::vector<uint8_t>; //boost::asio::mutable_buffer;
    using existing_labels_t = std::vector<uint16_t>;

    AnswerBase(const std::string& name,
               uint16_t rType,
               uint16_t rDlength,
               existing_labels_t& existingLabels);

    AnswerBase(uint16_t namePtr,
               uint16_t rType,
               uint16_t rDlength,
               existing_labels_t& existingLabels);

    virtual ~AnswerBase() = default;

//    /*! Undo changes done by tle latest Write
//     * \param buffer The buffer the write was applied to
//    */
//    void Revert(buffer_t& buffer) {
//        buffer.resize(orig_len_);
//    }

    void WriteHeader(buffer_t& buffer);

    /*! Can only be called after Write(), as the size is dynamic
     *
     * \return The bytes used by the answer RR, including the RR header.
     */
    std::size_t GetSize() const { return size_; }

    /*! Write the reply into the buffer
        This will write both the standard header from the base-class
        and the overridden specific data (like an A or SOA record).
     */
    virtual void Write(buffer_t& buffer) = 0;

    // Split "domainName" into it's fragments (items separated with dots)
    // FIXME: This belogns somewhere else...
    static fragments_t ToFraments(const std::string& domainName);

protected:
    void WriteRdlength(buffer_t& buffer);

private:
    const uint16_t name_ptr_ {0}; // Optional 'pointer' to an offset to a name in the reply message
    const std::string name_; // Optional name (if not using pointer).
    const uint16_t type_ {0};
    const uint16_t class_ {CLASS_IN}; /* Internet */
    const uint16_t ttl_ {300}; /* Common value */
    int rdlength_hdr_ {0};

protected:
    uint16_t rdlength_ = 0; /* Length of the data segment */
    size_t size_ = 0; /* Write will update this to the size of the buffer it consumes */
    size_t orig_len_ = 0;

    existing_labels_t& label_start_points_;


    /*! High-level write label method.
     *
     * It appends the domain-name from Zone to the name (unless the name ends
     * with a '.'), and appends the newly written name to label_start_points_
     */
    std::size_t WriteDomainName(buffer_t& buffer,
                                std::string_view domainName);

    /*! Add a label in the reply-buffer to the list of known labels.
     *
     * If the label-location is already in the known-list,
     * just return.
     */
    void AddLabelForCompression(uint16_t offset, const char * start);

    /* Write raw data */
    template <typename T> std::size_t WriteData(buffer_t& buffer, const T& data) {
        const auto start_size = buffer.size();
        const std::array<uint8_t, 1> size = {data.size()}; // one byte with the string-length
        std::copy(data.begin(), data.end(), std::back_inserter(size));
        std::copy(data.begin(), data.end(), std::back_inserter(buffer));
        return buffer.size() - start_size;
    }

    /* Write a domain-name into the end of the buffer, using header name-
     * compression if possible
     */
    std::size_t WriteDomainName(buffer_t& buffer,
                                const std::string& domainName);

    /*! Search all the existing labels in the reply for the domain name, or last part(s) of it
     *
     * \param labelStart Offset position for the best match.
     * \param pos Offset into the fragments array of where labelStart was matched
     * .
     */
    bool SearchForDomainName(const fragments_t& frags,
                             uint16_t& labelStart, std::size_t& pos,
                             const char *start) const;

    // NB: This method is only supposed to resolve strings we ourself have
    // put in the buffer (or verified). Therefore, there are no bounds or
    // sanity checks.
    bool ReverseCompare(uint16_t label,
                        const fragments_t& frags,
                        int &currFrag,
                        const char *start,
                        uint16_t& bestMatchedOffset) const;


    // For debugging - no sanity checks!
    std::string GetNameFromOffset(uint16_t offset, const char *start) const;
};


} // namespace

std::ostream& operator << (std::ostream& out,
                           const nsblast::lib::ns::AnswerBase::fragments_t& v);
