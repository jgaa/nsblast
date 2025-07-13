#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>

namespace nsblast {

struct CreateCaChainOptions {

    unsigned lifetime_days_ca() const noexcept {
        return 365 * std::max<unsigned>(num_years_ca, num_years_certs);
    }

    unsigned lifetime_days_certs() const noexcept {
        return 365 * std::max<unsigned>(num_years_certs, 1);
    }

    /// Path to where the files are created. Defaults to cwd.
    std::filesystem::path path = std::filesystem::current_path();

    /// The IP address or hostname/fqdn the server cert is valid for
    /// There will be generated one server cert for each subject
    std::vector<std::string> server_subjects;

    /// Number of client certs to create
    unsigned num_clients = 3;

    /// Validity period for the CA and server certs in years
    unsigned num_years_certs = 5;
    unsigned num_years_ca = 10;
    unsigned key_bytes = 4096;

    // Templates for file-names
    std::string ca_template = "ca-{kind}.pem";

    // Name template for server cert/key
    std::string servers_template = "server{count}-{kind}.pem";

    // Name template for client cert/key
    std::string client_template = "client{count}-{kind}.pem";

    std::string ca_name = "Ca Authority";
};

void createCaChain(const CreateCaChainOptions& options);



} // ns
