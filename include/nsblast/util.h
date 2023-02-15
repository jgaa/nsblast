#pragma once

#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_serialize.hpp>

namespace nsblast::lib {
    boost::uuids::uuid newUuid();
}
