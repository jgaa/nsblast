
#include "nsblast/nsblast.h"
#include "nsblast/ResourceIf.h"
#include "nsblast/util.h"

using namespace std;

std::ostream& operator << (std::ostream& o, const nsblast::lib::ResourceIf::Category& cat) {
    static constexpr array<string_view, 6> names = { "DEFAULT", "MASTER_ZONE", "ENTRY",
                                                    "DIFF", "ACCOUNT", "TRXLOG" };

    return o << names.at(static_cast<size_t>(cat));
}

std::ostream& operator << (std::ostream& o, const nsblast::lib::ResourceIf::RealKey& key) {
    static constexpr array<string_view, 8> names = { "ENTRY", "DIFF", "TENANT", "USER", "ROLE", "ZONE", "TZONE", "TRXID" };

    return o << names.at(static_cast<size_t>(key.kClass()))
             << ' ' << key.dataAsString();
}

namespace nsblast::lib {

namespace {
    auto concat(span_t left, span_t right) {
        std::string value;
        value.reserve(left.size() + right.size() + 1);
        value.append(left.begin(), left.end());
        value.push_back('/');
        value.append(right.begin(), right.end());
        return value;
    }
}

ResourceIf::RealKey::RealKey(span_t key,
                             ResourceIf::RealKey::Class kclass,
                             bool binary)
    : bytes_{binary ? string{key.begin(), key.end()} : init(key, kclass, {})}
{
}

ResourceIf::RealKey::RealKey(span_t key,
                             uint32_t version,
                             ResourceIf::RealKey::Class kclass)
    : bytes_{init(key, kclass, version)}
{
}

ResourceIf::RealKey::RealKey(span_t key, span_t postfix, Class kclass)
    : bytes_{init(concat(key, postfix), kclass, {})}
{
}

ResourceIf::RealKey::RealKey(uint64_t num, Class kclass)
    : bytes_{init(num, kclass)}
{
}

span_t ResourceIf::RealKey::key() const noexcept {
    return bytes_;
}

bool ResourceIf::RealKey::empty() const noexcept{
    return bytes_.empty();
}

bool ResourceIf::RealKey::isReversed(Class kclass) noexcept
{
    return kclass == Class::ENTRY
           || kclass == Class::DIFF
           || kclass == Class::ZONE;
}

ResourceIf::RealKey::Class ResourceIf::RealKey::kClass() const noexcept {
    if (!empty()) {
        return static_cast<Class>(bytes_[0]);
    }
    return Class::ENTRY;
}

string ResourceIf::RealKey::dataAsString() const {
    std::string fqdn;
    std::string postfix;
    if (!empty()) {
        auto end = bytes_.end();
        const auto kt = static_cast<ResourceIf::RealKey::Class>(bytes_.at(0));
        switch(kt) {
        case ResourceIf::RealKey::Class::TRXID: {
            assert(bytes_.size() == sizeof(uint64_t) + 1);
            return to_string(getValueAt<uint64_t>(bytes_, 1));
        }
        case ResourceIf::RealKey::Class::DIFF: {
            assert(bytes_.size() >= 6);
            end -= 5; // 32 bit unsigned + 0 byte-marker
            const auto serial = get32bValueAt(bytes_, bytes_.size() - 4);
            postfix = "/"s + to_string(serial);
            }
            [[fallthrough]];
        default:
            if (isReversed(kt)) {
                fqdn.assign(bytes_.begin() + 1, end);
                std::reverse(fqdn.begin(), fqdn.end());
                if (!postfix.empty()) {
                    return fqdn + postfix;
                }
                return fqdn;
            }
            if (!postfix.empty()) {
                return bytes_.substr(1) + postfix;
            }
            return bytes_.substr(1);
        }
    }

    return {};
}

bool ResourceIf::RealKey::isSameFqdn(const ResourceIf::RealKey &k) const noexcept
{
    size_t lstart_offset = 1;
    size_t lend_offset = 0;
    if (kClass() == Class::DIFF) {
        lend_offset = 5;
    }
    assert(bytes_.size() >= (lstart_offset + lend_offset));
    string_view left = {bytes_.data() + lstart_offset, bytes_.size() - (lstart_offset + lend_offset)};

    size_t rstart_offset = 1;
    size_t rend_offset = 0;
    if (k.kClass() == Class::DIFF) {
      rend_offset = 5;
    }
    assert(k.bytes_.size() >= (rstart_offset + rend_offset));
    string_view right = {k.bytes_.data() + rstart_offset, k.bytes_.size() - (rstart_offset + rend_offset)};

    return left == right;
}

string ResourceIf::RealKey::init(span_t key,
                                 ResourceIf::RealKey::Class kclass,
                                 optional<uint32_t> version) {

    std::string value;
    value.reserve(key.size() + 1 + (version ? 5 : 0));
    value.push_back(static_cast<uint8_t>(kclass));
    value.append(key.begin(), key.size());
    if (isReversed(kclass)) {
        std::reverse(value.begin() + 1, value.end());
    }
    if (version) {
        value.push_back(0);
        auto offset = value.size();
        value.resize(offset + 4);
        setValueAt(value, offset, *version);
    }
    return value;
}

string ResourceIf::RealKey::init(uint64_t value, Class kclass)
{
    if (kclass != Class::TRXID) {
        throw runtime_error{"kclass must be a type wich is a uint64_t"};
    }
    string rval;
    rval.resize(sizeof(uint64_t) + 1);
    rval[0] = static_cast<char>(kclass);
    setValueAt(rval, 1, value);
    return rval;
}

} // ns
