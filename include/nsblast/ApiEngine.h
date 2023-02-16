#pragma once

#include "nsblast/nsblast.h"
#include "nsblast/ResourceIf.h"
#include "yahat/HttpServer.h"


namespace nsblast {

namespace lib {
    class RestApi;
}

class ApiEngine {
public:
    ApiEngine(const Config& config);

    void initRocksdb();

    void run();

private:
    const Config config_;
    std::shared_ptr<ResourceIf> resource_;
};


} // ns
