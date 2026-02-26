
#include <unistd.h>

#include <iostream>
#include <filesystem>
#include <fstream>
#include <array>
#include <thread>
#include <chrono>
#include <unordered_map>
#include <unordered_set>
#include <boost/program_options.hpp>
#include <boost/json.hpp>
#include <boost/version.hpp>
#include <boost/uuid/uuid_io.hpp>

#include "nsblast/nsblast.h"
#include "nsblast/logging.h"
#include "nsblast/LogCapture.h"
#include "nsblast/Server.h"
#include "nsblast/Vars.h"
#include "nsblast/ResourceIf.h"
#include "nsblast/util.h"
#include "nsblast/DnsMessages.h"
#include "proto/nsblast.pb.h"
#include "RocksDbResource.h"

using namespace std;
using namespace nsblast;

namespace {

optional<logfault::LogLevel> toLogLevel(string_view name) {
    if (name.empty() || name == "off" || name == "false") {
        return {};
    }

    const auto level = logging::parseLogLevel(name, logfault::LogLevel::INFO);
    if (level == logfault::LogLevel::DISABLED) {
        return {};
    }

    return level;
}

boost::json::array& asArray(boost::json::object& v, string_view name) {
    if (auto entry = v.if_contains(name)) {
        assert(entry->is_array());
        return entry->as_array();
    }

    auto [it, _] = v.emplace(name, boost::json::array{});
    return it->value().as_array();
}

void rrToJson(const span_t buffer, const lib::Rr& rr, boost::json::object& obj) {
    switch(rr.type()) {
    case TYPE_A:
        asArray(obj, "a").emplace_back(lib::RrA(buffer, rr.offset()).address().to_string());
        break;
    case TYPE_AAAA:
        asArray(obj, "aaaa").emplace_back(lib::RrA(buffer, rr.offset()).address().to_string());
        break;
    case TYPE_NS:
        asArray(obj, "ns").emplace_back(lib::RrNs(buffer, rr.offset()).ns().string());
        break;
    case TYPE_CNAME:
        obj["cname"] = lib::RrCname(buffer, rr.offset()).cname().string();
        break;
    case TYPE_SOA: {
        boost::json::object o;
        const lib::RrSoa soa(buffer, rr.offset());
        o["mname"] = soa.mname().string();
        o["rname"] = soa.rname().string();
        o["email"] = soa.email();
        o["serial"] = soa.serial();
        o["refresh"] = soa.refresh();
        o["retry"] = soa.retry();
        o["expire"] = soa.expire();
        o["minimum"] = soa.minimum();
        obj["soa"] = std::move(o);
    } break;
    case TYPE_PTR:
        asArray(obj, "ptr").emplace_back(lib::RrPtr(buffer, rr.offset()).ptrdname().string());
        break;
    case TYPE_MX: {
        boost::json::object o;
        const lib::RrMx mx(buffer, rr.offset());
        o["host"] = mx.host().string();
        o["priority"] = mx.priority();
        asArray(obj, "mx").emplace_back(std::move(o));
    } break;
    case TYPE_TXT:
        asArray(obj, "txt").emplace_back(lib::RrTxt(buffer, rr.offset()).string());
        break;
    case TYPE_SRV: {
        boost::json::object o;
        const lib::RrSrv srv(buffer, rr.offset());
        o["target"] = srv.target().string();
        o["priority"] = srv.priority();
        o["weight"] = srv.weight();
        o["port"] = srv.port();
        asArray(obj, "srv").emplace_back(std::move(o));
    } break;
    case TYPE_AFSDB: {
        boost::json::object o;
        const lib::RrAfsdb ad(buffer, rr.offset());
        o["host"] = ad.host().string();
        o["subtype"] = ad.subtype();
        asArray(obj, "afsdb").emplace_back(std::move(o));
    } break;
    case TYPE_RP: {
        boost::json::object o;
        const lib::RrRp rp(buffer, rr.offset());
        o["mbox"] = lib::RrSoa::ToEmail(rp.mbox().string());
        o["txt"] = rp.txt().string();
        obj["rp"] = std::move(o);
    } break;
    case TYPE_HINFO: {
        boost::json::object o;
        const lib::RrHinfo hi(buffer, rr.offset());
        o["cpu"] = hi.cpu();
        o["os"] = hi.os();
        obj["hinfo"] = std::move(o);
    } break;
    case TYPE_DHCID:
        obj["dhcid"] = lib::Base64Encode(rr.rdata());
        break;
    case TYPE_OPENPGPKEY:
        obj["openpgpkey"] = lib::Base64Encode(rr.rdata());
        break;
    default:
        asArray(obj, format("#{}", rr.type())).emplace_back(lib::Base64Encode(rr.rdata()));
    }
}

boost::json::object entryToJson(const lib::Entry& entry) {
    boost::json::object o;
    bool has_label = false;
    for(const auto& rr : entry) {
        if (!has_label) {
            o["fqdn"] = rr.labels().string();
            o["ttl"] = rr.ttl();
            has_label = true;
        }

        rrToJson(entry.buffer(), rr, o);
    }

    return o;
}

size_t findBestZoneIx(const vector<boost::json::object>& zones, string_view fqdn) {
    optional<size_t> best_ix;
    size_t best_len = 0;

    for(size_t i = 0; i < zones.size(); ++i) {
        const auto zone = string_view{zones[i].at("zone").as_string()};
        if (zone.size() >= best_len && lib::isSameZone(zone, fqdn)) {
            best_ix = i;
            best_len = zone.size();
        }
    }

    return best_ix.value_or(numeric_limits<size_t>::max());
}

size_t findBestZoneIx(const vector<string>& zones, string_view fqdn) {
    optional<size_t> best_ix;
    size_t best_len = 0;

    for(size_t i = 0; i < zones.size(); ++i) {
        const auto zone = string_view{zones[i]};
        if (zone.size() >= best_len && lib::isSameZone(zone, fqdn)) {
            best_ix = i;
            best_len = zone.size();
        }
    }

    return best_ix.value_or(numeric_limits<size_t>::max());
}

void dumpZones(Server& server, const filesystem::path& path) {
    using key_class_t = lib::ResourceIf::RealKey::Class;

    auto trx = server.resource().transaction();
    vector<boost::json::object> zones;

    lib::ResourceIf::RealKey zkey{"", key_class_t::ZONE};
    trx->iterate(zkey, [&zones](lib::ResourceIf::TransactionIf::key_t key, span_t value) {
        boost::json::object zone;
        const auto fqdn = key.dataAsString();
        zone["zone"] = fqdn;
        zone["rrsets"] = boost::json::array{};

        pb::Zone meta;
        if (meta.ParseFromArray(value.data(), static_cast<int>(value.size()))) {
            if (meta.has_id()) {
                zone["id"] = meta.id();
            }
            if (meta.has_tenantid()) {
                zone["tenantId"] = meta.tenantid();
            }
            if (meta.has_status()) {
                zone["status"] = pb::ZoneStatus_Name(meta.status());
            }
        }

        zones.emplace_back(std::move(zone));
        return true;
    }, lib::ResourceIf::Category::ACCOUNT);

    lib::ResourceIf::RealKey ekey{"", key_class_t::ENTRY};
    boost::json::array orphan_rrsets;
    trx->iterate(ekey, [&zones, &orphan_rrsets](lib::ResourceIf::TransactionIf::key_t key, span_t value) {
        const auto fqdn = key.dataAsString();
        const auto zone_ix = findBestZoneIx(zones, fqdn);
        if (zone_ix == numeric_limits<size_t>::max()) {
            orphan_rrsets.emplace_back(entryToJson(lib::Entry{value}));
            return true;
        }

        zones[zone_ix]["rrsets"].as_array().emplace_back(entryToJson(lib::Entry{value}));
        return true;
    });

    boost::json::object root;
    root["zoneCount"] = zones.size();
    root["zones"] = boost::json::array{};
    for(auto& zone : zones) {
        root["zones"].as_array().emplace_back(std::move(zone));
    }
    root["orphanRrsetCount"] = orphan_rrsets.size();
    root["orphanRrsets"] = std::move(orphan_rrsets);

    if (auto dir = path.parent_path(); !dir.empty()) {
        filesystem::create_directories(dir);
    }

    ofstream out(path, ios::trunc | ios::out);
    if (!out.is_open()) {
        throw runtime_error{"Failed to open dump file: " + path.string()};
    }

    out << boost::json::serialize(root) << '\n';
}

struct RepairZoneIndexesResult {
    size_t zones_discovered = 0;
    size_t zone_records_written = 0;
    size_t tenant_zone_indexes_written = 0;
    size_t rr_indexes_written = 0;
    size_t orphan_rrsets = 0;
};

struct EmitFullSyncStreamResult {
    uint64_t start_after_trxid = 0;
    uint64_t first_emitted_trxid = 0;
    uint64_t last_emitted_trxid = 0;
    size_t emitted_transactions = 0;
    size_t emitted_parts = 0;
};

RepairZoneIndexesResult repairZoneIndexes(Server& server) {
    using key_class_t = lib::ResourceIf::RealKey::Class;

    auto trx = server.resource().transaction();
    unordered_map<string, pb::Zone> existing_zone_meta;

    // Keep any existing IDs if zone metadata is present.
    {
        lib::ResourceIf::RealKey zkey{"", key_class_t::ZONE};
        trx->iterate(zkey, [&existing_zone_meta](lib::ResourceIf::TransactionIf::key_t key, span_t value) {
            pb::Zone zone;
            if (zone.ParseFromArray(value.data(), static_cast<int>(value.size()))) {
                existing_zone_meta.emplace(key.dataAsString(), std::move(zone));
            }
            return true;
        }, lib::ResourceIf::Category::ACCOUNT);
    }

    unordered_map<string, string> zone_to_tenant;
    lib::ResourceIf::RealKey ekey{"", key_class_t::ENTRY};
    trx->iterate(ekey, [&zone_to_tenant](lib::ResourceIf::TransactionIf::key_t key, span_t value) {
        const auto fqdn = key.dataAsString();
        const lib::Entry entry{value};
        if (!entry.hasSoa()) {
            return true;
        }

        string tenant_id = boost::uuids::to_string(lib::nsblastTenantUuid);
        if (auto tid = entry.tenantId()) {
            tenant_id = boost::uuids::to_string(*tid);
        }

        zone_to_tenant.emplace(lib::toLower(fqdn), lib::toLower(tenant_id));
        return true;
    }, lib::ResourceIf::Category::ENTRY);

    RepairZoneIndexesResult result;
    result.zones_discovered = zone_to_tenant.size();

    // Rebuild these indexes from scratch.
    trx->remove(lib::ResourceIf::RealKey{"", key_class_t::ZONE}, true, lib::ResourceIf::Category::ACCOUNT);
    trx->remove(lib::ResourceIf::RealKey{"", key_class_t::TZONE}, true, lib::ResourceIf::Category::ACCOUNT);
    trx->remove(lib::ResourceIf::RealKey{"", key_class_t::ZRR}, true, lib::ResourceIf::Category::ACCOUNT);

    vector<string> zones;
    zones.reserve(zone_to_tenant.size());
    for (const auto& [zone_fqdn, tenant_id] : zone_to_tenant) {
        pb::Zone zone_meta;
        if (auto it = existing_zone_meta.find(zone_fqdn); it != existing_zone_meta.end()) {
            zone_meta = it->second;
        }

        if (!zone_meta.has_id()) {
            zone_meta.set_id(lib::newUuidStr());
        }
        zone_meta.set_tenantid(tenant_id);
        zone_meta.set_status(pb::ACTIVE);

        string serialized_zone;
        zone_meta.SerializeToString(&serialized_zone);

        trx->write(lib::ResourceIf::RealKey{zone_fqdn, key_class_t::ZONE},
                   serialized_zone, true, lib::ResourceIf::Category::ACCOUNT);
        ++result.zone_records_written;

        trx->write(lib::ResourceIf::RealKey{tenant_id, zone_fqdn, key_class_t::TZONE},
                   zone_fqdn, true, lib::ResourceIf::Category::ACCOUNT);
        ++result.tenant_zone_indexes_written;

        zones.emplace_back(zone_fqdn);
    }

    // Rebuild the zone->rr index.
    trx->iterate(ekey, [&zones, &result, &trx](lib::ResourceIf::TransactionIf::key_t key, span_t /*value*/) {
        const auto fqdn = key.dataAsString();
        const auto zone_ix = findBestZoneIx(zones, fqdn);
        if (zone_ix == numeric_limits<size_t>::max()) {
            ++result.orphan_rrsets;
            return true;
        }

        const auto& zone = zones[zone_ix];
        const auto zrr_key = lib::ResourceIf::RealKey{zone, fqdn, key_class_t::ZRR};
        trx->write(zrr_key, "", false, lib::ResourceIf::Category::ACCOUNT);
        ++result.rr_indexes_written;
        return true;
    }, lib::ResourceIf::Category::ENTRY);

    trx->commit();
    return result;
}

EmitFullSyncStreamResult emitFullSyncStream(Server& server, size_t max_parts_per_transaction = 2048) {
    using category_t = lib::ResourceIf::Category;
    using key_class_t = lib::ResourceIf::RealKey::Class;

    if (max_parts_per_transaction == 0) {
        throw runtime_error{"max_parts_per_transaction must be > 0"};
    }

    auto& db = server.db();
    EmitFullSyncStreamResult result;
    result.start_after_trxid = db.getLastCommittedTransactionId();

    // Clear replication stream, but keep current in-memory trx counter at current value.
    {
        auto trx = db.dbTransaction();
        trx->disableTrxlog();
        vector<lib::ResourceIf::RealKey> keys;
        lib::ResourceIf::RealKey start{uint64_t{0}, key_class_t::TRXID};
        trx->iterate(start, [&keys](lib::ResourceIf::TransactionIf::key_t key, span_t) {
            keys.emplace_back(lib::ResourceIf::RealKey::Binary{key.key()});
            return true;
        }, category_t::TRXLOG);
        for (const auto& key : keys) {
            trx->remove(key, false, category_t::TRXLOG);
        }
        trx->commit();
    }

    auto write_trx = db.dbTransaction();
    write_trx->disableTrxlog();
    pb::Transaction out;
    size_t writes_since_commit = 0;

    const auto flush = [&]() {
        if (out.parts_size() == 0) {
            return;
        }

        const auto id = db.createNewTrxId();
        if (!result.first_emitted_trxid) {
            result.first_emitted_trxid = id;
        }
        result.last_emitted_trxid = id;

        out.set_id(id);
        out.set_node(db.config().node_name);
        out.set_time(std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now().time_since_epoch()).count());
        const auto uuid = lib::newUuid();
        out.set_uuid(uuid.begin(), uuid.size());

        string serialized;
        out.SerializeToString(&serialized);
        const lib::ResourceIf::RealKey trx_key{static_cast<uint64_t>(id), key_class_t::TRXID};
        write_trx->write(trx_key,
                         span_t{serialized.data(), serialized.size()},
                         false, category_t::TRXLOG);

        ++result.emitted_transactions;
        ++writes_since_commit;
        out.Clear();

        // Keep transaction sizes bounded.
        if (writes_since_commit >= 256) {
            write_trx->commit();
            write_trx = db.dbTransaction();
            write_trx->disableTrxlog();
            writes_since_commit = 0;
        }
    };

    const auto append_category = [&](category_t category, key_class_t kclass) {
        auto read_trx = db.dbTransaction();
        lib::ResourceIf::RealKey start_key{"", kclass};
        read_trx->iterate(start_key, [&](lib::ResourceIf::TransactionIf::key_t key, span_t value) {
            auto* part = out.add_parts();
            part->set_columnfamilyix(static_cast<int32_t>(category));
            part->set_key(key.data(), key.size());
            part->set_value(value.data(), value.size());
            ++result.emitted_parts;

            if (out.parts_size() >= static_cast<int>(max_parts_per_transaction)) {
                flush();
            }
            return true;
        }, category);
    };

    // Build a deterministic full-state stream.
    const array account_classes = {
        key_class_t::TENANT,
        key_class_t::TENANT_NAME,
        key_class_t::USER,
        key_class_t::ROLE,
        key_class_t::ZONE,
        key_class_t::TZONE,
        key_class_t::ZRR,
        key_class_t::META
    };
    for (const auto kclass : account_classes) {
        append_category(category_t::ACCOUNT, kclass);
    }

    append_category(category_t::MASTER_ZONE, key_class_t::ENTRY);
    append_category(category_t::ENTRY, key_class_t::ENTRY);
    append_category(category_t::DIFF, key_class_t::DIFF);

    flush();

    if (writes_since_commit || out.parts_size() == 0) {
        write_trx->commit();
    }

    return result;
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
    std::string log_level_api = "info";
    std::string log_file;
    std::string config_file;
    std::string bootstrap_cluster_role;
    std::vector<std::string> bootstrap_sets;
    bool trunc_log = true;
    enum class Command {
        None,
        Backup,
        Bootstrap,
        ResetAuth,
        BackupList,
        BackupRestore,
        BackupValidate,
        DumpZones,
        FullResync,
        RepairZoneIndexes,
        EmitFullSyncStream,
        Vars,
        VarsList,
        VarsGet,
        VarsSet,
        VarsUnset
    };
    Command command = Command::None;
    int command_backup_id = 0;
    std::string dump_zones_path;
    std::string command_var_assignment;
    std::string command_var_name;
    bool vars_json_output = false;
    bool vars_force = false;
    bool use_json_log_console = false;
    bool use_json_log_file = false;

    namespace po = boost::program_options;
    po::options_description general("Options");

    general.add_options()
        ("help,h", "Print help and exit")
        ("version", "print version information and exit")
        ("config,c",
            po::value<string>(&config_file),
            "Path to a config file with options in boost::program_options format")
        ("db-path,d",
            po::value<string>(&config.db_path)->default_value(config.db_path),
            "Path to the database directory")
        ("db-dir",
            po::value<string>(&config.db_path),
            "Alias for --db-path")
        ("log-to-console,C",
             po::value<string>(&log_level_console)->default_value(log_level_console),
             "Log-level to the console; one of 'info', 'debug', 'trace'. Empty string to disable.")
        ("log-to-api",
             po::value<string>(&log_level_api)->default_value(log_level_api),
             "Log-level for the API log buffer (/log/show); one of 'info', 'debug', 'trace'.")
        ("json-log-to-console",
             po::bool_switch(&use_json_log_console),
             "Use JSON format for the console log")
        ("log-level,l",
            po::value<string>(&log_level)->default_value(log_level),
            "Log-level; one of 'info', 'debug', 'trace'.")
        ("log-file,L",
             po::value<string>(&log_file),
             "Log-file to write a log to. Default is to use only the console.")
        ("truncate-log-file,T",
             po::value<bool>(&trunc_log)->default_value(trunc_log),
             "Log-file to write a log to. Default is to use the console.")
        ("json-log-file",
         po::bool_switch(&use_json_log_file),
         "Use JSON format for the file log")
        ("set",
         po::value<vector<string>>(&bootstrap_sets)->composing(),
         "Override bootstrap vars with name=value (repeatable, bootstrap only)")
        ("json",
         po::bool_switch(&vars_json_output),
         "JSON output for vars list/get")
        ("force",
         po::bool_switch(&vars_force),
         "Force non-mutable vars update (vars set/unset only)");

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
         "Tells RocksDB to sync the database before starting a backup");

    po::options_description cluster("Cluster");
    cluster.add_options()
        ("cluster-role",
         po::value<string>(&bootstrap_cluster_role),
         "One of: \"primary\", \"follower\", \"none\" (bootstrap only)")
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
        ("disable-http-server",
         po::bool_switch(&config.disable_http),
         "Disables the HTTP server, including swagger and the API server")
        ("with-swagger",
         po::bool_switch(&config.swagger),
         "Enable the '/swagger' endpoint to interactively explore the REST API")
#endif
        ("disable-metrics",
         po::bool_switch(&config.disable_http),
         "Disables the /metrics endpoint.")
        ("disable-metrics-auth",
         po::bool_switch(&config.no_metrics_auth),
         "Disables authentication for the /metrics endpoint.")
#ifdef NSBLAST_WITH_UI
            ("with-ui",
             po::bool_switch(&config.ui),
             "Enable the '/ui' endpoint to serve the WEB UI")
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
            "Threads for the embedded HTTP server");

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
        ("create-certs-num-years",
         po::value(&config.ca_chain.num_years_certs)->default_value(config.ca_chain.num_years_certs),
         "Specifies how many years the server and client cert will be valid for. "
         "Require cert-subject to also be provided.")
        ("create-ca-certs-num-years",
         po::value(&config.ca_chain.num_years_ca)->default_value(config.ca_chain.num_years_ca),
         "Specifies how many years the CA cert will be valid for. "
         "Require cert-subject to also be provided.")
        ("create-certs-key-bytes",
         po::value(&config.ca_chain.key_bytes)->default_value(config.ca_chain.key_bytes),
         "Specifies how many bytes the key will be. "
         "Require cert-subject to also be provided.")
        ("create-certs-ca-name",
         po::value(&config.ca_chain.ca_name)->default_value(config.ca_chain.ca_name),
         "Name of the CA Authority for the self-signed certs. "
         "Require cert-subject to also be provided.")
        ;

    const auto appname = filesystem::path(argv[0]).stem().string();
    po::options_description config_options;
    config_options.add_options()
        ("config,c", po::value<string>(&config_file));

    po::options_description cmdline_options;
    cmdline_options.add(general).add(backup).add(cluster).add(odns).add(http).add(rocksdb).add(cg);

    const auto has_help_flag = [&] {
        for (int i = 1; i < argc; ++i) {
            const string_view token{argv[i]};
            if (token == "-h" || token == "--help") {
                return true;
            }
        }
        return false;
    }();

    auto parse_positive_int = [](string_view value, string_view name) -> int {
        size_t pos = 0;
        const int parsed = stoi(string{value}, &pos);
        if (pos != value.size() || parsed <= 0) {
            throw runtime_error{format("Invalid {} value '{}'", name, value)};
        }
        return parsed;
    };

    size_t command_ix = numeric_limits<size_t>::max();
    size_t command_sub_ix = numeric_limits<size_t>::max();
    size_t command_arg_ix = numeric_limits<size_t>::max();
    const auto option_takes_value = [](string_view opt) {
        static const unordered_set<string_view> options = {
            "-c", "--config", "-d", "--db-path", "--db-dir",
            "-C", "--log-to-console", "--log-to-api", "-l", "--log-level",
            "-L", "--log-file", "-T", "--truncate-log-file",
            "--set",
            "--backup-path", "--hourly-backup-interval", "--sync-before-backup",
            "--cluster-role", "--cluster-auth-key", "--cluster-server-cert",
            "--cluster-server-key", "--cluster-ca-cert", "--cluster-server-address",
            "--cluster-repl-agent-queue-size",
            "-H", "--http-endpoint", "--http-port", "--http-tls-key",
            "--http-tls-cert", "--http-num-threads",
            "--dns-endpoint", "--dns-udp-port", "--dns-tcp-port",
            "--dns-tcp-idle-time", "--dns-num-threads", "--dns-enable-notify",
            "--dns-enable-ixfr", "--dns-notify-port", "--default-nameserver",
            "--rocksdb-db-write-buffer-size", "--rocksdb-optimize-for-small-db",
            "--rocksdb-background-threads",
            "--create-cert-subject", "--create-certs-num-clients", "--create-certs-path",
            "--create-certs-num-years", "--create-ca-certs-num-years",
            "--create-certs-key-bytes", "--create-certs-ca-name",
            "--restore-backup", "--validate-backup"
        };
        return options.contains(opt);
    };

    try {
        bool skip_next_value = false;
        for (size_t i = 1; i < static_cast<size_t>(argc); ++i) {
            const string_view token{argv[i]};
            if (token.empty()) {
                continue;
            }

            if (skip_next_value) {
                skip_next_value = false;
                continue;
            }

            if (token[0] == '-') {
                if (token.find('=') == string_view::npos && option_takes_value(token)) {
                    skip_next_value = true;
                }
                continue;
            }

            command_ix = i;
            if (token == "bootstrap") {
                command = Command::Bootstrap;
            } else if (token == "reset-auth") {
                command = Command::ResetAuth;
            } else if (token == "dump-zones") {
                command = Command::DumpZones;
                for (size_t j = i + 1; j < static_cast<size_t>(argc); ++j) {
                    const string_view arg{argv[j]};
                    if (arg.empty() || arg[0] == '-') {
                        continue;
                    }
                    command_arg_ix = j;
                    dump_zones_path = string{arg};
                    break;
                }
            } else if (token == "full-resync") {
                command = Command::FullResync;
            } else if (token == "repair-zone-indexes") {
                command = Command::RepairZoneIndexes;
            } else if (token == "emit-full-sync-stream") {
                command = Command::EmitFullSyncStream;
            } else if (token == "vars") {
                command = Command::Vars;
                for (size_t j = i + 1; j < static_cast<size_t>(argc); ++j) {
                    const string_view arg{argv[j]};
                    if (arg.empty() || arg[0] == '-') {
                        continue;
                    }
                    command_sub_ix = j;
                    if (arg == "list") {
                        command = Command::VarsList;
                    } else if (arg == "get") {
                        command = Command::VarsGet;
                    } else if (arg == "set") {
                        command = Command::VarsSet;
                    } else if (arg == "unset") {
                        command = Command::VarsUnset;
                    } else {
                        throw runtime_error{format("Invalid vars subcommand '{}'", arg)};
                    }

                    if (command == Command::VarsGet
                        || command == Command::VarsSet
                        || command == Command::VarsUnset) {
                        for (size_t k = j + 1; k < static_cast<size_t>(argc); ++k) {
                            const string_view value_arg{argv[k]};
                            if (value_arg.empty() || value_arg[0] == '-') {
                                continue;
                            }
                            command_arg_ix = k;
                            if (command == Command::VarsSet) {
                                command_var_assignment = string{value_arg};
                            } else {
                                command_var_name = string{value_arg};
                            }
                            break;
                        }
                    }
                    break;
                }
                if (command == Command::Vars && !has_help_flag) {
                    throw runtime_error{"Missing vars subcommand. Use one of: list, get, set, unset"};
                }
            } else if (token == "backup") {
                command = Command::Backup;
                for (size_t j = i + 1; j < static_cast<size_t>(argc); ++j) {
                    const string_view arg{argv[j]};
                    if (arg.empty() || arg[0] == '-') {
                        continue;
                    }
                    command_sub_ix = j;
                    if (arg == "list") {
                        command = Command::BackupList;
                    } else if (arg == "restore") {
                        command = Command::BackupRestore;
                    } else if (arg == "validate") {
                        command = Command::BackupValidate;
                    } else {
                        throw runtime_error{format("Invalid backup subcommand '{}'", arg)};
                    }

                    if (command == Command::BackupRestore || command == Command::BackupValidate) {
                        for (size_t k = j + 1; k < static_cast<size_t>(argc); ++k) {
                            const string_view id_arg{argv[k]};
                            if (id_arg.empty() || id_arg[0] == '-') {
                                continue;
                            }
                            command_arg_ix = k;
                            command_backup_id = parse_positive_int(id_arg, "backup id");
                            break;
                        }
                    }
                    break;
                }
                if (command == Command::Backup && !has_help_flag) {
                    throw runtime_error{"Missing backup subcommand. Use one of: list, restore, validate"};
                }
            } else {
                throw runtime_error{format("Unknown command '{}'", token)};
            }
            break;
        }
    } catch (const std::exception& ex) {
        cerr << appname
             << " Failed to parse command-line/config arguments: " << ex.what() << endl;
        return -1;
    }

    auto print_global_help = [&]() {
        cout << appname << " [global-options]\n";
        cout << appname << " [global-options] <command> [command-arguments]\n\n";
        cout << "Available commands:\n";
        cout << "  bootstrap\n";
        cout << "  reset-auth\n";
        cout << "  dump-zones <path>\n";
        cout << "  full-resync\n";
        cout << "  repair-zone-indexes\n";
        cout << "  emit-full-sync-stream\n";
        cout << "  vars list\n";
        cout << "  vars get <name>\n";
        cout << "  vars set <name=value>\n";
        cout << "  vars unset <name>\n";
        cout << "  backup list\n";
        cout << "  backup restore <id>\n";
        cout << "  backup validate <id>\n\n";
        cout << cmdline_options << std::endl;
    };

    auto print_command_help = [&]() {
        const auto print_shared = [&]() {
            cout << "Relevant global options:\n";
            cout << "  -h, --help\n";
            cout << "  --version\n";
            cout << "  -c, --config <path>\n";
            cout << "  -d, --db-path <path>\n";
            cout << "  --db-dir <path>\n";
            cout << "  -C, --log-to-console <level>\n";
            cout << "  --log-to-api <level>\n";
            cout << "  --json-log-to-console\n";
            cout << "  -l, --log-level <level>\n";
            cout << "  -L, --log-file <path>\n";
            cout << "  -T, --truncate-log-file <bool>\n";
            cout << "  --json-log-file\n";
        };

        switch (command) {
        case Command::Bootstrap:
            cout << appname << " [global-options] bootstrap\n\n";
            cout << "Initializes a new database and default auth data, then exits.\n";
            cout << "Aborts if the database already exists.\n\n";
            print_shared();
            cout << "Bootstrap options:\n";
            cout << "  --cluster-role <primary|follower|none> (required)\n";
            cout << "  --set <name=value> (repeatable)\n";
            break;
        case Command::ResetAuth:
            cout << appname << " [global-options] reset-auth\n\n";
            cout << "Resets the 'admin' account and the 'nsBLAST' tenant to the default state.\n\n";
            print_shared();
            break;
        case Command::Backup:
            cout << appname << " [global-options] backup <list|restore|validate> [id]\n\n";
            cout << "Manage local backups without starting network endpoints.\n\n";
            print_shared();
            cout << "Backup options:\n";
            cout << "  --backup-path <path>\n";
            break;
        case Command::DumpZones:
            cout << appname << " [global-options] dump-zones <path>\n\n";
            cout << "Dumps all zones and RR sets from the database to JSON and exits.\n\n";
            print_shared();
            break;
        case Command::FullResync:
            cout << appname << " [global-options] full-resync\n\n";
            cout << "Follower-only maintenance mode: sync from trx-id 0 and exit when IN_SYNC.\n\n";
            print_shared();
            cout << "Cluster options:\n";
            cout << "  --cluster-auth-key <path>\n";
            cout << "  --cluster-server-address <host:port>\n";
            cout << "  --cluster-server-cert <path>\n";
            cout << "  --cluster-server-key <path>\n";
            cout << "  --cluster-ca-cert <path>\n";
            break;
        case Command::Vars:
        case Command::VarsList:
            cout << appname << " [global-options] vars list [--json]\n\n";
            cout << "List permanent variables.\n\n";
            print_shared();
            break;
        case Command::VarsGet:
            cout << appname << " [global-options] vars get <name> [--json]\n\n";
            cout << "Read one permanent variable.\n\n";
            print_shared();
            break;
        case Command::VarsSet:
            cout << appname << " [global-options] vars set <name=value> [--force]\n\n";
            cout << "Set a permanent variable.\n\n";
            print_shared();
            break;
        case Command::VarsUnset:
            cout << appname << " [global-options] vars unset <name> [--force]\n\n";
            cout << "Reset a permanent variable to compiled default.\n\n";
            print_shared();
            break;
        case Command::RepairZoneIndexes:
            cout << appname << " [global-options] repair-zone-indexes\n\n";
            cout << "Rebuild ACCOUNT zone metadata/indexes from ENTRY data and exits.\n\n";
            print_shared();
            break;
        case Command::EmitFullSyncStream:
            cout << appname << " [global-options] emit-full-sync-stream\n\n";
            cout << "Clears TRXLOG and emits a full-state replication stream, then exits.\n\n";
            print_shared();
            break;
        case Command::BackupList:
            cout << appname << " [global-options] backup list\n\n";
            cout << "Lists available backups and exits.\n\n";
            print_shared();
            cout << "Backup options:\n";
            cout << "  --backup-path <path>\n";
            break;
        case Command::BackupRestore:
            cout << appname << " [global-options] backup restore <id>\n\n";
            cout << "Restores backup <id> to the database directory and exits.\n\n";
            print_shared();
            cout << "Backup options:\n";
            cout << "  --backup-path <path>\n";
            break;
        case Command::BackupValidate:
            cout << appname << " [global-options] backup validate <id>\n\n";
            cout << "Validates backup <id> and exits.\n\n";
            print_shared();
            cout << "Backup options:\n";
            cout << "  --backup-path <path>\n";
            break;
        case Command::None:
            print_global_help();
            break;
        }
    };

    vector<string> filtered_args;
    filtered_args.reserve(static_cast<size_t>(argc));
    filtered_args.emplace_back(argv[0]);
    for (size_t i = 1; i < static_cast<size_t>(argc); ++i) {
        if (i == command_ix || i == command_sub_ix || i == command_arg_ix) {
            continue;
        }
        filtered_args.emplace_back(argv[i]);
    }

    po::variables_map vm;
    try {
        // Parse only --config first so we can load defaults from file.
        po::store(po::command_line_parser(filtered_args)
                    .options(config_options)
                    .allow_unregistered()
                    .run(),
                  vm);
        po::notify(vm);

        po::variables_map vm_from_config;
        po::variables_map vm_from_cli;

        auto merge_explicit = [](po::variables_map& dst, const po::variables_map& src) {
            for (const auto& [name, value] : src) {
                if (value.defaulted()) {
                    continue;
                }

                dst.erase(name);
                dst.emplace(name, value);
            }
        };

        if (!config_file.empty()) {
            ifstream cfg{config_file};
            if (!cfg.is_open()) {
                throw runtime_error{"Failed to open config file: " + config_file};
            }

            po::store(po::parse_config_file(cfg, cmdline_options, true), vm_from_config);
        }

        // Parse command line and apply only explicit values so defaults do not
        // clobber entries from the config file.
        po::store(po::command_line_parser(filtered_args).options(cmdline_options).run(), vm_from_cli);

        vm.clear();
        merge_explicit(vm, vm_from_config);
        merge_explicit(vm, vm_from_cli);
        po::notify(vm);
    } catch (const std::exception& ex) {
        cerr << appname
             << " Failed to parse command-line/config arguments: " << ex.what() << endl;
        return -1;
    }

    const auto has_explicit_option = [&vm](const string& key) {
        return vm.count(key) && !vm[key].defaulted();
    };

    if (has_explicit_option("cluster-role") && command != Command::Bootstrap) {
        cerr << "--cluster-role is only valid for the bootstrap command" << endl;
        return -1;
    }

    if (has_explicit_option("set") && command != Command::Bootstrap) {
        cerr << "--set is only valid for the bootstrap command" << endl;
        return -1;
    }

    if (vm.count("help")) {
        if (command == Command::None) {
            print_global_help();
        } else {
            print_command_help();
        }
        return -2;
    }

    if (vm.count("version")) {
        cout << Server::getVersionInfo();
        return -3;
    }

    if (command == Command::Bootstrap && !has_help_flag && bootstrap_cluster_role.empty()) {
        cerr << "bootstrap requires --cluster-role <primary|follower|none>" << endl;
        return 2;
    }

    auto ring_level = logging::parseLogLevel(log_level_api, logfault::LogLevel::INFO);
    if (ring_level == logfault::LogLevel::DISABLED) {
        ring_level = logfault::LogLevel::INFO;
    }
    logfault::LogManager::Instance().AddHandler(
            make_unique<logging::RingBufferHandler>(ring_level));

    if (!log_file.empty()) {
        if (auto level = toLogLevel(log_level)) {
            if (use_json_log_file) {
                logfault::LogManager::Instance().AddHandler(
                        make_unique<logfault::JsonHandler>(log_file, *level, trunc_log, 0xffff));
            } else {
                logfault::LogManager::Instance().AddHandler(
                        make_unique<logfault::StreamHandler>(log_file, *level, trunc_log));
            }
        }
    }

    if (auto level = toLogLevel(log_level_console)) {
        if (use_json_log_console) {
            logfault::LogManager::Instance().AddHandler(
                    make_unique<logfault::JsonHandler>(clog, *level, 0xffff));
        } else {
            logfault::LogManager::Instance().AddHandler(
                    make_unique<logfault::StreamHandler>(clog, *level));
        }
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

    config.cluster_force_full_resync = command == Command::FullResync;

    LOG_INFO << appname << ' ' << NSBLAST_VERSION  " starting up. Log level: " << log_level;
    LOG_INFO << "I am running as user=" << getuid() << " group=" << getgid();

    try {        
        Server server{config};

        if (command == Command::Bootstrap) {
            server.startRocksDb(true, true);
            if (bootstrap_cluster_role.empty()) {
                throw runtime_error{"bootstrap requires --cluster-role <primary|follower|none>"};
            }
            server.bootstrapVars(bootstrap_cluster_role, bootstrap_sets);
            server.startAuth();
            LOG_INFO << "Database bootstrap complete.";
            return 0;
        }

        if (command == Command::VarsList) {
            server.startRocksDb();
            const auto items = server.vars().list();
            if (vars_json_output) {
                boost::json::object out;
                auto& arr = out["items"] = boost::json::array{};
                for (const auto& item : items) {
                    boost::json::object row;
                    row["name"] = item.name;
                    row["value"] = item.value;
                    row["default"] = item.default_value;
                    row["requires_restart"] = item.requires_restart;
                    row["non_mutable"] = item.non_mutable;
                    row["description"] = item.description;
                    arr.as_array().emplace_back(std::move(row));
                }
                cout << boost::json::serialize(out) << endl;
            } else {
                for (const auto& item : items) {
                    cout << item.name
                         << "\tvalue=" << boost::json::serialize(item.value)
                         << "\tdefault=" << boost::json::serialize(item.default_value)
                         << "\trequires_restart=" << (item.requires_restart ? "true" : "false")
                         << "\tnon_mutable=" << (item.non_mutable ? "true" : "false")
                         << "\tdescription=" << item.description
                         << '\n';
                }
            }
            return 0;
        }

        if (command == Command::VarsGet) {
            if (command_var_name.empty()) {
                throw runtime_error{"vars get requires <name>"};
            }
            server.startRocksDb();
            if (const auto item = server.vars().get(command_var_name)) {
                if (vars_json_output) {
                    boost::json::object out;
                    out["name"] = item->name;
                    out["value"] = item->value;
                    out["default"] = item->default_value;
                    out["requires_restart"] = item->requires_restart;
                    out["non_mutable"] = item->non_mutable;
                    out["description"] = item->description;
                    cout << boost::json::serialize(out) << endl;
                } else {
                    cout << boost::json::serialize(item->value) << endl;
                }
                return 0;
            }
            return 3;
        }

        if (command == Command::VarsSet) {
            if (command_var_assignment.empty()) {
                throw runtime_error{"vars set requires <name=value>"};
            }
            server.startRocksDb();
            try {
                server.vars().setFromAssignment(command_var_assignment, vars_force, true);
            } catch (const lib::Vars::Error& ex) {
                return ex.code();
            }
            return 0;
        }

        if (command == Command::VarsUnset) {
            if (command_var_name.empty()) {
                throw runtime_error{"vars unset requires <name>"};
            }
            server.startRocksDb();
            try {
                server.vars().unset(command_var_name, vars_force, true);
            } catch (const lib::Vars::Error& ex) {
                return ex.code();
            }
            return 0;
        }

        if (command == Command::ResetAuth) {
            server.resetAuth();
            return 0;
        }

        if (command == Command::BackupList) {
            server.startBackupMgr(false);
            server.listBackups();
            return 0;
        }

        if (command == Command::BackupRestore) {
            if (!command_backup_id) {
                throw runtime_error{"backup restore requires <id>"};
            }
            server.startBackupMgr(false);
            server.restoreBackup(command_backup_id);
            return 0;
        }

        if (command == Command::BackupValidate) {
            if (!command_backup_id) {
                throw runtime_error{"backup validate requires <id>"};
            }
            server.startBackupMgr(false);
            server.validateBackup(command_backup_id);
            return 0;
        }

        if (command == Command::DumpZones) {
            if (dump_zones_path.empty()) {
                throw runtime_error{"dump-zones requires <path>"};
            }
            server.startRocksDb();
            dumpZones(server, dump_zones_path);
            return 0;
        }

        if (command == Command::FullResync) {
#ifdef NSBLAST_CLUSTER
            server.startRocksDb();
            server.initReplication();
            if (!server.isReplicationFollower()) {
                throw runtime_error{"full-resync only works when cluster_role is follower"};
            }

            LOG_INFO << "Starting follower full resync maintenance mode.";
            server.startGrpcService();
            server.StartReplication();
            server.startIoThreads();

            constexpr auto timeout = chrono::minutes{30};
            const auto deadline = chrono::steady_clock::now() + timeout;

            while(chrono::steady_clock::now() < deadline) {
                if (server.followerInSync()) {
                    LOG_INFO << "Follower reported IN_SYNC. Exiting full resync mode.";
                    server.stop();
                    return 0;
                }
                this_thread::sleep_for(chrono::milliseconds{200});
            }

            server.stop();
            throw runtime_error{"Timed out waiting for follower replication to report IN_SYNC"};
#else
            throw runtime_error{"full-resync requires cluster support (NSBLAST_CLUSTER)"};
#endif
        }

        if (command == Command::RepairZoneIndexes) {
            server.startRocksDb();
            const auto result = repairZoneIndexes(server);
            LOG_INFO << "Repair complete:"
                     << " zones_discovered=" << result.zones_discovered
                     << ", zone_records_written=" << result.zone_records_written
                     << ", tenant_zone_indexes_written=" << result.tenant_zone_indexes_written
                     << ", rr_indexes_written=" << result.rr_indexes_written
                     << ", orphan_rrsets=" << result.orphan_rrsets;
            const auto emit = emitFullSyncStream(server);
            LOG_INFO << "Full sync stream emitted after repair:"
                     << " start_after_trxid=" << emit.start_after_trxid
                     << ", first_emitted_trxid=" << emit.first_emitted_trxid
                     << ", last_emitted_trxid=" << emit.last_emitted_trxid
                     << ", emitted_transactions=" << emit.emitted_transactions
                     << ", emitted_parts=" << emit.emitted_parts;
            return 0;
        }

        if (command == Command::EmitFullSyncStream) {
            server.startRocksDb();
            const auto emit = emitFullSyncStream(server);
            LOG_INFO << "Full sync stream emitted:"
                     << " start_after_trxid=" << emit.start_after_trxid
                     << ", first_emitted_trxid=" << emit.first_emitted_trxid
                     << ", last_emitted_trxid=" << emit.last_emitted_trxid
                     << ", emitted_transactions=" << emit.emitted_transactions
                     << ", emitted_parts=" << emit.emitted_parts;
            return 0;
        }

        server.start();
    } catch (const exception& ex) {
        LOG_ERROR << "Caught exception from Server: " << ex.what();
    }
} // main
