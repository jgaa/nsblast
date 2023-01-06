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


class Rr {
public:
    using data_t = std::string_view; // for now
    using key_t = std::string_view;

    Rr(data_t data) : data_{data} {}

protected:
    const data_t data_;
};

class RrA : public Rr {
public:

};

// Is soa a rr?
class Soa {
public:
    using key_t = std::string_view;


};

class Zone {
public:
    using key_t = std::string_view;

};

/*! Internal representation of a dns entry
 *
 *  This represent all the information we get when we query for
 *  a fqdn. It's designed to be used for caching, for binary
 *  storage in a database and for binary transport to other dns servers.
 *
 *  The data is stored without padding, with the most relevant (frequently wanted)
 *  data first to be CPU-cache friendly.
 *
 *  The individual RR's are stored in the same format they will normally be returned
 *  in response to a DNS query.
 */
class DataRow {
public:
    DataRow() = default;

    // data
    // iterator over rr's

    // Buffer layout.
    // - version: Single octet specifying the version of format of the data.
    // - bitflags to identify what kind of RR's that are present
    //      (to avoid searching for non-existing entries)
    //   Starts with a octet defining the length in octets of the bitfield.
    //     Allows us to adapt to any number of RR's in the future.
    // - Sequence of RR's, sorted by popularity. The most common first.
    //   - type
    //   - total size in octets, including type and size
    //   - name as an array of binary DNS names (0ne octet size, then the segment).
    //     Case is preserved.
    //   - see RFC...

    std::vector<unsigned char> buffer_;
};

class Entry {
public:
    using ptr_t = std::shared_ptr<Entry>;
    using key_t = std::string_view;

private:
    DataRow dr_;
};

/*! Interface for the DNS service and the API service. */
class DnsIf {
public:
    DnsIf() = default;
    virtual ~DnsIf() = default;

    virtual Entry::ptr_t lookup(const Entry::key_t& fqdn);
};

} // ns
