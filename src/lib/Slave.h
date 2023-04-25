#pragma once

#include <atomic>
#include <mutex>
#include <memory>
#include "nsblast/nsblast.h"
#include "nsblast/ResourceIf.h"
#include "proto/nsblast.pb.h"


namespace nsblast::lib {

class SlaveMgr;

class Slave : public std::enable_shared_from_this<Slave> {
public:
    using tcp_t = boost::asio::ip::tcp;
    Slave(SlaveMgr& mgr, std::string_view fqdn, pb::Zone  zone);

    void start();
    void done();

    tcp_t::endpoint remoteEndpoint() const noexcept;

    /*! Called when the slave-server gets an NOTIFY request for the zone */
    void onNotify(const boost::asio::ip::address& address);

private:
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
    void sendQuestion(tcp_t::socket& socket, uint16_t question,
                      uint32_t serial, yield_t& yield);

    /*! Get one reply for a question via the TCP socket
     *
     * The caller owns the buffer for the returned Entry.
     */
    Message getReply(tcp_t::socket& socket, buffer_t& buffer, yield_t& yield);
    void checkIfDone();
    bool isZoneUpToDate(tcp_t::socket& socket, yield_t& yield);
    void doAxfr(tcp_t::socket& socket, yield_t& yield);
    void doIxfr(tcp_t::socket& socket, yield_t& yield);

    // Handle isfr and axfr payloads. If mySerial is > 0,
    // it is a for an ixfr request.
    void handleIxfrPayloads(ResourceIf::TransactionIf& trx,
                            tcp_t::socket& socket,
                            uint32_t mySerial,
                            yield_t& yield);

    SlaveMgr& mgr_;
    const std::string fqdn_;
    const pb::Zone zone_; // Configuration
    boost::asio::deadline_timer schedule_;
    std::optional<boost::asio::deadline_timer> timeout_;
    mutable std::mutex mutex_;;
    std::atomic_bool done_{false};
    tcp_t::endpoint current_remote_ep_;
    uint16_t next_id = getRandomNumber16();
    std::atomic_size_t notifications_ = 0;
};

} // ns
