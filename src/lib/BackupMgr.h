#pragma once

#include <boost/date_time.hpp>

#include "nsblast/nsblast.h"

namespace nsblast {

class Server;

namespace lib {

class BackupMgr {
public:
    BackupMgr(Server& server);

    void initAutoBackup();

    void restoreBackup(int id);
    void validateBackup(int id);
    void listBackups();

    Server& server() {
        return server_;
    }

    /*! Get the next hole hour(s) in UTC
     *
     *  \param numHours Number of hours into the future
     *  \return time_t value for the desired time-point.
     */
    static std::chrono::system_clock::time_point getNextHours(size_t numHours);

private:
    void onTimer();
    void startTimer(std::chrono::system_clock::time_point when);

    boost::asio::system_timer timer_;
    Server& server_;
};

}} // ns
