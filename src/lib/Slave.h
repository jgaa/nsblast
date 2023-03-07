#pragma once

#include <mutex>
#include <memory>
#include "nsblast/nsblast.h"
#include "nsblast/ResourceIf.h"
#include "proto/nsblast.pb.h"


namespace nsblast::lib {

class SlaveMgr;

class Slave : public std::enable_shared_from_this<Slave> {
public:
    Slave(SlaveMgr& mgr, std::string_view fqdn, const pb::Zone& zone);

    void start();

private:
    void setTimer(uint32_t secondsInFuture);
    void sync();
    void sync(boost::asio::yield_context& yield);

    /*! Get the serial for the local copy of the SOA for the zone.
     *
     *  \returns The local SOA's serial, or 0 if we don't have a local
     *           SOA RR yet.
     */
    uint32_t localSerial();
    uint32_t interval() const noexcept;

    SlaveMgr& mgr_;
    const std::string fqdn_;
    const pb::Zone zone_; // Configuration
    boost::asio::deadline_timer schedule_;
    std::optional<boost::asio::deadline_timer> timeout_;
    std::mutex mutex_;;
    bool done_ = true;
};

} // ns
