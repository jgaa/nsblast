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
    /// Enable the swagger interface
    bool swagger = true;

    ///@}

    /*! \name Database */
    ///@{
    /// Path to the database directory
    std::string db_path = "/var/lib/nsblast";


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
    std::string dns_udp_port = "domain";

    /*! The TCP port to listen to */
    std::string dns_tcp_port = "domain";

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

    /*! Default time to wait between checking for changes in SOA for slaves
     *
     *  This sttrategy is used as a fall-back if there are no reliable way to get
     *  notifications about zoine changes.
     */
    uint32_t dns_default_zone_pull_interval_ = 600;

    ///@}

    /*! \name HTTP */
    ///@{
    /// HTTP configuration
    yahat::HttpConfig http;
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

// RFC 6891
static constexpr std::uint16_t TYPE_OPT = 41;

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
static constexpr size_t MAX_TCP_QUERY_LEN = 1024 * 4; // If longer, something smells!
static constexpr size_t MAX_TCP_MESSAGE_BUFFER = 1024 * 6; // Default for normal queries (not zone transfers)

} // ns
