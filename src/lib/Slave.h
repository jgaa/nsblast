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
    using tcp_t = boost::asio::ip::tcp;
    using buffer_t = std::vector<char>;
    using yield_t = boost::asio::yield_context;

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

    /*! Send a question via the TCP socket */
    void sendQuestion(tcp_t::socket& socket, uint16_t question, yield_t& yield);

    /*! Get one reply for a question via the TCP socket
     *
     * The caller owns the buffer for the returned Entry.
     */
    Entry getReply(tcp_t::socket& socket, buffer_t& buffer, yield_t& yield);

    SlaveMgr& mgr_;
    const std::string fqdn_;
    const pb::Zone zone_; // Configuration
    boost::asio::deadline_timer schedule_;
    std::optional<boost::asio::deadline_timer> timeout_;
    std::mutex mutex_;;
    bool done_ = true;
};

} // ns
