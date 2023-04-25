
#include <boost/asio.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/spawn.hpp>
#include <utility>

#include "SlaveMgr.h"
#include "Slave.h"

#include "nsblast/logging.h"
#include "nsblast/util.h"
#include "proto_util.h"

using namespace std;
using namespace std::string_literals;


namespace nsblast::lib {

namespace {

class ZoneMerge {
public:
    using rr_info_t = RrInfo;

    ZoneMerge() = default;

    void addExisting(ResourceIf::TransactionIf& trx, string_view fqdn) {
        const auto e = trx.lookup(fqdn);
        if (!e.empty()) {
            for(const auto& rr : e) {
                existing_.push_back(sb_.addRr(rr).rrInfo());
            }
        }
    }

    void addDeleted(const Rr& rr) {
        if (deleted_span_.empty()) {
            // Assume all RR's in the same merge() is from the same buffer
            deleted_span_ = rr.span();
        }
        deleted_.push_back(rr.rrInfo());
    };

    void addAdded(const Rr& rr) {
        if (added_span_.empty()) {
            // Assume all RR's in the same merge() is from the same buffer
            added_span_ = rr.span();
        }
        added_.push_back(rr.rrInfo());
    };

    /*! Merge the three sources.
     *
     *  Delete the deleted from the existing.
     *  Add the added to the existing.
     *  Make sure there is only instance of any RR in existing when done.
     *
     *  After the merge, deleted_ and added_ are reset.
     */
    void merge() {
        if (added_.empty() && deleted_.empty()) {
            return;
        }

        changed_ = true;

        // Note: Dependency onm left or right buffer is identified by 'left' flag in each rrinfo
        struct Compare {
            Compare(span_t dleft, span_t dright)
                : dleft_{dleft}, dright_{dright} {}

            bool operator()(const RrInfo& left, const RrInfo& right) const noexcept {
                const auto ls = left.dataSpanAfterLabel(left.left ? dleft_ : dright_);
                const auto rs = right.dataSpanAfterLabel(right.left ? dleft_ : dright_);

                auto res = memcmp(ls.data(), rs.data(), min(ls.size(), rs.size()));
                if (res == 0) {
                    return ls.size() < rs.size();
                }
                return res < 0;
            }

        private:
            const span_t dleft_, dright_;
        };

        sort(deleted_.begin(), deleted_.end(), Compare(deleted_span_, deleted_span_));

        sort(added_.begin(), added_.end(), Compare(added_span_, added_span_));

        // Erase any deleted items from existing_.
        {
            for(auto& e : deleted_) { e.left = true; }
            for(auto& e : existing_) { e.left = false; }

            Compare cmp{deleted_span_, sb_.buffer()};
            if (!deleted_.empty()) {
                need_new_builder_ = true;
                existing_.erase(
                            remove_if(existing_.begin(), existing_.end(),
                                      [&](const rr_info_t& left) {
                                return find_if(deleted_.begin(), deleted_.end(),
                                            [&](const rr_info_t& right) {
                                    return cmp(left, right);
                                }) != deleted_.end();
                }));
            }
        }

        // Make a list of "added" items, not currently in existing_
        // Get new/changed entries
        for(auto& e : added_) { e.left = true; }
        for(auto& e : existing_) { e.left = false; }

        vector<rr_info_t> add;
        set_difference(added_.begin(), added_.end(), existing_.begin(), existing_.end(),
                       back_inserter(add), Compare(added_span_, sb_.buffer()));

        for(const auto& i: add) {
            existing_.push_back(sb_.addRr(i.rr(added_span_)).rrInfo());
        }

        added_.clear();
        deleted_.clear();
        added_span_ = {};
        deleted_span_ = {};
    }

    void save(ResourceIf::TransactionIf& trx, string_view fqdn) {
        // Must already be merged
        assert(deleted_.empty());
        assert(added_.empty());

        span_t data = {};
        optional<StorageBuilder> sb;

        if (need_new_builder_) {
            // We have deleted entries. We need a new builder.
            sb.emplace();
            for(const auto& i: existing_) {
                sb->addRr(i.rr(data));
            }

            sb->finish();
            if (sb->rrCount() > 0) {
                data = sb->buffer();
            }
        } else {
            sb_.finish();
            if (sb_.rrCount() > 0) {
                data = sb_.buffer();
            }
        }

        if (data.empty()) {
            trx.remove({fqdn});
        } else {
            trx.write({fqdn}, data, false);
        }
    }

    void replaceSoa(const RrSoa& soa) {
        sb_.replaceSoa(soa);
    }

private:
    std::vector<rr_info_t> deleted_;
    std::vector<rr_info_t> added_;
    std::vector<rr_info_t> existing_;
    span_t deleted_span_;
    span_t added_span_;
    StorageBuilder sb_;
    bool need_new_builder_ = false;
    bool changed_ = false;
};

// Cache for all changes received during the processing a AXFR or IXFR reply.
// Cannot deal with really large zones. That's probably fine, since
// we will use non-standard methods to handle incremental updates
// for a nsblast cluster.
class ZoneMerger {
public:

    ZoneMerger(ResourceIf::TransactionIf& trx, string_view zoneFqdn, bool fetchExisting = true)
        :trx_{trx}, zone_fqdn_{zoneFqdn}, fetch_existing_{fetchExisting} {
        get(zone_fqdn_);
    }

    ZoneMerge& get(string_view fqdn) {
        auto key = toLower(fqdn);

        if (auto it = changes_.find(key); it != changes_.end()) {
            return it->second;
        }

        auto& z = changes_[key];
        if (fetch_existing_) {
            z.addExisting(trx_, key);
        }
        return z;
    }

    void merge() {
        for(auto& [_, z] : changes_) {
            z.merge();
        }
    }

    void save() {
        for(auto& [key, z] : changes_) {
            z.save(trx_, key);
        }
    }

    void setSoa(const RrSoa& soa) {
        if (fetch_existing_) {
            get(zone_fqdn_).replaceSoa(soa);
        } else {
            get(zone_fqdn_).addAdded(soa);
        }
    }

    std::map<std::string, ZoneMerge> changes_;
    ResourceIf::TransactionIf& trx_;
    string_view zone_fqdn_;
    bool fetch_existing_ = true;
};

} // anon ns

Slave::Slave(SlaveMgr &mgr, std::string_view fqdn, pb::Zone  zone)
    : mgr_{mgr}, fqdn_{fqdn}, zone_{std::move(zone)}, schedule_{mgr.ctx()}
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
    if (notifications_) {
        secondsInFuture = 1;
        LOG_TRACE << "Slave::setTimer Setting timer for " << fqdn_
                  << ' ' << secondsInFuture << " seconds from now. (have "
                  << notifications_ << " notifications!";
    } else {
        LOG_TRACE << "Slave::setTimer Setting timer for " << fqdn_
                  << ' ' << secondsInFuture << " seconds from now.";
    }

    std::lock_guard<std::mutex> lock{mutex_};
    schedule_.expires_from_now(boost::posix_time::seconds{secondsInFuture});
    schedule_.async_wait([self=shared_from_this()](boost::system::error_code ec) {
        if (self->done_) {
            LOG_TRACE << "Slave::setTimer - Timer for sync with zone " << self->fqdn_
                      << " discovered that the Slave instance is done.";
            return;
        }
        if (ec) {
            if (ec == boost::asio::error::operation_aborted) {
                LOG_TRACE << "Slave::setTimer: Timer aborted.";
            } else {
                LOG_WARN << "Slave::setTimer - Timer for sync with zone " << self->fqdn_
                     << " failed with error: " << ec.message();
            }
        }

        self->sync();
    });
}

void Slave::sync()
{
    /* The logic is that the slave is either waiting for the timer
     * or syncing. After a sync, a new timer is initiated and until
     * is times out or is cancelled, there is no sync.
     *
     * If a notifivation is received, the timer is cancelled, and notifications_
     * incremented. The notification will not interrupt an ongoing sync.
     *
     */
    notifications_ = 0;
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

    {
        lock_guard<mutex> lock{mutex_};
        current_remote_ep_ = socket.remote_endpoint();
    }

    // TODO: Create a timer.
    //   In the timer:
    //   - Check for connection, read or write timeouts
    //   - Check if the Slave object is obsolete (done_ == true)

    const auto strategy = PB_GET(zone_.master(), strategy, "axfr");

    if (strategy == "ixfr") {
        return doIxfr(socket, yield);
    } if (strategy == "axfr") {
        if (isZoneUpToDate(socket, yield)) {
            return;
        }

        return doAxfr(socket, yield);
    }

    LOG_ERROR << "Slave::sync: Unknown sync strategy '" << strategy
              << "' for zone " << fqdn_
              << ". The zone can not be synced with the master server at "
              << current_remote_ep_
              << " until the configuration has been corrected.";

    throw runtime_error{"Unknown sync strategy: "s + strategy};
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

void Slave::sendQuestion(boost::asio::ip::tcp::socket &socket,
                         uint16_t question,
                         uint32_t serial, // for ixfr
                         Slave::yield_t &yield)
{
    const auto current_remote_ep_ = socket.remote_endpoint();
    boost::system::error_code ec;
    array<char, 2> size_buf = {};

    LOG_TRACE << "Slave::sendQuestion - Sending Query of type "
              << question << " regarding " << fqdn_
              << " to " << current_remote_ep_;

    MessageBuilder mb;
    mb.setMaxBufferSize(512);
    auto hdr = mb.createHeader(++next_id, false, MessageBuilder::Header::OPCODE::QUERY, false);
    mb.addQuestion(fqdn_, question);

    if (question == QTYPE_IXFR) {
        MutableRrSoa soa{serial};
        mb.addRr(soa, hdr, MessageBuilder::Segment::AUTHORITY);
    }
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
        throw runtime_error{"Slave::sync - Aborting IXFR/AXFR. My instance is obsolete!"};
    }
}

bool Slave::isZoneUpToDate(boost::asio::ip::tcp::socket &socket, Slave::yield_t &yield)
{
    // Get our soa
    auto serial = localSerial();
    if (serial) {
        // Get master's soa so we can compare
        sendQuestion(socket, TYPE_SOA, 0, yield);

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
        LOG_TRACE << "Slave::isZoneUpToDate local serial is " << serial
                  << ", remote serial is " << rserial;

        if (rserial != serial) {
            LOG_DEBUG << "Slave::isZoneUpToDate - SOA serials are; local=" << serial
                      << ", master=" << rserial
                      << ". I need to isZoneUpToDate against the master for zone " << fqdn_
                      << " at " << current_remote_ep_;
        } else {
            LOG_DEBUG << "Slave::isZoneUpToDate - SOA serial " << serial
                      << " for " << fqdn_
                      << " is in sync with the master at " << current_remote_ep_;
            return true;
        }
    }
    return false;
}

void Slave::doAxfr(boost::asio::ip::tcp::socket &socket, Slave::yield_t &yield)
{
    // Send question
    checkIfDone();
    sendQuestion(socket, QTYPE_AXFR, 0, yield);

    auto trx = mgr_.db().transaction();
    trx->remove({fqdn_}, true);
    handleIxfrPayloads(*trx, socket, 0, yield);
}

void Slave::doIxfr(boost::asio::ip::tcp::socket &socket, Slave::yield_t &yield)
{
    checkIfDone();

    auto current_serial = 0;
    auto trx =  mgr_.db().transaction();
    {
        auto e = trx->lookup(fqdn_);
        if (e.empty() || !e.flags().soa) {
            LOG_INFO << "Slave::doIxfr: Failed to lookup existing SOA. "
                     << "Falling back to a full zone transfer for " << fqdn_;
            trx.reset();
            return doAxfr(socket, yield);
        }

        auto soa = e.getSoa();
        current_serial = soa.serial();
        if (!current_serial) {
            LOG_WARN << "Slave::doIxfr: Current SOA serial is zero. "
                     << "Falling back to a full zone transfer for " << fqdn_;
            trx.reset();
            return doAxfr(socket, yield);
        }
    }

    sendQuestion(socket, QTYPE_IXFR, current_serial, yield);
    handleIxfrPayloads(*trx, socket, current_serial, yield);
}

void Slave::handleIxfrPayloads(ResourceIf::TransactionIf &trx,
                               boost::asio::ip::tcp::socket &socket,
                               uint32_t mySerial, Slave::yield_t &yield)
{
    bool isIxfr = mySerial != 0;

    enum class Stage {
        NEED_FIRST_SOA,
        HAVE_FIRST_SOA,
        HAVE_IXFR_DEL_SOA,
        HAVE_IXFR_ADD_SOA,
        HAVE_FINAL_SOA
    } stage = Stage::NEED_FIRST_SOA;

    buffer_t buffer;
    string rsoa_fqdn;
    uint32_t rsoa_current_serial = 0;
    uint32_t prev_serial = 0;
    uint16_t id = 0; // All messages must be for this id
    MutableRrSoa firstSoa;

    optional<ZoneMerger> merger;
    merger.emplace(trx, fqdn_, isIxfr);

    while(stage != Stage::HAVE_FINAL_SOA) {
        checkIfDone();
        auto reply = getReply(socket, buffer, yield);
        checkIfDone();

        for(auto& rr : reply.getAnswers()) {
            if (stage == Stage::HAVE_FINAL_SOA) [[unlikely]] {
                LOG_ERROR << "Slave::handleIxfrPayloads - Invalid I/AXFR payload for " << fqdn_
                          << " from master at " << current_remote_ep_
                          << " There are more answers after the second SOA!";
                throw runtime_error{"Slave::sync - Invalid I/AXFR payload. More RR's after second SOA."};
            }

            if (stage == Stage::NEED_FIRST_SOA) [[unlikely]] {
                if (rr.type() != TYPE_SOA) {
                    LOG_ERROR << "Slave::handleIxfrPayloads - Invalid I/AXFR payload for " << fqdn_
                              << " from master at " << current_remote_ep_
                              << ". I/AXFR must start with SOA RR!";
                    throw runtime_error{"Slave::sync - Invalid I/AXFR payload."};
                }


                RrSoa soa{reply.span(), rr.offset()};
                rsoa_current_serial = soa.serial();
                rsoa_fqdn = toLower(soa.labels().string());
                if (fqdn_ != rsoa_fqdn) {
                    LOG_ERROR << "Slave::handleIxfrPayloads - Invalid AXFR payload for " << fqdn_
                              << " from master at " << current_remote_ep_
                              << ". fqdn in first received SOA is " << rsoa_fqdn
                              << ". I expected " << fqdn_;
                    throw runtime_error{"Slave::sync - Invalid AXFR payload. Unexpected fqdn in first SOA."};
                }
                id = reply.header().id();
                firstSoa = soa;
                stage = Stage::HAVE_FIRST_SOA;
                continue;
            } // NEED_FIRST_SOA

            if (rr.type() == TYPE_SOA) [[unlikely]] {
                // Try to figure out which soa this is...

                RrSoa soa{reply.span(), rr.offset()};
                rsoa_fqdn = toLower(soa.labels().string());
                if (fqdn_ != rsoa_fqdn) {
                    LOG_ERROR << "Slave::handleIxfrPayloads - Invalid I/AXFR payload for " << fqdn_
                              << " from master at " << current_remote_ep_
                              << ". fqdn in received SOA is " << rsoa_fqdn
                              << ". I expected " << fqdn_;
                    throw runtime_error{"Slave::sync - Invalid AXFR payload. SOA's must have the same labels."};
                }
                const auto this_serial = soa.serial();
                if (this_serial == rsoa_current_serial) {
                    if (isIxfr && stage == Stage::HAVE_IXFR_DEL_SOA) {
                        // For IXFR, we get the current soa twice, first
                        // as the lead-in for the last ADD segment, and
                        // this looks like the case here.
                        stage = Stage::HAVE_IXFR_ADD_SOA;
                    } else {
                        stage = Stage::HAVE_FINAL_SOA;
                        merger->setSoa(soa); // This must be the soa in the final result.
                    }
                } else if (this_serial < rsoa_current_serial) {
                    if (!isIxfr) {
                        LOG_ERROR << "Slave::handleIxfrPayloads - Invalid AXFR payload for " << fqdn_
                                  << " from master at " << current_remote_ep_
                                  << ". serial in received SOA is " << this_serial
                                  << ". I expected " << rsoa_current_serial;
                        throw runtime_error{"Slave::sync - Invalid AXFR payload. SOA's must have the same serial."};
                    }
                    if (stage == Stage::HAVE_FIRST_SOA
                            || stage == Stage::HAVE_IXFR_ADD_SOA ) {
                        stage = Stage::HAVE_IXFR_DEL_SOA;
                    } else if (stage == Stage::HAVE_IXFR_DEL_SOA) {
                        stage = Stage::HAVE_IXFR_ADD_SOA;
                        if (this_serial <= prev_serial) {
                            LOG_ERROR << "Slave::handleIxfrPayloads - Invalid IXFR payload for " << fqdn_
                                      << " from master at " << current_remote_ep_
                                      << " sent IXFR ADD sequence in the wrong order. "
                                      << ". Prev serial was " << prev_serial
                                      << ", this serial is " << this_serial;
                            throw runtime_error{"Slave::sync - Invalid IXFR payload. IXFR ADD sequence in the wrong order."};
                        }
                    }
                } else {
                    LOG_ERROR << "Slave::handleIxfrPayloads - Invalid I/AXFR payload for " << fqdn_
                              << " from master at " << current_remote_ep_
                              << " sent an invalid I/AXFR SOA. "
                              << ", this serial is " << this_serial
                              << ". It cannot be higher than the lead-in serial "
                              << rsoa_current_serial;
                    throw runtime_error{"Slave::sync - Invalid I/AXFR payload. Trailing SOA has higher serial than first SOA."};
                }

                prev_serial = soa.serial();
            } /* if TYPE_SOA */ else [[likely]] {
                if (stage == Stage::HAVE_IXFR_ADD_SOA) {
add:
                    merger->get(rr.labels().string()).addAdded(rr);
                } else if (stage == Stage::HAVE_IXFR_DEL_SOA) {
                    merger->get(rr.labels().string()).addDeleted(rr);
                } else if (stage == Stage::HAVE_FIRST_SOA) [[unlikely]] {
                    if (isIxfr) {
                        // This looks like an AXFR reply to an IXFR request.
                        // Fall back to AXFR mode.

                        LOG_TRACE << "Slave::handleIxfrPayloads - Receiving AXFR payload to IXFR request for " << fqdn_
                                  << " from master at " << current_remote_ep_
                                  << ". (This is OK).";

                        isIxfr = false;
                        merger.emplace(trx, fqdn_, false);
                        trx.remove({fqdn_}, true);
                    }

                    // Not pretty, but avoiding passing trough two if's for the same operation
                    stage = Stage::HAVE_IXFR_ADD_SOA;
                    goto add;
                }
            }

        } // loop over all answer rr's in one (of several) reply

        merger->merge();
    } // while not finished

    merger->save();
    checkIfDone();

    LOG_DEBUG << "Slave - Committing zone update for " << fqdn_
              << " with serial " << rsoa_current_serial
              << " received from from master at " << current_remote_ep_
              << " using " << (isIxfr ? "IXFR" : "AXFR");
    trx.commit();
}

boost::asio::ip::tcp::endpoint Slave::remoteEndpoint() const noexcept
{
    std::lock_guard<std::mutex> lock{mutex_};
    return current_remote_ep_;
}

void Slave::onNotify(const boost::asio::ip::address& address)
{
    std::lock_guard<std::mutex> lock{mutex_};
    if (done_) {
        return;
    }

    if (current_remote_ep_.address() != address) {
        LOG_WARN << "Slave::onNotify: Received NOTIFY from address " << address
                 << " regarding zone " << fqdn_
                 << ". My primary server's is " << current_remote_ep_;
        return;
    }

    LOG_DEBUG << "Slave::onNotify: Acting on NOTIFY message for "
              << fqdn_ << " from " << address;

    ++notifications_;
    boost::system::error_code ec;
    schedule_.cancel(ec);
}

} // ns
