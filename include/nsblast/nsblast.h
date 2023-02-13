#pragma once

#include <string>
#include <memory>
#include <optional>
#include <vector>

#include "yahat/HttpServer.h"

namespace nsblast {

/*! The applications configuration */
struct Config {
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
     *  Can be a hostname or an IP address.
     *  If a hostname is provided and it resolves to multiple IP
     *  addresses, nsblast will try to listen to all of them.
     *
     */
    std::string dns_endpoint;

    /*! The port to listen to */
    std::string dns_port; // Only required for non-standard ports

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

} // ns
