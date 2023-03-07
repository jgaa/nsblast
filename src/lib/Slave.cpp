
#include <boost/asio.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/spawn.hpp>

#include "SlaveMgr.h"
#include "Slave.h"

#include "nsblast/logging.h"
#include "nsblast/util.h"
#include "proto_util.h"

using namespace std;
using namespace std::string_literals;


namespace nsblast::lib {

Slave::Slave(SlaveMgr &mgr, std::string_view fqdn, const pb::Zone& zone)
    : mgr_{mgr}, fqdn_{fqdn}, zone_{zone}, schedule_{mgr.ctx()}
{
}

void Slave::start()
{
    // Set a timer to a random time within the refresh-window
    setTimer(1);

    // When the timer is triggered, do a client transfer

        // Delete the Zone in Entry contxt
        // Add all the RR's until the final soa
        // If the transfer is successful and valid, cimmit the transaction.
        // Set a new timer, this time for the refresh-value
    // repeat
}

void Slave::setTimer(uint32_t secondsInFuture)
{
    LOG_TRACE << "Slave::setTimer Setting timer for " << fqdn_
              << ' ' << secondsInFuture << " seconds from now.";

    schedule_.expires_from_now(boost::posix_time::seconds{secondsInFuture});
    schedule_.async_wait([self=shared_from_this()](boost::system::error_code ec) {
        if (self->done_) {
            LOG_TRACE << "Slave::setTimer - Timer for sync with zone " << self->fqdn_
                      << " discovered that the Slave instance is done.";
            return;
        }
        if (ec) {
            LOG_WARN << "Slave::setTimer - Timer for sync with zone " << self->fqdn_
                     << " failed with error: " << ec.message();
        }

        self->sync();
    });
}

void Slave::sync()
{
    boost::asio::spawn([this, self=shared_from_this()](boost::asio::yield_context yield) {
        try {
            sync(yield);
        }  catch (const exception& ex) {
            LOG_ERROR << "Slave::sync - Zone sync for " << self->fqdn_
                      << " failed with exception: " << ex.what();
        }

        self->setTimer(self->interval());
    });
}

void Slave::sync(boost::asio::yield_context &yield)
{
    LOG_DEBUG << "Slave::sync - synching zone " << fqdn_;

    // Create a TCP connection to the master

    // Get our soa
    auto serial = localSerial();
    if (serial) {
        // Get master's soa so we can compare

        MessageBuilder mb;
        mb.setMaxBufferSize(512);
        mb.createHeader(1, false, MessageBuilder::Header::OPCODE::QUERY, false);
        mb.addQuestion(fqdn_, TYPE_SOA);
        mb.finish();

        // Send question
        // Read reply
    }

    // Do AXFR

    // Send question
    // Read all the replies.
    // At the first reply-message, start a DB transaction and delete the old RR records
    // Then add the new RR records as we receive them.
    // TODO: In the future, may be better to cache small zones in memory and
    //       do a fast transaction update, and cache large zones on local storage
    //       so we don't keeps the transaction open for a long time.

}

uint32_t Slave::localSerial()
{
    auto trx = mgr_.db().transaction();

    auto entry = trx->lookup(fqdn_);
    if (entry.empty() || !entry.hasSoa()) {
        return {};
    }

    return entry.getSoa().serial();
}

uint32_t Slave::interval() const noexcept
{
    return PB_GET(zone_.master(), refresh, mgr_.config().dns_default_zone_pull_interval_);
}


} // ns
