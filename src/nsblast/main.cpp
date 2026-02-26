
#include <unistd.h>

#include <iostream>
#include <filesystem>
#include <fstream>
#include <array>
#include <thread>
#include <chrono>
#include <unordered_map>
#include <boost/program_options.hpp>
#include <boost/json.hpp>
#include <boost/version.hpp>
#include <boost/uuid/uuid_io.hpp>

#include "nsblast/nsblast.h"
#include "nsblast/logging.h"
#include "nsblast/LogCapture.h"
#include "nsblast/Server.h"
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
    bool trunc_log = true;
    int restore_backup_id = 0;
    int validate_backup_id = 0;
    std::string dump_zones_path;
    bool full_resync = false;
    bool repair_zone_indexes = false;
    bool emit_full_sync_stream = false;
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
        ("reset-auth",
            "Resets the 'admin' account and the 'nsBLAST' tenant to it's default, initial state."
            "The server will terminate after the changes are made.")
        ("dump-zones",
            po::value<string>(&dump_zones_path),
            "Emergency: dump all zones and RR sets from the database to a JSON file and exit")
        ("full-resync",
            po::bool_switch(&full_resync),
            "Follower-only maintenance mode: force replication sync from trx-id 0 and exit when IN_SYNC")
        ("repair-zone-indexes",
            po::bool_switch(&repair_zone_indexes),
            "Emergency: rebuild ACCOUNT zone metadata/indexes (ZONE, TZONE, ZRR) from ENTRY data and exit")
        ("emit-full-sync-stream",
            po::bool_switch(&emit_full_sync_stream),
            "Emergency: clear TRXLOG and emit a synthetic full-state replication stream from current DB content, then exit")
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
            "Threads for the embedded HTTP server")
        ("dynip-enable-get",
            po::value<bool>(&config.dynip_enable_get)->default_value(config.dynip_enable_get),
            "Enable GET /nic/update (legacy DynDNS compatible endpoint)")
        ("dynip-enable-post-json",
            po::value<bool>(&config.dynip_enable_post_json)->default_value(config.dynip_enable_post_json),
            "Enable POST /nic/update with JSON payload")
        ("dynip-allow-partial-multi-host",
            po::value<bool>(&config.dynip_allow_partial_multi_host)->default_value(config.dynip_allow_partial_multi_host),
            "Allow partial success when multiple hostnames are provided")
        ("dynip-max-hosts-per-request",
            po::value<size_t>(&config.dynip_max_hosts_per_request)->default_value(config.dynip_max_hosts_per_request),
            "Maximum hostnames accepted in one DynIP request")
        ("dynip-require-user-agent",
            po::value<bool>(&config.dynip_require_user_agent)->default_value(config.dynip_require_user_agent),
            "Require User-Agent for DynIP requests")
        ("dynip-allow-private-ips",
            po::value<bool>(&config.dynip_allow_private_ips)->default_value(config.dynip_allow_private_ips),
            "Allow private IPv4/IPv6 ranges in DynIP updates")
        ("dynip-default-ttl-seconds",
            po::value<uint32_t>(&config.dynip_default_ttl_seconds)->default_value(config.dynip_default_ttl_seconds),
            "Default TTL used when DynIP creates a new A/AAAA RR set")
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
    po::variables_map vm;
    try {
        // Parse only --config first so we can load defaults from file.
        po::store(po::command_line_parser(argc, argv)
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
        po::store(po::command_line_parser(argc, argv).options(cmdline_options).run(), vm_from_cli);

        vm.clear();
        merge_explicit(vm, vm_from_config);
        merge_explicit(vm, vm_from_cli);
        po::notify(vm);
    } catch (const std::exception& ex) {
        cerr << appname
             << " Failed to parse command-line/config arguments: " << ex.what() << endl;
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

    config.cluster_force_full_resync = full_resync;

    LOG_INFO << appname << ' ' << NSBLAST_VERSION  " starting up. Log level: " << log_level;
    LOG_INFO << "I am running as user=" << getuid() << " group=" << getgid();

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

        if (!dump_zones_path.empty()) {
            server.startRocksDb();
            dumpZones(server, dump_zones_path);
            return 0;
        }

        if (full_resync) {
#ifdef NSBLAST_CLUSTER
            server.startRocksDb();
            server.initReplication();
            if (!server.isReplicationFollower()) {
                throw runtime_error{"--full-resync only works when --cluster-role=follower"};
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
            throw runtime_error{"--full-resync requires cluster support (NSBLAST_CLUSTER)"};
#endif
        }

        if (repair_zone_indexes) {
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

        if (emit_full_sync_stream) {
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
