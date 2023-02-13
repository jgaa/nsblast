#pragma once

#include "nsblast/nsblast.h"
#include "yahat/HttpServer.h"

namespace nsblast {

class ApiEngine {
public:
    ApiEngine(const Config& config);

    void run();

private:
    const Config config_;
};


} // ns
