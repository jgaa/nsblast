#pragma once

#include <string>
#include <memory>
#include <optional>
#include <vector>

#include <boost/core/span.hpp>

#include "yahat/HttpServer.h"

namespace nsblast {

using span_t = boost::span<const char>;

static constexpr uint32_t DEFAULT_TTL = 2592000; // 30 days

/*! The applications configuration */
struct Config {
    /*! \name Options */
    ///@{
#ifdef NSBLAST_WITH_SWAGGER
    /// Enable the swagger interface
    bool swagger = true;
#else
    bool dummy_placeholder_ = false;
#endif

    ///@}

    /*! \name Database */
    ///@{
    /// Path to the database directory
    std::string db_path = "/var/lib/nsblast";

    /*! Enable logging of transactions. This is required in cluster-mode.
     *
     *  Levels:
     *    - 1: Replicate DNS data. This is sufficient for slave servers that will
     *         never need to step in (fail-over) and become a master server.
     */
    unsigned db_log_transactions = 1;

    /// Unique node-name in a cluster. Defaults to the hostname of the machine.
    std::string node_name = boost::asio::ip::host_name();
    ///@}

    /*! \name Backup / Restore */
    ///@{
    /// Path to the DB backup location. By default db_path + "/backup".
    std::string backup_path;

    /// Hourly backup interval. 0 to disable.
    size_t hourly_backup_interval = 0;

    /// Tells RocksDB to sync the database before starting a backup
    bool sync_before_backup = true;

    ///@}

    /*! \name Cluster */
    ///@{

    /// Address (IP ':' port) to the gRPC service used for cluster-sync
    std::string cluster_server_addr = "0.0.0.0:10123";

    /// Path to a file containing a gRPC shared secret.
    /// Used to authenticate gRPC replicas.
    std::string cluster_auth_key;

    /// Max transaction queue size for replication agents (server to slave)
    size_t cluster_repl_agent_max_queue_size = 128;

    /// Milliseconds delay from a follower commits a transaction until it notifies the primary
    size_t cluster_followers_update_delay_ = 200;

    /// Milliseconds housekeeping interval in the replication engine.
    size_t cluster_replication_housekeeping_timer_ = 1000;

    /// Seconds for replication keepalive message
    size_t cluster_keepalive_timer = 60;

    /// Seconds from a request until a reply must occur before the delay is treated as a connection problem
    size_t cluster_keepalive_timeout = 120;

    /// Milliseconds to wait before ackowledinging the current trx-id from a follower.
    size_t cluster_ack_delay = 200;


    /*! Role of this server.
     *
     *  - none:    This server is not part of a nsblast cluster
     *
     *  - primary: The one server that handles changes to the zones and
     *             is the source of truth.
     *
     *  - follower: A server that replicates changes from the primary,
     *             but don't allow any locally initiated changes. A follower
     *             is not prepared to act as a fail-over agent.
     */
    std::string cluster_role = "none";

    ///@}

    /*! \name DNS */
    ///@{

    /*! Number of threads for the DNS server.
     *
     *  Note that db and file access is syncronous, so even
     *  if the DNS server is asyncroneous, we need some
     *  extra threads to wait for slow IO to complete.
     */
    size_t num_dns_threads = 6;

    /*! endpoint to the DNS interface.
     *
     *  Can be a hostname or an IP address (ipv4 or IPv6).
     *
     *  If a hostname is provided and it resolves to multiple IP
     *  addresses, nsblast will try to listen to all of them.
     *
     *  If you need to listen to both ipv4 and ipv6, you can specify
     *  a hostname, and add all the ip numbers ipv4 and ipv6 in /etc/hosts
     *  for that hostname.
     */
    std::string dns_endpoint = "localhost";

    /*! The UDP port to listen to */
    std::string dns_udp_port = "53";

    /*! The TCP port to listen to */
    std::string dns_tcp_port = "53";

    /*! The default tts set on Record Sets (including the SOA) if the ttl is not set in the Json Entry when creating the entry. */
    uint32_t default_ttl = DEFAULT_TTL;

    /*! DNS TCP connection idle time for QUERY sessions in seconds */
    uint32_t dns_tcp_idle_time = 3;

    /*! The servers response to QTYPE=ANY on UDP
     *  One of:
     *    - hinfo    Follow RFC 8482's reccomondation and return a specially crafted HINFO record.
     *    - relevant Only return A, AAA, CNAME, MX
     *    - all      Return all RR's
     *
     *  On UDP it is reccommended to use "hinfo" to reduce the risk of the server
     *  being used in DoS / UDP amplification attaks where a short, spoofed
     *  UDP message can generate large UDP messages sent to the spoofed IP address.
     */
    std::string udp_qany_response = "hinfo";

    /*! The servers response to QTYPE=ANY on TCP
     *  One of:
     *    - hinfo    Follow RFC 8482's reccomondation and return a specially crafted HINFO record.
     *    - relevant Only return A, AAA, CNAME, MX
     *    - all      Return all RR's
     */
    std::string tcp_qany_response = "relevant";

    /*! Only allow Srv targets that points to a fqdn managed by this server */
    bool dns_validate_srv_targets_locally = true;

    /*! Max buffer-size for TCP buffers doring zone transfers.
     *
     *  Note: This is a 16 bit value.
     */
    uint16_t dns_max_large_tcp_buffer_size = std::numeric_limits<uint16_t>::max() - 1024;

    /*! Max UDP send-buffer size when OPT RR is in use */
    uint16_t udp_max_buffer_size_with_opt_ = 4096;

    /*! Default time to wait between checking for changes in SOA for slaves
     *
     *  This sttrategy is used as a fall-back if there are no reliable way to get
     *  notifications about zoine changes.
     */
    uint32_t dns_default_zone_pull_interval_ = 600;

    /*! Enable inremental Zone updates via IXFR
     *
     *  This will cause the server to use a little more CPU and disk space
     *  each time a Zone changes, as it neets to store the differences in the
     *  database as well as the new values.
     */
    bool dns_enable_ixfr = true;

    /*! Enable RFC 1996 NOTIFY messages
     *
     *  This causes a master server to send NOTIFY messages over UDP when a
     *  zone is changed.
     */
    bool dns_enable_notify = true;

    /*! Port to send NOTIFY reqests to */
    uint16_t dns_notify_to_port = 53;

    /*! TTL for HINFO response */
    uint32_t dns_hinfo_ttl = 86400; // one day

    ///@}

    /*! \name HTTP */
    ///@{
    /// HTTP configuration
    yahat::HttpConfig http;

    /// Max page size in a REST list reply
    size_t rest_max_page_size = 1000;

    /// Default page size in a REST list reply
    size_t rest_default_page_size = 100;
    ///@}

    /*! \name Authentication */
    ///@{
    /// Enable authentication
    bool enable_auth = true;

    /// The size of authentication tokens/ sessions that can be cached in memory.
    size_t auth_cache_lru_size = 1024 * 1024;
    ///@}

    /*! \name RocksDB */
    ///@{
    /// See the RocksDB documentation for 'db_write_buffer_size'
    size_t rocksdb_db_write_buffer_size = 0;

    /// Calls DBOptions::OptimizeForSmallDb if true.
    bool rocksdb_optimize_for_small_db = true;

    /// Number of threads for flush and compaction. 0 == use default.
    size_t rocksdb_background_threads = 0;
    ///@}
};

// RFC 1035
static constexpr std::uint16_t TYPE_A = 1;
static constexpr std::uint16_t TYPE_NS = 2;
static constexpr std::uint16_t TYPE_CNAME = 5;
static constexpr std::uint16_t TYPE_SOA = 6;
static constexpr std::uint16_t TYPE_WKS = 11;
static constexpr std::uint16_t TYPE_PTR = 11;
static constexpr std::uint16_t TYPE_HINFO = 13;
static constexpr std::uint16_t TYPE_MINFO = 14;
static constexpr std::uint16_t TYPE_MX = 15;
static constexpr std::uint16_t TYPE_TXT = 16;
static constexpr std::uint16_t TYPE_RP = 17;
static constexpr std::uint16_t TYPE_AFSDB = 18;
static constexpr std::uint16_t TYPE_SRV = 33;
static constexpr std::uint16_t TYPE_DHCID = 49;
static constexpr std::uint16_t TYPE_OPENPGPKEY = 61;

// RFC 6891
static constexpr std::uint16_t TYPE_OPT = 41;

static constexpr std::uint16_t QTYPE_IXFR = 251;
static constexpr std::uint16_t QTYPE_AXFR = 252;
//static constexpr std::uint16_t QTYPE_MAILB = 253;
static constexpr std::uint16_t QTYPE_ALL= 255;

static constexpr std::uint16_t CLASS_IN = 1;
//static constexpr std::uint16_t CLASS_CH = 3;
//static constexpr std::uint16_t CLASS_HS = 4;

// rfc 35962
static constexpr std::uint16_t TYPE_AAAA = 28;

static constexpr size_t TXT_SEGMENT_MAX = 255;
static constexpr size_t TXT_MAX = TXT_SEGMENT_MAX * 32; // Our own limit

constexpr char CURRENT_STORAGE_VERSION = 1;

static constexpr size_t MAX_UDP_QUERY_BUFFER = 512;
static constexpr size_t MAX_UDP_QUERY_BUFFER_WITH_OPT = 4096;
static constexpr size_t MAX_TCP_QUERY_LEN = 1024 * 4; // If longer, something smells!
static constexpr size_t MAX_TCP_MESSAGE_BUFFER = 1024 * 12; // Default for normal queries (not zone transfers)
static constexpr size_t MAX_RDATA_SIZE = 1024 * 6;

} // ns
