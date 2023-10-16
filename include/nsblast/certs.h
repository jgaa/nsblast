#pragma once

#include <string>
#include <vector>
#include <filesystem>

namespace nsblast {

struct CreateCaChainOptions {

    /// Path to where the files are created. Defaults to cwd.
    std::filesystem::path path = std::filesystem::current_path();

    /// The IP address or hostname/fqdn the server cert is valid for
    /// There will be generated one server cert for each subject
    std::vector<std::string> server_subjects;

    /// Number of client certs to create
    unsigned num_clients = 3;

    unsigned lifetime_days_ca = 356 * 10;
    unsigned lifetime_days_certs = 356;
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
