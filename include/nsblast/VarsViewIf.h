#pragma once

#include <memory>

#include "proto/nsblast.pb.h"

namespace nsblast::lib {

class VarsViewIf {
public:
    virtual ~VarsViewIf() = default;
    virtual std::shared_ptr<const pb::VarsSnapshot> snapshot() const = 0;
};

} // namespace nsblast::lib
