#include "Metrics.h"
#include "nsblast/Server.h"

#include "nsblast/logging.h"
#include "nsblast/util.h"

using namespace std;
using namespace std::string_literals;

namespace nsblast::lib {

namespace {

    // We use a log-handler to count error and warning log events.
    class LogHandler : public logfault::Handler {
    public:
        LogHandler(logfault::LogLevel level, Metrics::counter_t *counter)
            : Handler(level),  level_{level}, counter_{counter} {
            if (!counter_) {
                throw std::invalid_argument("counter_ must not be nullptr");
            }
        };

        void LogMessage(const logfault::Message& msg) override {
            if (msg.level_ == level_) {
                counter_->inc();
            }
        }

    private:
        const logfault::LogLevel level_;
        Metrics::counter_t *counter_;
    };
}

Metrics::Metrics(Server& server)
    : server_{server}
{

    // Create the metrics objects
    errors_ = metrics_.AddCounter("nsblast_logged_errors", "Number of errors logged", {});
    warnings_ = metrics_.AddCounter("nsblast_logged_warnings", "Number of warnings logged", {});

    dns_requests_ok_ = metrics_.AddCounter("nsblast_dns_requests", "Number of successful DNS requests", {}, {{"result", "ok"}});
    dns_requests_not_master_ = metrics_.AddCounter("nsblast_dns_requests", "Number of DNS requests that failed because the server is not a master", {}, {{"result", "not_master"}});
    dns_requests_not_found_ = metrics_.AddCounter("nsblast_dns_requests", "Number of DNS requests that failed because the record was not found", {}, {{"result", "not_found"}});
    dns_requests_not_implemented_ = metrics_.AddCounter("nsblast_dns_requests", "Number of DNS requests that failed because the query type is not implemented", {}, {{"result", "not_implemented"}});
    dns_requests_error_ = metrics_.AddCounter("nsblast_dns_requests", "Number of DNS requests that failed with an error", {}, {{"result", "error"}});
    dns_responses_ok_ = metrics_.AddCounter("nsblast_dns_responses", "Number of successful DNS responses", {}, {{"result", "ok"}});
    truncated_dns_responses_ = metrics_.AddCounter("nsblast_truncated_dns_responses", "Number of DNS requests that was truncated", {});
    current_dns_requests_ = metrics_.AddGauge("nsblast_current_dns_requests", "Number of DNS requests currently being processed", {}, {{"state", "current"}});
    asio_worker_threads_ = metrics_.AddGauge("nsblast_worker_threads", "Number of worker threads", {}, {{"kind", "asio"}});

    if (server.isCluster()) {
        if (server.isPrimaryReplicationServer()) {
            cluster_replication_followers_ = metrics_.AddGauge("nsblast_cluster_replication", "Number of followers connected to us", {});
        } else if (server_.isReplicationFollower()) {
            cluster_replication_primaries_ = metrics_.AddGauge("nsblast_cluster_replication", "Number of primaries we are connected to", {});
        }
    }

    logfault::LogManager::Instance().AddHandler(std::make_unique<LogHandler>(logfault::LogLevel::ERROR, errors_));
    logfault::LogManager::Instance().AddHandler(std::make_unique<LogHandler>(logfault::LogLevel::WARN, warnings_));
}


} // ns nsblast::lib
