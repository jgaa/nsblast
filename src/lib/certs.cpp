#include "nsblast/certs.h"

#include <string>
#include <string_view>
#include <format>
#include <filesystem>
#include <iostream>
#include <fstream>

#include <boost/algorithm/string.hpp>

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include "nsblast/logging.h"
#include "nsblast/util.h"

using namespace std;
using namespace std::string_literals;

namespace nsblast {

namespace {

void setIssuerOrg(X509 *cert, const std::string& issuerOrg) {
    if (auto ca_name = X509_NAME_new()) {

        lib::ScopedExit cleaner{[ca_name] {
            X509_NAME_free(ca_name);
        }};

        X509_NAME_add_entry_by_txt(
            ca_name, "O", MBSTRING_ASC,
            reinterpret_cast<const unsigned char *>(issuerOrg.c_str()), -1, -1, 0);
        X509_set_issuer_name(cert, ca_name);
        return;
    }
    throw runtime_error{"X509_get_issuer_name"};
}

template <typename T>
void setSubjects(X509 *cert, const T& subjects) {
    if (auto name = X509_NAME_new()) {

        lib::ScopedExit cleaner{[name] {
            X509_NAME_free(name);
        }};

        int loc = 0;

        for(const auto [subj, section] : subjects) {
            X509_NAME_add_entry_by_txt(
                name, section.c_str(), MBSTRING_ASC,
                reinterpret_cast<const unsigned char *>(subj.data()), subj.size(), loc++, 0);
        }

        X509_set_subject_name(cert, name);
        return;
    }
    throw runtime_error{"X509_get_subject_name"};
}

auto openFile(const std::filesystem::path& path) {

    struct Closer {
        void operator()(FILE *fp) const {
            fclose(fp);
        }
    };

    if (auto fp = fopen(path.c_str(), "wb")) {
        return unique_ptr<FILE, Closer>(fp);
    }

    const auto why = strerror(errno);

    throw runtime_error{format("Failed to open file {} for binary write: {}",
                               path.string(), why)};
}

auto openFile(std::filesystem::path path, const std::string& name) {
    path /= name;

    LOG_DEBUG << "Creating file: " << path;
    return openFile(path);
}


// https://stackoverflow.com/questions/60476336/programmatically-generate-a-ca-certificate-with-openssl-in-c
void addExt(X509 *cert, int nid, const char *value)
{
    X509V3_CTX ctx = {};

    /* This sets the 'context' of the extensions. */
    /* No configuration database */
    X509V3_set_ctx_nodb(&ctx);

    /* Issuer and subject certs: both the target since it is self signed,
     * no request and no CRL
     */
    X509V3_set_ctx(&ctx, cert, cert, NULL, NULL, 0);
    if (auto ex = X509V3_EXT_conf_nid(NULL, &ctx, nid, value)) {
        X509_add_ext(cert,ex,-1);
        X509_EXTENSION_free(ex);
    } else {
        throw runtime_error{"X509V3_EXT_conf_nid failed."};
    }
}

struct X509_Deleter {
    void operator()(X509* x) const {
        X509_free(x);
    }
};

using X509_Ptr = std::unique_ptr<X509, X509_Deleter>;

struct EVP_PKEY_Deleter {
    void operator()(EVP_PKEY* p) const { EVP_PKEY_free(p); }
};

using EVP_PKEY_Ptr = std::unique_ptr<EVP_PKEY, EVP_PKEY_Deleter>;

template <typename T>
std::pair<X509_Ptr, EVP_PKEY_Ptr> createCert(
    const std::string& caName,
    long serial,
    unsigned lifetimeDays,
    unsigned keyBytes,
    const T& subjects) {

    if (auto cert = X509_new()) {

        X509_Ptr rcert{cert};

        // Set cert serial
        ASN1_INTEGER_set(X509_get_serialNumber(cert), serial);

        // set cert version
        X509_set_version(cert, X509_VERSION_3);

        // Set lifetime
        // From now
        X509_gmtime_adj(X509_get_notBefore(cert), 0);

        // To ...
        X509_gmtime_adj(X509_get_notAfter(cert), 60 * 60 * 24 * lifetimeDays);

        setIssuerOrg(cert, caName);

        setSubjects(cert, subjects);

        EVP_PKEY_Ptr rkey;

        // Generate and set a RSA private key
        if (auto rsa_key = EVP_PKEY_new()) {
            rkey.reset(rsa_key);

            if (auto ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL)) {
                lib::ScopedExit ctx_cleanup{[&ctx] {
                    EVP_PKEY_CTX_free(ctx);
                }};

                EVP_PKEY_keygen_init(ctx);
                EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, keyBytes);
                if (EVP_PKEY_keygen(ctx, &rsa_key) <= 0) {
                    throw runtime_error{"Error generating EVP_PKEY_RSA key for the certificate."};
                }
            } else {
                throw runtime_error{"EVP_PKEY_CTX_new_id"};
            }

            X509_set_pubkey(cert, rsa_key);
        } else {
            throw runtime_error{"EVP_PKEY_new()"};
        }

        return make_pair(std::move(rcert), std::move(rkey));
    }

    throw runtime_error{"X509_new failed"};
}

std::pair<X509_Ptr, EVP_PKEY_Ptr> createCaCert(
    const std::string& caName,
    unsigned lifetimeDays,
    unsigned keyBytes,
    const std::filesystem::path& keyPath,
    const std::filesystem::path& certPath) {

    const array<pair<const string&, const string&>, 1> s = {
        make_pair(caName, "O"s),
    };

    auto [cert, key] = createCert(caName, 1, lifetimeDays, keyBytes, s);

    addExt(cert.get(), NID_basic_constraints, "critical,CA:TRUE");
    addExt(cert.get(), NID_key_usage, "critical,keyCertSign,cRLSign");
    addExt(cert.get(), NID_subject_key_identifier, "hash");

    if (!X509_sign(cert.get(), key.get(), EVP_sha256())) {
        throw runtime_error{"Error signing CA certificate."};
    }

    // Write the CA cert
    PEM_write_X509(openFile(certPath).get(), cert.get());

    // Write the CA key, if we were given a path to it
    if (!keyPath.empty()) {
        PEM_write_PrivateKey(openFile(keyPath).get(),
                             key.get(), NULL, NULL, 0, 0, NULL);
    }

    return make_pair(std::move(cert), std::move(key));
}

void createClientCert(const std::string& caName,
                      const std::string& name,
                      EVP_PKEY *ca_rsa_key,
                      long serial,
                      unsigned lifetimeDays,
                      unsigned keyBytes,
                      const std::filesystem::path& keyPath,
                      const std::filesystem::path& certPath) {

    const array<pair<const string&, const string&>, 2> s = {
        make_pair(caName, "O"s),
        make_pair(name, "CN"s)
    };

    auto [cert, key] = createCert(caName, serial, lifetimeDays, keyBytes, s);

    if (!X509_sign(cert.get(), ca_rsa_key, EVP_sha256())) {
        throw runtime_error{format("Error signing client certificate {}.", serial)};
    }

    PEM_write_PrivateKey(openFile(keyPath).get(), key.get(), NULL, NULL, 0, 0, NULL);
    PEM_write_X509(openFile(certPath).get(), cert.get());
}

void createServerCert(const std::string& caName,
                      const std::string& subject,
                      EVP_PKEY *ca_rsa_key,
                      long serial,
                      unsigned lifetimeDays,
                      unsigned keyBytes,
                      const std::filesystem::path& keyPath,
                      const std::filesystem::path& certPath) {

    const array<pair<const string&, const string&>, 2> s = {
        make_pair(caName, "O"s),
        make_pair(subject, "CN"s)
    };

    auto [cert, key] = createCert(caName, serial, lifetimeDays, keyBytes, s);

    if (!X509_sign(cert.get(), ca_rsa_key, EVP_sha256())) {
        throw runtime_error{format("Error signing client certificate {}.", serial)};
    }

    PEM_write_PrivateKey(openFile(keyPath).get(), key.get(), NULL, NULL, 0, 0, NULL);
    PEM_write_X509(openFile(certPath).get(), cert.get());
}

string expand(string what, bool kindIsCert, unsigned count = 0) {
    static const array<string_view, 2> kind = {"cert", "key"};

    boost::replace_all(what, "{kind}", kind[kindIsCert ? 0 : 1]);
    boost::replace_all(what, "{count}", to_string(count));

    return what;
}

} // anon ns

void createCaChain(const CreateCaChainOptions& options)
{
    // Create the CA
    auto ca_cert_path = options.path;
    ca_cert_path /= expand(options.ca_template, true);
    auto [ca_cert, ca_key] =  createCaCert(options.ca_name, options.lifetime_days_ca,
                                          options.key_bytes, {}, ca_cert_path);

    // Serial numbers for the certs we make
    unsigned serial = 1; // CA has serial 1

    unsigned scount = 1;

    for(const auto& subject: options.server_subjects) {

        auto kpath = options.path;
        auto cpath = kpath;

        cpath /= expand(options.servers_template, true, scount);
        kpath /= expand(options.servers_template, false, scount);
        ++scount;

        createServerCert(options.ca_name, subject, ca_key.get(), ++serial,
                         options.lifetime_days_certs, options.key_bytes, kpath, cpath);
    }


    for(auto i = 1; i <= options.num_clients; ++i) {

        const auto name = format("Client Cert {}", i);

        auto kpath = options.path;
        auto cpath = kpath;
        cpath /= expand(options.client_template, true, i);
        kpath /= expand(options.client_template, false, i);

        createClientCert(options.ca_name, name, ca_key.get(), ++serial,
                         options.lifetime_days_certs, options.key_bytes, kpath, cpath);
    }
}

} // ns
