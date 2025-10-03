
#include <format>

#include "BackupMgr.h"
#include "nsblast/Server.h"
#include "RocksDbResource.h"
#include "nsblast/logging.h"
#include "nsblast/util.h"

using namespace std;
using namespace std::string_literals;

namespace nsblast::lib {


BackupMgr::BackupMgr(Server &server)
    : server_{server}, timer_{server.ctx()}
{
}

void BackupMgr::initAutoBackup()
{
    // Always start at the next full hour.
    auto next = getNextHours(1);
    if (auto hours = server().config().hourly_backup_interval) {
        LOG_INFO_N << "Scheduling automatic database backups every "
                   << hours
                   << " hours. See hourly-backup-interval option";
        startTimer(next);
    }
}

void BackupMgr::restoreBackup(int id)
{
    // We need to actually start the database to make sure that it's not in use by anther process.
    try {
        server().startRocksDb();
    } catch (const exception& ex) {
        LOG_WARN_N << "If Nsblast fails to open the existing (old) database "
                      "and you are sure it's not in use by another process, you may try "
                      "to remove the database folder (named 'rocksdb') and it's subdirectories. Make sure "
                      "you DONT remove the backup directory!";
        return;
    }

    server().db().close();
    server().db().restoreBackup(id, server().config().backup_path);
}

void BackupMgr::validateBackup(int id)
{
    server().startRocksDb(false);
    server().db().verifyBackup(id, server().config().backup_path);
}

void BackupMgr::listBackups()
{
    server().startRocksDb(false);
    boost::json::object json;
    server().db().listBackups(json, server().config().backup_path);

    std::ostringstream out;
    if (auto a = json.if_contains("backups")) {
        for(const auto& b : a->as_array()) {
            auto& o = b.as_object();
            out << "Backup id: #" << o.at("id").as_uint64() << endl
                << "     uuid: " << o.at("uuid").as_string() << endl
                << "     date: " << o.at("date").as_string() << endl
                << "     size: " << o.at("size").as_uint64() << endl;
        }
    }

    LOG_INFO << "Listing backups:" << endl << out.str();
}

std::chrono::system_clock::time_point BackupMgr::getNextHours(size_t numHours)
{
    assert(numHours > 0);
    assert(numHours > 0);
    using namespace std::chrono;

    const auto now = system_clock::now();

    // Add N hours, then align to whole hours (floor).
    // e.g. 10:37 + 3h = 13:37 -> floor<hours> = 13:00
    const auto when = floor<hours>(now + hours(numHours));

    assert(when > now);

    LOG_TRACE << "BackupMgr::getNextHours Time " << std::format("{}", when)
              << " is supposed to be the next " << numHours
              << " hour(s) into the future.";

    return when;
}

void BackupMgr::onTimer()
{
    if (server().isDone()) {
        LOG_DEBUG_N << "Server is shutting down. I'm not starting a new Backup.";
        return;
    }

    LOG_INFO_N << "Starting automatic backup. Interval is set to "
               << server().config().hourly_backup_interval
               << " hours";

    server().db().startBackup(server().config().backup_path,
                              server().config().sync_before_backup);
}

void BackupMgr::startTimer(std::chrono::system_clock::time_point when)
{
    if (server().isDone()) {
        return;
    }

    LOG_INFO_N << "Scehduling next automatic backup at "
               << format("{}", when);

    try {
        timer_.cancel();
    } catch(const std::exception&) {
        ;
    }
    timer_.expires_at(when);
    timer_.async_wait([this](boost::system::error_code ec) {
        if (ec) {
            if (ec == boost::asio::error::operation_aborted) {
                LOG_TRACE << "BackupMgr::startTimer timer was cancelled";
                return;
            }
            LOG_WARN << "BackupMgr::startTimer timer failed: " << ec.message();
        } else {
            try {
                onTimer();
            } catch (const exception& ex) {
                LOG_ERROR << "BackupMgr::startTimer - exception from onTimer(): "
                          << ex.what();
            }
            if (auto hours = server().config().hourly_backup_interval) {
                startTimer(getNextHours(hours));
            }
        }
    });
}

} // ns
