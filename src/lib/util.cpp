#include "nsblast/util.h"

namespace nsblast::lib {

boost::uuids::uuid newUuid()
{
    static boost::uuids::random_generator uuid_gen_;
    return uuid_gen_();
}

FqdnKey labelsToFqdnKey(const Labels &labels) {
    return toFqdnKey(labels.string());
}

span_t getNextKey(span_t fqdn) noexcept {
    bool bs = false;
    size_t pos = 0;
    for(auto& ch : fqdn) {
        if (bs) {
            ; // Ignore this character
        } else {
            if (ch == '.') {
                return {fqdn.subspan(pos + 1)};
            }
            bs = ch == '\\';
        }
        ++pos;
    }

    return {};
}



}
