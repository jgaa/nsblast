
#include "nsblast/DnsEngine.h"
#include "HttpServer.h"
#include "Db.h"
#include "swagger_res.h"

using namespace std;

namespace nsblast::lib {

class DnsEngineImpl : public DnsEngine {
public:
    DnsEngineImpl(const Config& config)
        : config_{config}, db_{config_}, http_{config_} {}

    void init() override {
        db_.init();

        http_.addRoute("/api/swagger",
                       make_shared<EmbeddedHandler<embedded::resource_t>>(
                           embedded::swagger_files_,
                           "/api/swagger"));
    }

    void run() override {
        http_future_ = http_.start();

        // Wait for the thing to stop
        http_future_.get();
    }

private:
    const Config config_;
    Db db_;
    HttpServer http_;

    future<void> http_future_;
};

} // ns

namespace nsblast {

std::unique_ptr<DnsEngine> DnsEngine::create(const Config& config) {
    return make_unique<lib::DnsEngineImpl>(config);
}

} // ns
