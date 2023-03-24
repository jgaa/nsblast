
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

void Slave::done() {
    done_ = true;
}

void Slave::setTimer(uint32_t secondsInFuture)
{
    LOG_TRACE << "Slave::setTimer Setting timer for " << fqdn_
              << ' ' << secondsInFuture << " seconds from now.";

    std::lock_guard<std::mutex> lock{mutex_};
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

    auto socket = TcpConnect(mgr_.ctx(), zone_.master().hostname(),
                             to_string(PB_GET(zone_.master(), port, 53)),
                             yield);

    current_remote_ep_ = socket.remote_endpoint();

    // TODO: Create a timer.
    //   In the timer:
    //   - Check for connection, read or write timeouts
    //   - Check if the Slave object is obsolete (done_ == true)

    if (isZoneUpToDate(socket, yield)) {
        return;
    }

    doAxfr(socket, yield);
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
    const auto current_remote_ep_ = socket.remote_endpoint();
    boost::system::error_code ec;
    array<char, 2> size_buf = {};

    LOG_TRACE << "Slave::sendQuestion - Sending Query of type "
              << question << " regarding " << fqdn_
              << " to " << current_remote_ep_;

    MessageBuilder mb;
    mb.setMaxBufferSize(512);
    mb.createHeader(++next_id, false, MessageBuilder::Header::OPCODE::QUERY, false);
    mb.addQuestion(fqdn_, question);
    mb.finish();

    // Send question
    setValueAt(size_buf, 0, static_cast<uint16_t>(mb.span().size()));
    array<boost::asio::const_buffer, 2> buffers = {
        to_asio_buffer(size_buf),
        to_asio_buffer(mb.span())
    };

    socket.async_send(buffers, yield[ec]);
    if (ec) {
        LOG_WARN << "Slave::sendQuestion - Failed to send Query to "
                 << current_remote_ep_ << " regarding type=" << to_string(question) << " for " << fqdn_;
        throw runtime_error{"Slave::sendQuestion: Failed to send query"};
    }
}

Message Slave::getReply(boost::asio::ip::tcp::socket &socket, Slave::buffer_t &buffer, Slave::yield_t &yield)
{
    boost::system::error_code ec;
    array<char, 2> size_buf = {};

    LOG_TRACE << "Slave::getReply - Starting to wait for reply regarding " << fqdn_
              << " from " << current_remote_ep_;


    // Read reply-length
    auto bytes = socket.async_receive(to_asio_buffer(size_buf), yield[ec]);
    if (ec) {
        LOG_WARN << "Slave::getReply - Failed to read reply-len from "
                 << current_remote_ep_ << " regarding " << fqdn_;
        throw runtime_error{"Slave::getReply: Failed to read reply-len"};
    }

    if (bytes == 0) {
        LOG_TRACE <<  "Slave::getReply - Received 0 bytes from "
                  << current_remote_ep_ << " regarding " << fqdn_
                  << ". Assuming the connection was closed by the peer.";
        throw runtime_error{"Slave::getReply: Connection closed"};
    }

    // Read reply
    const auto reply_len = get16bValueAt(size_buf, 0);

    LOG_TRACE << "Slave::getReply - Got message-lenght " << reply_len
              << " bytes for the reply regarding " << fqdn_
              << " from " << current_remote_ep_;

    if (reply_len < 12) {
        LOG_WARN << "Slave::getReply - Invalid reply-len " << reply_len
                << " for question to "
                 << current_remote_ep_ << " regarding " << fqdn_;
        throw runtime_error{"Slave::getReply: Failed to get Reply - Invalid reply-len"};
    }

    buffer.resize(reply_len);
    bytes = socket.async_receive(to_asio_buffer(buffer), yield[ec]);
    if (ec) {
        LOG_WARN << "Slave::getReply - Failed to read reply from "
                 << current_remote_ep_ << " regarding " << fqdn_;
        throw runtime_error{"Slave::getReply: Failed to read reply"};
    }

    if (bytes == 0) {
        LOG_TRACE <<  "Slave::getReply - Received 0 bytes from "
                  << current_remote_ep_ << " regarding " << fqdn_
                  << " while reading a message. Assuming the connection was closed by the peer.";
        throw runtime_error{"Slave::getReply: Connection closed"};
    }

    return {buffer};
}

void Slave::checkIfDone()
{
    if (done_) {
        LOG_DEBUG << "Slave::sync - Aborting AXFR zone for " << fqdn_
                  << " because the instance on this replicator is obsolete.";
        throw runtime_error{"Slave::sync - Aborting AXFR. My instance is obsolete!"};
    }
}

bool Slave::isZoneUpToDate(boost::asio::ip::tcp::socket &socket, Slave::yield_t &yield)
{
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
            LOG_ERROR << "Slave::isZoneUpToDate - The master server at "
                      << current_remote_ep_
                      << " for zone " << fqdn_
                      << " did not return a SOA RR for this fqdn. "
                      << "Is this the correct authortative server for this zone?";
            throw runtime_error{"Slave::isZoneUpToDate: Master server has no SOA for this zone."};
        }

        const auto rserial = soa->serial();
        LOG_TRACE << "Slave::isZoneUpToDate locval serial is " << serial
                  << ", remote serial is " << rserial;

        if (rserial != serial) {
            LOG_DEBUG << "Slave::isZoneUpToDate - SOA serials are; local=" << serial
                      << ", master=" << rserial
                      << ". I need to isZoneUpToDate against the master for zone " << fqdn_
                      << " at " << current_remote_ep_;
        } else {
            LOG_DEBUG << "Slave::isZoneUpToDate - SOA serial for " << fqdn_
                      << " are in isZoneUpToDate with the master at " << current_remote_ep_;
            return true;
        }
    }
    return false;
}

// Note: This implementation assumes that the RR's arrive sorted,
//       with all relevant RR's for a fqdn in one block (potentially
//       carried in multiple messages).
//       The RR's are not validated, like ensuring that a CNAME is not
//       mixed with other RR's.
void Slave::doAxfr(boost::asio::ip::tcp::socket &socket, Slave::yield_t &yield)
{
    // Send question
    checkIfDone();
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
        checkIfDone();
        auto reply = getReply(socket, buffer, yield);

        for(auto& rr : reply.getAnswers()) {
            if (has_second_rr) {
                LOG_ERROR << "Slave::sync - Invalid AXFR payload for " << fqdn_
                          << " from master at " << current_remote_ep_
                          << " There are more answers after the second SOA!";
                throw runtime_error{"Slave::sync - Invalid AXFR payload. More RR's after second SOA."};
            }

            if (first) {
                if (rr.type() != TYPE_SOA) {
                    LOG_ERROR << "Slave::sync - Invalid AXFR payload for " << fqdn_
                              << " from master at " << current_remote_ep_
                              << ". AXFR must start with SOA RR!";
                    throw runtime_error{"Slave::sync - Invalid AXFR payload."};
                }

                RrSoa soa{reply.span(), rr.offset()};
                rsoa_serial = soa.serial();
                rsoa_fqdn = toLower(soa.labels().string());
                if (fqdn_ != rsoa_fqdn) {
                    LOG_ERROR << "Slave::sync - Invalid AXFR payload for " << fqdn_
                              << " from master at " << current_remote_ep_
                              << ". fqdn in first received SOA is " << rsoa_fqdn
                              << ". I expected " << fqdn_;
                    throw runtime_error{"Slave::sync - Invalid AXFR payload. Unexpected fqdn in first SOA."};
                }
                id = reply.header().id();
                first = false;

                // Delete all existing Entries in the local database for this zone
                trx->remove({fqdn_}, true);

            } /* first */ else {
                if (rr.type() == TYPE_SOA) {
                    // There can only be two SOA RR's in the reply, the first and the last
                    // entry, and they must be the same.
                    RrSoa soa{reply.span(), rr.offset()};
                    auto fsoa_fqdn = toLower(soa.labels().string());
                    if (fsoa_fqdn != rsoa_fqdn) {
                        LOG_ERROR << "Slave::sync - Invalid AXFR payload for " << fqdn_
                                  << " from master at " << current_remote_ep_
                                  << ". fqdn in first received SOA is " << rsoa_fqdn
                                  << ". fqdn in second received SOA is " << fsoa_fqdn;
                        throw runtime_error{"Slave::sync - Invalid AXFR payload. First and second SOA has different labels."};
                    }
                    if (soa.serial() != rsoa_serial) {
                        LOG_ERROR << "Slave::sync - Invalid AXFR payload for " << fqdn_
                                  << " from master at " << current_remote_ep_
                                  << ". serial in first received SOA was " << rsoa_serial
                                  << ". fqdn in second received SOA is " << soa.serial();
                        throw runtime_error{"Slave::sync - Invalid AXFR payload. First and second SOA has different serials."};
                    }

                    has_second_rr = true;
                }
            } // Not first


            auto key = labelsToFqdnKey(rr.labels());
            if (current_fqdn.empty() || key != current_fqdn) {
                if (sb) {
                    sb->finish();
                    if (sb->rrCount()) {
                        LOG_TRACE << "Slave::sync - During AXFR payload for " << fqdn_
                                  << " from master at " << current_remote_ep_
                                  << " Writing " << sb->rrCount() << " RR's for " << current_fqdn;
                        trx->write({current_fqdn}, sb->buffer(), false); // set new flag??
                    }
                }

                sb.emplace();
                current_fqdn = key.string();
            }

            if (!has_second_rr) {
                const auto type = rr.type();
                // Copy the RR
                if (type == TYPE_OPT) [[unlikely]] {
                    LOG_WARN << "Slave::sync - During AXFR payload for " << fqdn_
                             << " from master at " << current_remote_ep_
                             << " Ignoring OPT RR for " << current_fqdn;
                } else {
                    // All is well
                    sb->createRr(key, type, rr.ttl(), rr.rdata());
                }
            } else {
                // The fqdn must have changed back, and
                // triggered the emplace() above.
                if(sb->rrCount() != 0) {
                    LOG_ERROR << "Slave::sync - Invalid AXFR payload for " << fqdn_
                              << " from master at " << current_remote_ep_
                              << ". Server is sending RR's after second SOA. That is not valid DNS speak!";
                    throw runtime_error{"Slave::sync - Invalid AXFR payload. Received RR's after second SOA!"};
                }
            }
        } // RR's in one reply-message
    } // Reply message(s)

    checkIfDone();

    LOG_DEBUG << "Slave::sync - Committing AXFR zone for " << fqdn_
              << " received from from master at " << current_remote_ep_;
    trx->commit();

    // TODO: In the future, may be better to cache small zones in memory and
    //       do a fast transaction update, and cache large zones on local storage
    //       so we don't keeps the transaction open for a long time.
}


} // ns
