#pragma once

namespace nsblast::lib {

#define PB_GET(obj, prop, defval) \
    (obj.has_##prop() ? obj.prop() : defval)


} // ns
