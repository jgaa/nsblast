#pragma once

#include <cassert>

#include "yahat/Metrics.h"

namespace nsblast {
class Server;
}

namespace nsblast::lib {


class Metrics
{
public:
    using gauge_t = yahat::Metrics::Gauge<uint64_t>;
    using counter_t = yahat::Metrics::Counter<uint64_t>;
    using gauge_scoped_t = yahat::Metrics::Scoped<gauge_t>;
    using counter_scoped_t = yahat::Metrics::Scoped<counter_t>;

    Metrics(Server& server);

    yahat::Metrics& metrics() {
        return metrics_;
    }

    // Access to various metrics objects
    counter_t& errors() {
        return *errors_;
    }

    counter_t& warnings() {
        return *warnings_;
    }

    counter_t& dns_requests_ok() {
        return *dns_requests_ok_;
    }

    counter_t& dns_requests_not_master() {
        return *dns_requests_not_master_;
    }

    counter_t& dns_requests_not_found() {
        return *dns_requests_not_found_;
    }

    counter_t& dns_requests_not_implemented() {
        return *dns_requests_not_implemented_;
    }

    counter_t& truncated_dns_responses() {
        return *truncated_dns_responses_;
    }

    counter_t& dns_requests_error() {
        return *dns_requests_error_;
    }

    counter_t& dns_responses_ok() {
        return *dns_responses_ok_;
    }

    gauge_t& cluster_replication_followers() {
        assert(cluster_replication_followers_);
        return *cluster_replication_followers_;
    }

    gauge_t& cluster_replication_primaries() {
        assert(cluster_replication_primaries_);
        return *cluster_replication_primaries_;
    }

    gauge_t& current_dns_requests() {
        return *current_dns_requests_;
    }

    gauge_t& asio_worker_threads() {
        return *asio_worker_threads_;
    }

private:
    Server& server_;
    yahat::Metrics metrics_;

    counter_t * errors_{};
    counter_t * warnings_{};
    counter_t * dns_requests_ok_{};
    counter_t * dns_requests_not_master_{};
    counter_t * dns_requests_not_found_{};
    counter_t * dns_requests_not_implemented_{};
    counter_t * truncated_dns_responses_{};
    counter_t * dns_requests_error_{}; // Potentially probes for vulnerabilities
    counter_t * dns_responses_ok_{};
    gauge_t * cluster_replication_followers_{}; // Only for primary
    gauge_t * cluster_replication_primaries_{}; // Only for followers
    gauge_t * current_dns_requests_{};
    gauge_t * asio_worker_threads_{};
};


} // ns nsblast::lib

