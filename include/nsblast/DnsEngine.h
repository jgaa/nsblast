#pragma once

#include <memory>

#include "nsblast/nsblast.h"

namespace nsblast {

class DnsEngine {
public:
    static std::unique_ptr<DnsEngine> create(const Config& config);

    virtual void init() = 0;
};

} // ns
