
#include <iostream>
#include <filesystem>
#include <boost/program_options.hpp>

#include "nsblast/nsblast.h"
#include "nsblast/logging.h"
#include "nsblast/Server.h"

using namespace std;
using namespace nsblast;

int main(int argc, char* argv[]) {
    try {
        locale loc("");
    } catch (const std::exception&) {
        cout << "Locales in Linux are fundamentally broken. Never worked. Never will. Overriding the current mess with LC_ALL=C" << endl;
        setenv("LC_ALL", "C", 1);
    }


    Config config;
    std::string log_level = "info";
    std::string log_file;
    bool trunc_log = true;

    namespace po = boost::program_options;
    po::options_description general("Options");
    po::positional_options_description positionalDescription;

    general.add_options()
        ("help,h", "Print help and exit")
        ("version", "print version string and exit")
        ("db-path,d",
            po::value<string>(&config.db_path)->default_value(config.db_path),
            "Definition file to deploy")
        ("log-level,l",
             po::value<string>(&log_level)->default_value(log_level),
             "Log-level to use; one of 'info', 'debug', 'trace'")
        ("log-file,L",
             po::value<string>(&log_file),
             "Log-file to write a log to. Default is to use the console.")
        ("truncate-log-file,T",
             po::value<bool>(&trunc_log)->default_value(trunc_log),
             "Log-file to write a log to. Default is to use the console.")
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

    po::options_description cmdline_options;
    cmdline_options.add(general).add(odns).add(http);
    po::positional_options_description kfo;
    kfo.add("kubeconfig", -1);
    po::variables_map vm;
    po::store(po::command_line_parser(argc, argv).options(cmdline_options).positional(kfo).run(), vm);
    po::notify(vm);
    if (vm.count("help")) {
        std::cout << filesystem::path(argv[0]).stem().string() << " [options]";
        std::cout << cmdline_options << std::endl;
        return -1;
    }

    if (vm.count("version")) {
        std::cout << filesystem::path(argv[0]).stem().string() << ' '  << NSBLAST_VERSION << endl;
        return -2;
    }

    auto llevel = logfault::LogLevel::INFO;
    if (log_level == "debug") {
        llevel = logfault::LogLevel::DEBUGGING;
    } else if (log_level == "trace") {
        llevel = logfault::LogLevel::TRACE;
    } else if (log_level == "info") {
        ;  // Do nothing
    } else {
        std::cerr << "Unknown log-level: " << log_level << endl;
        return -1;
    }

    if (!log_file.empty()) {
        logfault::LogManager::Instance().AddHandler(
                make_unique<logfault::StreamHandler>(log_file, llevel, trunc_log));
    } else {
        logfault::LogManager::Instance().AddHandler(
                make_unique<logfault::StreamHandler>(clog, llevel));
    }

    LOG_INFO << filesystem::path(argv[0]).stem().string() << ' ' << NSBLAST_VERSION  " starting up. Log level: " << log_level;

    try {        
        Server server{config};
        server.start();
    } catch (const exception& ex) {
        LOG_ERROR << "Caught exception from Server: " << ex.what();
    }
} // mail
