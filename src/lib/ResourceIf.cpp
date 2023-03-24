
#include "nsblast/nsblast.h"
#include "nsblast/ResourceIf.h"

using namespace std;

std::ostream& operator << (std::ostream& o, const nsblast::ResourceIf::Category& cat) {
    static constexpr array<string_view, 2> names = { "ZONE", "ENTRY" };

    return o << names.at(static_cast<size_t>(cat));
}

std::ostream& operator << (std::ostream& o, const nsblast::ResourceIf::RealKey& key) {
    static constexpr array<string_view, 1> names = { "ENTRY" };

    return o << names.at(static_cast<size_t>(key.kClass()))
             << ' ' << key.dataAsString();
}

nsblast::ResourceIf::RealKey::RealKey(nsblast::span_t key,
                                      nsblast::ResourceIf::RealKey::Class kclass,
                                      bool binary)
    : bytes_{binary ? string{key.begin(), key.end()} : init(key, kclass)}
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
    if (!empty()) {
        fqdn.assign(bytes_.begin() + 1, bytes_.end());
        std::reverse(fqdn.begin(), fqdn.end());
    }
    return fqdn;
}

string nsblast::ResourceIf::RealKey::init(nsblast::span_t key, nsblast::ResourceIf::RealKey::Class kclass) {
    std::string value;
    value.reserve(key.size() + 1);
    value.push_back(static_cast<uint8_t>(kclass));
    value.append(key.begin(), key.size());
    std::reverse(value.begin() + 1, value.end());
    return value;
}
