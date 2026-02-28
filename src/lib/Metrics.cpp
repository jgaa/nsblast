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

        void LogMessage(const logfault::Message& msg) noexcept override {
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
    request_latency_ok_ = metrics_.AddSummary("nsblast_request_latency", "Request latency", {}, {{"result", "ok"}}, {{0.5, 0.9, 0.95, 0.99}});

    backup_already_running_ = metrics_.AddCounter("nsblast_backup_already_running", "Number of backup requests that was already running", {});
    backups_ok_ = metrics_.AddCounter("nsblast_backups", "Number of successful backups", {}, {{"result", "ok"}});
    backups_failed_ = metrics_.AddCounter("nsblast_backups", "Number of failed backups", {}, {{"result", "failed"}});
    backup_duration_ = metrics_.AddSummary("nsblast_backup_duration", "Duration of backups", {}, {}, {{0.5, 0.9, 0.95, 0.99}});
    backup_state_ = metrics_.AddStateset<2>("nsblast_backup_state", "Backup state", {}, {}, {"idle", "running"});
    backup_state_->setExclusiveState(BackupState::IDLE);

    dynip_updates_ok_ = metrics_.AddCounter("nsblast_dynip_updates", "Number of DynIP update responses", {}, {{"result", "good"}});
    dynip_updates_nochg_ = metrics_.AddCounter("nsblast_dynip_updates", "Number of DynIP update responses", {}, {{"result", "nochg"}});
    dynip_updates_badauth_ = metrics_.AddCounter("nsblast_dynip_updates", "Number of DynIP update responses", {}, {{"result", "badauth"}});
    dynip_updates_nohost_ = metrics_.AddCounter("nsblast_dynip_updates", "Number of DynIP update responses", {}, {{"result", "nohost"}});
    dynip_updates_badip_ = metrics_.AddCounter("nsblast_dynip_updates", "Number of DynIP update responses", {}, {{"result", "badip"}});
    dynip_updates_numhost_ = metrics_.AddCounter("nsblast_dynip_updates", "Number of DynIP update responses", {}, {{"result", "numhost"}});
    dynip_updates_notfqdn_ = metrics_.AddCounter("nsblast_dynip_updates", "Number of DynIP update responses", {}, {{"result", "notfqdn"}});
    dynip_updates_disabled_ = metrics_.AddCounter("nsblast_dynip_updates", "Number of DynIP update responses", {}, {{"result", "disabled"}});
    dynip_updates_error_ = metrics_.AddCounter("nsblast_dynip_updates", "Number of DynIP update responses", {}, {{"result", "error"}});
    dynip_update_latency_ = metrics_.AddSummary("nsblast_dynip_update_latency", "DynIP update response latency", "seconds", {}, {{0.5, 0.9, 0.95, 0.99}});

#ifdef NSBLAST_CLUSTER
    // Metrics are constructed before runtime role is initialized from vars.
    // Expose cluster gauges unconditionally in cluster builds so follower startup
    // never dereferences null gauges.
    auto *cluster_replication = metrics_.AddGauge(
        "nsblast_cluster_replication",
        "Number of cluster peers connected to us",
        {});
    cluster_replication_followers_ = cluster_replication;
    cluster_replication_primaries_ = cluster_replication;
    cluster_replication_in_sync_ = metrics_.AddGauge(
        "nsblast_cluster_replication_in_sync",
        "Whether this follower is in sync with its primary (1=true, 0=false)",
        {});
#endif

    logfault::LogManager::Instance().AddHandler(std::make_unique<LogHandler>(logfault::LogLevel::ERROR, errors_));
    logfault::LogManager::Instance().AddHandler(std::make_unique<LogHandler>(logfault::LogLevel::WARN, warnings_));
}

void Metrics::incDynipUpdate(std::string_view result) {
    if (result == "good") {
        dynip_updates_ok_->inc();
    } else if (result == "nochg") {
        dynip_updates_nochg_->inc();
    } else if (result == "badauth") {
        dynip_updates_badauth_->inc();
    } else if (result == "nohost") {
        dynip_updates_nohost_->inc();
    } else if (result == "badip") {
        dynip_updates_badip_->inc();
    } else if (result == "numhost") {
        dynip_updates_numhost_->inc();
    } else if (result == "notfqdn") {
        dynip_updates_notfqdn_->inc();
    } else if (result == "!disabled" || result == "disabled") {
        dynip_updates_disabled_->inc();
    } else {
        dynip_updates_error_->inc();
    }
}


} // ns nsblast::lib
