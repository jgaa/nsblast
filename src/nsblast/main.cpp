
#include <iostream>
#include <filesystem>
#include <boost/program_options.hpp>

#include "nsblast/nsblast.h"
#include "nsblast/logging.h"
#include "nsblast/DnsEngine.h"

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

    namespace po = boost::program_options;
    po::options_description general("Options");
    po::positional_options_description positionalDescription;

    general.add_options()
        ("help,h", "Print help and exit")
        ("version", "print version string and exit")
        ("db-path,d",
            po::value<string>(&config.db_path_)->default_value(config.db_path_),
            "Definition file to deploy")
        ("log-level,l",
             po::value<string>(&log_level)->default_value(log_level),
             "Log-level to use; one of 'info', 'debug', 'trace'")
    ;

    po::options_description cmdline_options;
    cmdline_options.add(general);
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

    logfault::LogManager::Instance().AddHandler(
                make_unique<logfault::StreamHandler>(clog, llevel));

    LOG_INFO << filesystem::path(argv[0]).stem().string() << ' ' << NSBLAST_VERSION  " starting up. Log level: " << log_level;

    try {
        auto engine = DnsEngine::create(config);
        engine->init();
        //engine.run();
    } catch (const exception& ex) {
        LOG_ERROR << "Caught exception from run: " << ex.what();
    }
} // mail
