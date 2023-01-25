#pragma once

#include <string>
#include <memory>
#include <optional>
#include <vector>

namespace nsblast {

struct Config {
    std::string db_path = "/var/lib/nsblast";

    // DNS
    // Number of threads for the DNS server. Note that db and file access
    // is syncronous, so even if the DNS server is asyncroneous, we need some
    // extra threads to wait for slow IO to complete.
    size_t num_dns_threads = 6;
    std::string dns_endpoint;
    std::string dns_port; // Only required for non-standard ports

    // HTTP

    // Number of threads for the API and UI. Note that db and file access
    // is syncronous, so even if the HTTP server is asyncroneous, we need some
    // extra threads to wait for slow IO to complete.
    size_t num_http_threads = 6;

    std::string http_endpoint;
    std::string http_port; // Only required for non-standard ports
    std::string http_tls_key;
    std::string http_tls_cert;

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


} // ns
