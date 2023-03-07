
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
    // TODO: Set a timer to a random time within the refresh-window?
    //       We don't want to start all the refresh transfers in parallel
    //       when the server starts up. However, wen we add a new slave zone
    //       from the REST API, we want an immediate sync.
    setTimer(1);
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
// Note: This implementation assumes that the RR's arrive sorted,
//       with all relevant RR's for a fqdn in one block (potentially
//       carried in multiple messages).
//       The RR's are not validated, like ensuring that a CNAME is not
//       mixed with other RR's.
void Slave::sync(boost::asio::yield_context &yield)
{
    LOG_DEBUG << "Slave::sync - synching zone " << fqdn_;

    // Create a TCP connection to the master

    auto socket = TcpConnect(mgr_.ctx(), zone_.master().hostname(),
                             to_string(PB_GET(zone_.master(), port, 53)),
                             yield);

    auto remote_ep = socket.remote_endpoint();

    // TODO: Cteate a timer.
    //   In the timer:
    //   - Check for connection, read or write timeouts
    //   - Check if the Slave object is obsolete (done_ == true)

    boost::system::error_code ec;
    array<char, 2> size_buf = {};

    // Get our soa
    auto serial = localSerial();
    if (serial) {
        // Get master's soa so we can compare
        sendQuestion(socket, TYPE_SOA, yield);

        buffer_t buffer;
        auto reply = getReply(socket, buffer, yield);

        auto soa = reply.getSoa();
        assert(!reply.empty());

        if (!soa) {
            LOG_ERROR << "Slave::sync - The master server at "
                      << remote_ep
                      << " for zone " << fqdn_
                      << " did not return a SOA RR for this fqdn. "
                      << "Is this the correct authortative server for this zone?";
            throw runtime_error{"Slave::sync: Master server has no SOA for this zone."};
        }

        const auto rserial = soa->serial();
        LOG_TRACE << "Slave::sync locval serial is " << serial
                  << ", remote serial is " << rserial;

        if (rserial != serial) {
            LOG_DEBUG << "Slave::sync - SOA serials are; local=" << serial
                      << ", master=" << rserial
                      << ". I need to sync against the master for zone " << fqdn_
                      << " at " << remote_ep;
        } else {
            LOG_DEBUG << "Slave::sync - SOA serial for " << fqdn_
                      << " are in sync with the master at " << remote_ep;
            return;
        }
    }

    // Do AXFR

    // Send question
    sendQuestion(socket, QTYPE_AXFR, yield);

    // Read all the replies.
    bool first = true;
    uint16_t id = 0; // All messages must be for this id
    buffer_t buffer;
    string rsoa_fqdn;
    uint32_t rsoa_serial = 0;
    bool has_second_rr = false;

    auto trx = mgr_.db().transaction();

    optional<StorageBuilder> sb;
    string current_fqdn;

    while(!has_second_rr) {
        auto reply = getReply(socket, buffer, yield);

        for(auto& rr : reply.getAnswers()) {
            if (has_second_rr) {
                LOG_ERROR << "Slave::sync - Invalid AXFR payload for " << fqdn_
                          << " from master at " << remote_ep
                          << " There are more answers after the second SOA!";
                throw runtime_error{"Slave::sync - Invalid AXFR payload. More RR's after second SOA."};
            }

            if (first) {
                if (rr.type() != TYPE_SOA) {
                    LOG_ERROR << "Slave::sync - Invalid AXFR payload for " << fqdn_
                              << " from master at " << remote_ep
                              << ". AXFR must start with SOA RR!";
                    throw runtime_error{"Slave::sync - Invalid AXFR payload."};
                }

                RrSoa soa{reply.span(), rr.offset()};
                rsoa_serial = soa.serial();
                rsoa_fqdn = toLower(soa.labels().string());
                if (fqdn_ != rsoa_fqdn) {
                    LOG_ERROR << "Slave::sync - Invalid AXFR payload for " << fqdn_
                              << " from master at " << remote_ep
                              << ". fqdn in first received SOA is " << rsoa_fqdn
                              << ". I expected " << fqdn_;
                    throw runtime_error{"Slave::sync - Invalid AXFR payload. Unexpected fqdn in first SOA."};
                }
                id = reply.header().id();
                first = false;

                // Delete all existing Entries in the local database for this zone
                trx->remove(fqdn_, true);

            } /* first */ else {
                if (rr.type() != TYPE_SOA) {
                    // There can only be two SOA RR's in the reply, the first and the last
                    // entry, and they must be the same.
                    RrSoa soa{reply.span(), rr.offset()};
                    auto fsoa_fqdn = toLower(soa.labels().string());
                    if (fsoa_fqdn != rsoa_fqdn) {
                        LOG_ERROR << "Slave::sync - Invalid AXFR payload for " << fqdn_
                                  << " from master at " << remote_ep
                                  << ". fqdn in first received SOA is " << rsoa_fqdn
                                  << ". fqdn in second received SOA is " << fsoa_fqdn;
                        throw runtime_error{"Slave::sync - Invalid AXFR payload. First and second SOA has different labels."};
                    }
                    if (soa.serial() != rsoa_serial) {
                        LOG_ERROR << "Slave::sync - Invalid AXFR payload for " << fqdn_
                                  << " from master at " << remote_ep
                                  << ". serial in first received SOA was " << rsoa_serial
                                  << ". fqdn in second received SOA is " << soa.serial();
                        throw runtime_error{"Slave::sync - Invalid AXFR payload. First and second SOA has different serials."};
                    }

                    has_second_rr = true;
                }
            } // Not first


            auto key = labelsToFqdnKey(rr.labels());
            if (!current_fqdn.empty() && key != current_fqdn) {
                if (sb) {
                    sb->finish();
                    if (sb->rrCount()) {
                        trx->write(current_fqdn, sb->buffer(), false); // set new flag??
                    }
                }

                sb.emplace();
                current_fqdn = key.string();
            }

            if (!has_second_rr) {
                // Copy the RR
                sb->createRr(key, rr.type(), rr.ttl(), rr.rdata());
            } else {
                assert(sb->rrCount() == 0); // The fqdn must have changed back, and
                                            // triggered the emplace() above.
            }
        }
    }


    // At the first reply-message, start a DB transaction and delete the old RR records
    // Then add the new RR records as we receive them.
    // Before committing the transaction, check if the Slave is obsolete (done_ == true)

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

void Slave::sendQuestion(boost::asio::ip::tcp::socket &socket, uint16_t question, Slave::yield_t &yield)
{
    const auto remote_ep = socket.remote_endpoint();
    boost::system::error_code ec;
    array<char, 2> size_buf = {};

    MessageBuilder mb;
    mb.setMaxBufferSize(512);
    // TODO: Add a random ID
    mb.createHeader(1, false, MessageBuilder::Header::OPCODE::QUERY, false);
    mb.addQuestion(fqdn_, question);
    mb.finish();

    // Send question
    setValueAt(size_buf, 0, mb.span().size());
    array<boost::asio::const_buffer, 2> buffers = {
        to_asio_buffer(size_buf),
        to_asio_buffer(mb.span())
    };

    socket.async_send(buffers, yield[ec]);
    if (ec) {
        LOG_WARN << "Slave::sendQuestion - Failed to send Query to "
                 << remote_ep << " regarding type=" << to_string(question) << " for " << fqdn_;
        throw runtime_error{"Slave::sendQuestion: Failed to send query"};
    }
}

Message Slave::getReply(boost::asio::ip::tcp::socket &socket, Slave::buffer_t &buffer, Slave::yield_t &yield)
{
    const auto remote_ep = socket.remote_endpoint();
    boost::system::error_code ec;
    array<char, 2> size_buf = {};

    // Read reply-length
    socket.async_receive(to_asio_buffer(size_buf), yield[ec]);
    if (ec) {
        LOG_WARN << "Slave::getReply - Failed to read reply-len from "
                 << remote_ep << " regarding " << fqdn_;
        throw runtime_error{"Slave::getReply: Failed to read reply-len"};
    }

    // Read reply
    const auto reply_len = get16bValueAt(size_buf, 0);
    if (reply_len == 0 || reply_len > 1024) {
        LOG_WARN << "Slave::getReply - Invalid reply-len " << reply_len
                << " for question to "
                 << remote_ep << " regarding " << fqdn_;
        throw runtime_error{"Slave::getReply: Failed to get Reply - Invalid reply-len"};
    }

    buffer.resize(reply_len);
    socket.async_receive(to_asio_buffer(size_buf), yield[ec]);
    if (ec) {
        LOG_WARN << "Slave::getReply - Failed to read reply from "
                 << remote_ep << " regarding " << fqdn_;
        throw runtime_error{"Slave::getReply: Failed to read reply"};
    }

    return {buffer};
}


} // ns
