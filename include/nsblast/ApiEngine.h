#pragma once

#include "nsblast/nsblast.h"
#include "nsblast/ResourceIf.h"
#include "yahat/HttpServer.h"


namespace nsblast {

namespace lib {
    class RestApi;
    class SlaveMgr;
}

class ApiEngine {
public:
    ApiEngine(const Config& config);

    void initRocksdb();

    void run();

    auto& resource() {
        assert(resource_);
        return *resource_;
    }

    auto& slaveMgr() {
        assert(slave_mgr_);
        return *slave_mgr_;
    }

    auto& config() const noexcept {
        return config_;
    }

private:
    const Config config_;
    std::shared_ptr<ResourceIf> resource_;
    std::shared_ptr<lib::SlaveMgr> slave_mgr_;
};


} // ns
