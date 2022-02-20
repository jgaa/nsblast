
#include "nsblast/DnsEngine.h"

#include "Db.h"

using namespace std;

namespace nsblast::lib {

class DnsEngineImpl : public DnsEngine {
public:
    DnsEngineImpl(const Config& config)
        : config_{config}, db_{config_} {}

    void init() override {
        db_.init();
    }

    const Config config_;
    Db db_;
};

} // ns

namespace nsblast {

std::unique_ptr<DnsEngine> DnsEngine::create(const Config& config) {
    return make_unique<lib::DnsEngineImpl>(config);
}

} // ns
