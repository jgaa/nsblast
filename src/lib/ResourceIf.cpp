
#include "nsblast/nsblast.h"
#include "nsblast/ResourceIf.h"
#include "nsblast/util.h"

using namespace std;

std::ostream& operator << (std::ostream& o, const nsblast::ResourceIf::Category& cat) {
    static constexpr array<string_view, 3> names = { "ZONE", "ENTRY", "DIFF" };

    return o << names.at(static_cast<size_t>(cat));
}

std::ostream& operator << (std::ostream& o, const nsblast::ResourceIf::RealKey& key) {
    static constexpr array<string_view, 2> names = { "ENTRY", "DIFF" };

    return o << names.at(static_cast<size_t>(key.kClass()))
             << ' ' << key.dataAsString();
}

namespace nsblast {

using namespace ::nsblast::lib;

nsblast::ResourceIf::RealKey::RealKey(span_t key,
                                      ResourceIf::RealKey::Class kclass,
                                      bool binary)
    : bytes_{binary ? string{key.begin(), key.end()} : init(key, kclass, {})}
{
}

nsblast::ResourceIf::RealKey::RealKey(nsblast::span_t key, uint32_t version, nsblast::ResourceIf::RealKey::Class kclass)
    : bytes_{init(key, kclass, version)}
{
}

nsblast::span_t nsblast::ResourceIf::RealKey::key() const noexcept {
    return bytes_;
}

bool nsblast::ResourceIf::RealKey::empty() const noexcept{
    return bytes_.empty();
}

nsblast::ResourceIf::RealKey::Class nsblast::ResourceIf::RealKey::kClass() const noexcept {
    if (!empty()) {
        return static_cast<Class>(bytes_.at(0));
    }
    return Class::ENTRY;
}

string nsblast::ResourceIf::RealKey::dataAsString() const {
    std::string fqdn;
    std::string postfix;
    if (!empty()) {
        auto end = bytes_.end();
        if (bytes_.at(0) == static_cast<char>(ResourceIf::RealKey::Class::DIFF)) {
            assert(bytes_.size() >= 6);
            end -= 5; // 32 bit unsigned + 0 byte-marker
            const auto serial = get32bValueAt(bytes_, bytes_.size() - 4);
            postfix = "/"s + to_string(serial);
        }
        fqdn.assign(bytes_.begin() + 1, end);
        std::reverse(fqdn.begin(), fqdn.end());
    }
    return fqdn + postfix;
}

string nsblast::ResourceIf::RealKey::init(nsblast::span_t key,
                                          nsblast::ResourceIf::RealKey::Class kclass,
                                          optional<uint32_t> version) {
    std::string value;
    value.reserve(key.size() + 1 + (version ? 5 : 0));
    value.push_back(static_cast<uint8_t>(kclass));
    value.append(key.begin(), key.size());
    std::reverse(value.begin() + 1, value.end());
    if (version) {
        value.push_back(0);
        auto offset = value.size();
        value.resize(offset + 4);
        setValueAt(value, offset, *version);
    }
    return value;
}

} // ns
