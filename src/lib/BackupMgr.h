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
    static boost::posix_time::ptime getNextHours(size_t numHours);

private:
    void onTimer();
    void startTimer(boost::posix_time::ptime when);

    boost::asio::deadline_timer timer_;
    Server& server_;
};

}} // ns
