
#include <iostream>
#include <filesystem>
#include <boost/program_options.hpp>

#include "nsblast/nsblast.h"
#include "nsblast/logging.h"
#include "nsblast/Server.h"

using namespace std;
using namespace nsblast;

namespace {

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

    namespace po = boost::program_options;
    po::options_description general("Options");
    po::positional_options_description positionalDescription;

    general.add_options()
        ("help,h", "Print help and exit")
        ("version", "print version string and exit")
        ("db-path,d",
            po::value<string>(&config.db_path)->default_value(config.db_path),
            "Definition file to deploy")
        ("log-to-console,C",
             po::value<string>(&log_level_console)->default_value(log_level_console),
             "Log-level to the consolee; one of 'info', 'debug', 'trace'. Empty string to disable.")
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
    po::options_description http("HTTP/API server");
    http.add_options()
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
        ("dns-notify-port",
            po::value<uint16_t>(&config.dns_notify_to_port)->default_value(config.dns_notify_to_port),
           "Port number to send NOTIFY messages to when a zone change")
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

    po::options_description cmdline_options;
    cmdline_options.add(general).add(odns).add(http).add(rocksdb);
    po::positional_options_description kfo;
    kfo.add("kubeconfig", -1);
    po::variables_map vm;
    try {
        po::store(po::command_line_parser(argc, argv).options(cmdline_options).positional(kfo).run(), vm);
        po::notify(vm);
    } catch (const std::exception& ex) {
        cerr << filesystem::path(argv[0]).stem().string()
             << " Failed to parse command-line arguments: " << ex.what() << endl;
        return -1;
    }

    if (vm.count("help")) {
        std::cout << filesystem::path(argv[0]).stem().string() << " [options]";
        std::cout << cmdline_options << std::endl;
        return -2;
    }

    if (vm.count("version")) {
        std::cout << filesystem::path(argv[0]).stem().string() << ' '  << NSBLAST_VERSION << endl;
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

    LOG_INFO << filesystem::path(argv[0]).stem().string() << ' ' << NSBLAST_VERSION  " starting up. Log level: " << log_level;

    try {        
        Server server{config};

        if (vm.count("reset-auth")) {
            server.resetAuth();
            return 0;
        }

        server.start();
    } catch (const exception& ex) {
        LOG_ERROR << "Caught exception from Server: " << ex.what();
    }
} // mail
