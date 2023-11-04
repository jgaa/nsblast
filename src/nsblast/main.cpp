
#include <iostream>
#include <filesystem>
#include <boost/program_options.hpp>
#include <boost/version.hpp>

#include "nsblast/nsblast.h"
#include "nsblast/logging.h"
#include "nsblast/Server.h"

using namespace std;
using namespace nsblast;

namespace {

//string_view cppStrandard() {
//    if constexpr (__cplusplus == 202101L)
//        return "C++23";
//    if constexpr (__cplusplus == 202002L)
//        return "C++20";
//    if constexpr (__cplusplus == 201703L)
//        return "C++17";

//    return "unknown";
//}

optional<logfault::LogLevel> toLogLevel(string_view name) {
    if (name.empty() || name == "off" || name == "false") {
        return {};
    }

    if (name == "debug") {
        return logfault::LogLevel::DEBUGGING;
    }

    if (name == "trace") {
        return logfault::LogLevel::TRACE;
    }

    return logfault::LogLevel::INFO;
}

}

int main(int argc, char* argv[]) {
    try {
        locale loc("");
    } catch (const std::exception&) {
        cout << "Locales in Linux are fundamentally broken. Never worked. Never will. Overriding the current mess with LC_ALL=C" << endl;
        setenv("LC_ALL", "C", 1);
    }


    Config config;
    config.http.http_basic_auth_realm = "nsBLAST";
    std::string log_level = "info";
    std::string log_level_console = "info";
    std::string log_file;
    bool trunc_log = true;
    bool reset_auth = false;
    int restore_backup_id = 0;
    int validate_backup_id = 0;

    namespace po = boost::program_options;
    po::options_description general("Options");

    general.add_options()
        ("help,h", "Print help and exit")
        ("version", "print version information and exit")
        ("db-path,d",
            po::value<string>(&config.db_path)->default_value(config.db_path),
            "Path to the database directory")
        ("log-to-console,C",
             po::value<string>(&log_level_console)->default_value(log_level_console),
             "Log-level to the console; one of 'info', 'debug', 'trace'. Empty string to disable.")
        ("log-level,l",
            po::value<string>(&log_level)->default_value(log_level),
            "Log-level; one of 'info', 'debug', 'trace'.")
        ("log-file,L",
             po::value<string>(&log_file),
             "Log-file to write a log to. Default is to use only the console.")
        ("truncate-log-file,T",
             po::value<bool>(&trunc_log)->default_value(trunc_log),
             "Log-file to write a log to. Default is to use the console.")
        ("reset-auth",
            "Resets the 'admin' account and the 'nsBLAST' tenant to it's default, initial state."
            "The server will terminate after the changes are made.")
    ;

    po::options_description backup("Backup/Restore");
    backup.add_options()
        ("backup-path",
         po::value<string>(&config.backup_path),
        "Path to the root of the backups directory. Defaults to a directory named 'backup' under the db-path.")
        ("hourly-backup-interval",
         po::value(&config.hourly_backup_interval)->default_value(config.hourly_backup_interval),
         "If set, nsblast will start an automatic backup every # hours.")
        ("sync-before-backup",
         po::value(&config.sync_before_backup)->default_value(config.sync_before_backup),
         "Tells RocksDB to sync the database before starting a backup")
        ("restore-backup",
         po::value(&restore_backup_id),
         "This option will attempt to restore backup id# to the database directory and "
         "then exit the application. USE WITH CARE!")
        ("validate-backup",
         po::value(&validate_backup_id),
         "This option will attempt to validate backup id# "
         "and then exit the application.")
        ("list-backups",
         "This option will attempt to list the available backups "
         "and then exit the application.")
        ;

    po::options_description cluster("Cluster");
    cluster.add_options()
        ("cluster-role",
         po::value(&config.cluster_role)->default_value(config.cluster_role),
         "One of: \"primary\", \"follower\", \"none\" (not part of a nsblast cluster)")
        ("cluster-auth-key",
         po::value(&config.cluster_auth_key)->default_value(config.cluster_auth_key),
         "Path to a binary file containing a key (shared secret) to use for gRPC authentication. "
         "The same key must be used by all the neblast servers in a Simple Cluster. "
         "As an alternative, the environment-valiable NSBLAST_CLUSTER_AUTH_KEY ca be used. In that "
         "case, the variable must contain the secret key itself as plain text."   )
        ("cluster-server-cert",
         po::value(&config.cluster_x509_server_cert)->default_value(config.cluster_x509_server_cert),
         "X509 certificate for the gRPC server")
        ("cluster-server-key",
         po::value(&config.cluster_x509_server_key)->default_value(config.cluster_x509_server_key),
         "X509 key for the gRPC server")
        ("cluster-ca-cert",
         po::value(&config.cluster_x509_ca_cert)->default_value(config.cluster_x509_ca_cert),
         "X509 certificate used to sign the server cert for the gRPC server")
        ("cluster-server-address",
             po::value(&config.cluster_server_addr)->default_value(config.cluster_server_addr),
             "Address to the primary server, or (for the primary), the address/port to listen to.")
        ("cluster-repl-agent-queue-size",
             po::value(&config.cluster_repl_agent_max_queue_size)->default_value(config.cluster_repl_agent_max_queue_size),
             "The number of transactions that can be queued for a follower before the follower is regarded as not being up to date.")
        ;

    po::options_description http("HTTP/API server");
    http.add_options()
#ifdef NSBLAST_WITH_SWAGGER
        ("with-swagger",
         po::value(&config.swagger)->default_value(config.swagger),
         "Enable the '/swagger' endpoint to interactively explore the REST API")
#endif
        ("http-endpoint,H",
            po::value<string>(&config.http.http_endpoint)->default_value(config.http.http_endpoint),
            "HTTP endpoint. For example [::] to listen to all interfaces")
        ("http-port",
            po::value<string>(&config.http.http_port)->default_value(config.http.http_port),
            "HTTP port to listen to. Not required when using port 80 or 443")
        ("http-tls-key",
            po::value<string>(&config.http.http_tls_key)->default_value(config.http.http_tls_key),
            "TLS key for the embedded HTTP server")
        ("http-tls-cert",
            po::value<string>(&config.http.http_tls_cert)->default_value(config.http.http_tls_cert),
            "TLS cert for the embedded HTTP server")
        ("http-num-threads",
            po::value<size_t>(&config.http.num_http_threads)->default_value(config.http.num_http_threads),
            "Threads for the embedded HTTP server")
        ;

    po::options_description odns("DNS server");
    odns.add_options()
        ("dns-endpoint",
            po::value<string>(&config.dns_endpoint)->default_value(config.dns_endpoint),
            "DNS endpoint. For example [::] to listen to all interfaces")
        ("dns-udp-port",
            po::value<string>(&config.dns_udp_port)->default_value(config.dns_udp_port),
            "DNS port to listen to on UDP")
        ("dns-tcp-port",
            po::value<string>(&config.dns_tcp_port)->default_value(config.dns_tcp_port),
            "DNS port to listen to on TCP")
        ("dns-tcp-idle-time",
            po::value<uint32_t>(&config.dns_tcp_idle_time)->default_value(config.dns_tcp_idle_time),
            "Idle-time in seconds for TCP sessions for the DNS protocol")
        ("dns-num-threads",
            po::value<size_t>(&config.num_dns_threads)->default_value(config.num_dns_threads),
            "Threads for the DNS server")
        ("dns-enable-notify",
            po::value<bool>(&config.dns_enable_notify)->default_value(config.dns_enable_notify),
            "A master server sens DNS NOTIFY messages to slave servers when a zone is changed.")
        ("dns-enable-ixfr",
         po::value<bool>(&config.dns_enable_ixfr)->default_value(config.dns_enable_ixfr),
         "Enable IXFR from a master server to it's slaves. This adds aome extra data in the database "
         "for each change that is made to a zone.")
        ("dns-notify-port",
            po::value<uint16_t>(&config.dns_notify_to_port)->default_value(config.dns_notify_to_port),
           "Port number to send NOTIFY messages to when a zone change")
        ("default-nameserver",
          po::value(&config.default_name_servers),
          "Default name-servers to use for new zones. The first definition will be used as the primary."
        )
        ;

    po::options_description rocksdb("RocksDB");
    rocksdb.add_options()
        ("rocksdb-db-write-buffer-size",
         po::value(&config.rocksdb_db_write_buffer_size)->default_value(config.rocksdb_db_write_buffer_size),
         "See the RocksDB documentation for 'db_write_buffer_size'")
        ("rocksdb-optimize-for-small-db",
         po::value(&config.rocksdb_optimize_for_small_db)->default_value(config.rocksdb_optimize_for_small_db),
         "Calls DBOptions::OptimizeForSmallDb if true")
        ("rocksdb-background-threads",
         po::value(&config.rocksdb_background_threads)->default_value(config.rocksdb_background_threads),
         "Number of threads for flush and compaction. 0 == use default.")
        ;

    po::options_description cg("Certificate Generator");
    cg.add_options()
        ("create-cert-subject",
         po::value(&config.ca_chain.server_subjects),
         "Add a subject to a self-signed server cert. "
         "If this option is given, the application will generate self-signed certificate(s) and exit.")
        ("create-certs-num-clients",
         po::value(&config.ca_chain.num_clients)->default_value(config.ca_chain.num_clients),
         "Specifies how many client certs to generate. "
         "Require cert-subject to also be provided.")
        ("create-certs-path",
         po::value(&config.ca_chain.path)->default_value(config.ca_chain.path),
         "Specifies where to generate the certs. "
         "Require cert-subject to also be provided.")
        ;

    const auto appname = filesystem::path(argv[0]).stem().string();
    po::options_description cmdline_options;
    cmdline_options.add(general).add(backup).add(cluster).add(odns).add(http).add(rocksdb).add(cg);
    po::variables_map vm;
    try {
        po::store(po::command_line_parser(argc, argv).options(cmdline_options).run(), vm);
        po::notify(vm);
    } catch (const std::exception& ex) {
        cerr << appname
             << " Failed to parse command-line arguments: " << ex.what() << endl;
        return -1;
    }

    if (vm.count("help")) {
        cout << appname << " [options]";
        cout << cmdline_options << std::endl;
        return -2;
    }

    if (vm.count("version")) {
        cout << Server::getVersionInfo();
        return -3;
    }

    if (!log_file.empty()) {
        if (auto level = toLogLevel(log_level)) {
            logfault::LogManager::Instance().AddHandler(
                make_unique<logfault::StreamHandler>(log_file, *level, trunc_log));
        }
    }

    if (auto level = toLogLevel(log_level_console)) {
        logfault::LogManager::Instance().AddHandler(
                make_unique<logfault::StreamHandler>(clog, *level));
    }

    if (!config.ca_chain.server_subjects.empty()) {
        LOG_INFO << appname << ' ' << NSBLAST_VERSION
                 << " generating self-signed certs in : "
                 << config.ca_chain.path;

        try {
            createCaChain(config.ca_chain);
        } catch (const exception& ex) {
            LOG_ERROR << "Caught exception: " << ex.what();
            return -4;
        }

        return 0;
    }

    LOG_INFO << appname << ' ' << NSBLAST_VERSION  " starting up. Log level: " << log_level;

    try {        
        Server server{config};

        if (vm.count("reset-auth")) {
            server.resetAuth();
            return 0;
        }

        if (vm.count("list-backups")) {
            server.startBackupMgr(false);
            server.listBackups();
            return 0;
        }

        if (restore_backup_id) {
            server.startBackupMgr(false);
            server.restoreBackup(restore_backup_id);
            return 0;
        }

        if (validate_backup_id) {
            server.startBackupMgr(false);
            server.validateBackup(validate_backup_id);
            return 0;
        }

        server.start();
    } catch (const exception& ex) {
        LOG_ERROR << "Caught exception from Server: " << ex.what();
    }
} // main
