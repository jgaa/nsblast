#pragma once

#include <string>

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


} // ns
