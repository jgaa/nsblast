
#include <set>
#include <variant>

#include <boost/asio/spawn.hpp>
#include <boost/scope_exit.hpp>

#include "nsblast/ResourceIf.h"
#include "Notifications.h"
#include "nsblast/DnsEngine.h"
#include "nsblast/logging.h"
#include "nsblast/util.h"

using namespace std;
using namespace std::string_literals;

std::ostream& operator << (std::ostream& out, const nsblast::lib::Notifications::Notifier::endpoint_t& ep) {
    if (holds_alternative<nsblast::lib::Notifications::udp_t::endpoint>(ep)) {
        return out << get<nsblast::lib::Notifications::udp_t::endpoint>(ep);
    }
    if (holds_alternative<nsblast::lib::Notifications::tcp_t::endpoint>(ep)) {
        return out << get<nsblast::lib::Notifications::tcp_t::endpoint>(ep);
    }
    return out;
}

namespace nsblast::lib {

void Notifications::Notifier::notified(const Notifications::Notifier::endpoint_t &ep)
{
    if (done()) {
        return;
    }

    LOG_TRACE << "Notifications::Notifier::notified - Notification for "
              << fqdn_ << "/" << id_ << " got ACK from " << get<0>(ep);

    lock_guard<mutex> lock{mutex_};
    pending_.erase(std::remove(pending_.begin(), pending_.end(), ep), pending_.end());
    if (pending_.empty()) {
        cancelTimer();
    }
}

void Notifications::Notifier::cancel()
{   
    done_ = true;
    cancelTimer();
}

bool Notifications::Notifier::done() const
{
    LOG_TRACE << "Notifications::Notifier::done() - done_=" << done_
              << ", expired=" << (expires_ >= chrono::steady_clock::now());
    return done_ || expires_ <= chrono::steady_clock::now();
}

void Notifications::Notifier::cancelTimer()
{
    lock_guard<mutex> lock{mutex_};
    if (yield_ && !yield_->cancelled()) {
        boost::asio::post(yield_->get_executor(), [w=weak_from_this()] {
            if (auto self = w.lock()) {
                boost::system::error_code ec;
                self->timer_.cancel(ec);
            }
        });
    }
}

void Notifications::Notifier::init()
{
    LOG_TRACE << "Notifications::Notifier::init() Initiating notifications for "
              << fqdn_ << "/" << id_;

    mb_ = make_shared<MessageBuilder>();
    mb_->setMaxBufferSize(512);
    mb_->createHeader(id(), false, MessageBuilder::Header::OPCODE::NOTIFY, false);
    mb_->addQuestion(fqdn_, TYPE_SOA);

    boost::asio::spawn(parent_.server().ctx(), [this] (boost::asio::yield_context yield) {
        auto self = shared_from_this();
        try {
            ScopedExit se{[this] {
                lock_guard<mutex> lock{mutex_};
                yield_ = {};
            }};
            yield_ = &yield;
            resolve(yield);
            process(yield);
            parent_.server().notifications().done(fqdn_, id());
        }  catch (const exception& ex) {
            LOG_ERROR << "Notifications::Notifier::init: Processing failed with exception: "
                      << ex.what();
        }
        LOG_TRACE << "Notifications::Notifier::init - Init complete for " << fqdn_ << "/" << id_;
    }, boost::asio::detached);
}

void Notifications::Notifier::resolve(boost::asio::yield_context &yield)
{
    vector<string> hosts;
    auto trx = parent_.server().resource().transaction();

    auto e = trx->lookup({fqdn_});
    if (e.empty() || !e.flags().soa) {
        throw runtime_error{"Notifications::Notifier::resolve: Failed to load SOA for zone "s
                            + string{fqdn_}};
    }

    soa_ = e.getSoa();

    for(const auto& rr : e) {
        if (rr.type() == TYPE_NS) {
            RrNs ns{e.buffer(), rr.offset()};
            hosts.emplace_back(ns.ns().string());
        }
    }

    if (hosts.empty()) {
        LOG_WARN << "Notifications::Notifier::resolve: Found no relevant NS records to notify for zone "
                 << fqdn_;
        throw runtime_error{"Found no relevant NS records to notify"};
    }

    for(const auto& host : hosts) {
        auto ne = trx->lookup({host});
        if (ne.empty()) {
            LOG_TRACE << "Notifications::Notifier::resolve: Deferring NS "
                      << host << " for zone " << fqdn_
                      << " to the system resolver.";
            udp_t::resolver resolver{yield.get_executor()};

            // TODO-MAYBE: Would be better to resolve the hosts in parallel.
            boost::system::error_code ec;
            const auto res = resolver.async_resolve(host,
                                                    to_string(parent_.server().config().dns_notify_to_port),
                                                    yield[ec]);
            if (ec.failed()) {
                LOG_DEBUG_N << "Failed to resolve host " << host << " for DNS NOTIFY message";
                continue;
            }
            for(const auto& r : res) {
                pending_.emplace_back(udp_t::endpoint{r.endpoint()});
            }
            continue;
        }
        for(const auto& rr : ne) {
            const auto type = rr.type();
            if (type == TYPE_A || type == TYPE_AAAA) {
                const RrA a{ne.buffer(), rr.offset()};
                pending_.emplace_back(
                            udp_t::endpoint{a.address(),
                                            parent_.server().config().dns_notify_to_port});
            }
        }
    }

    if (pending_.empty()) {
        LOG_WARN << "Notifications::Notifier::process - Noone to notify for "
                 << fqdn_ << "/" << id_;
    }
}


void Notifications::Notifier::process(boost::asio::yield_context& yield)
{
    LOG_TRACE << "Notifications::Notifier::process() Called on "
              << fqdn_ << "/" << id_;

    int delay = 6;
    bool send_notifications = true;

    while(!done()) {

        {
            lock_guard<mutex> lock{mutex_};
            if (pending_.empty()) {
                // We are done;
                done_ = true;
                LOG_TRACE << "Notifications::Notifier::process() We are done - no further notifications on "
                          << fqdn_ << "/" << id_;
                return;
            }

            if (send_notifications) {
                for(const auto& ep: pending_) {
                    LOG_TRACE << "Notifications::Notifier::process() Preparing to send NOTIFY for "
                              << fqdn_ << "/" << id_ << " to " << ep;

                    notify(ep);
                }
            }

            timer_.expires_from_now(boost::posix_time::seconds(delay));
            delay = max(delay * 2, 60);
        }

        boost::system::error_code ec;
        timer_.async_wait(yield[ec]);
        if (ec) {
            send_notifications = false;
            if (ec == boost::asio::error::operation_aborted) {
                LOG_TRACE << "Notifications::Notifier::process() Timer for "
                          << fqdn_<< "/" << id_
                          << " aborted.";
                return;
            }

            LOG_WARN << "Notifications::Notifier::process() Timer has error: "
                     << ec.message();
        } else {
            send_notifications = true;
        }
    } // not expired

    LOG_DEBUG << "Notifications::Notifier::process() notifications expired or completed on "
              << fqdn_ << "/" << id_;
}

void Notifications::Notifier::notify(const Notifications::Notifier::endpoint_t &ep)
{
    // The send operations may outlive us, so we need the buffer in mb_ to be
    // valid unil the callback is called.
    // It's a send and forget operation, so we don't care if the operation failed.

    auto cb = [mb=mb_, w=weak_from_this(), ep=get<0>(ep)](boost::system::error_code ec) {

        if (auto self = w.lock()) {
            LOG_TRACE << "Notifications::Notifier::notify - CB returned status " << ec.message()
                      << " for NOTIFY message regarding " << self->fqdn_ << "/" << self->id() << " to " << ep;
        }
    };

    LOG_TRACE << "Notifications::Notifier::notify - Asking DnsEngine to seld NOTIFY message via existing UDP soket "
              << "regarding " << fqdn_ << "/" << id() << " to " << ep;
    parent_.server().dns().send(mb_->span(), get<0>(ep), cb);
}

void Notifications::notify(const std::string& zoneFqdn)
{
    lock_guard<mutex> lock{mutex_};
    if (auto it = notifiers_.find(zoneFqdn); it != notifiers_.end()) {
        LOG_TRACE << "Notifications::notify - Using existing Notifier entry for "
                   << zoneFqdn;
        it->second->cancel();
        it->second = make_shared<Notifier>(*this, zoneFqdn);
        return;
    }

    LOG_TRACE << "Notifications::notify - Creating new Notifier for " << zoneFqdn;
    notifiers_[zoneFqdn] = make_shared<Notifier>(*this, zoneFqdn);
}

void Notifications::notified(const string &zoneFqdn,
                             const Notifications::Notifier::endpoint_t &ep, uint32_t id)
{
    if (auto notifier = getNotifier(zoneFqdn, id)) {
        notifier->notified(ep);
    }
}

std::shared_ptr<Notifications::Notifier> Notifications::getNotifier(const string &zoneFqdn, uint32_t id)
{
    lock_guard<mutex> lock{mutex_};
    if (auto it = notifiers_.find(zoneFqdn); it != notifiers_.end()) {
        if (it->second->id() == id) {
            return it->second;
        }
    }

    return {};
}

void Notifications::done(const string& zoneFqdn, uint32_t id)
{
    lock_guard<mutex> lock{mutex_};
    if (auto it = notifiers_.find(zoneFqdn); it != notifiers_.end()) {
        if (it->second->id() == id) {
            LOG_TRACE << "Notifications::done - Removing completed Notifier for " << zoneFqdn << "/" << id;
            notifiers_.erase(zoneFqdn);
        }
    }
}

} // ns
