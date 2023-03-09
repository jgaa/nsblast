
#include "nsblast/nsblast.h"
#include "nsblast/ResourceIf.h"

using namespace std;

std::ostream& operator << (std::ostream& o, const nsblast::ResourceIf::Category& cat) {
    static constexpr array<string_view, 2> names = { "ZONE", "ENTRY" };

    return o << names.at(static_cast<size_t>(cat));
}
