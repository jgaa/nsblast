
#include <set>
#include <variant>

#include <boost/asio/spawn.hpp>

#include "Notifications.h"

#include "nsblast/logging.h"

using namespace std;
using namespace std::string_literals;

namespace nsblast::lib {

void Notifications::Notifier::notified(const Notifications::Notifier::endpoint_t &ep)
{
    if (done()) {
        return;
    }

    LOG_TRACE << "Notifications::Notifier::notified - Notification for "
              << fqdn_ << "/" << id_ << " got ACK from " << get<0>(ep);

    lock_guard<mutex> lock{mutex_};
    pending_.erase(std::remove(pending_.begin(), pending_.end(), ep));
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
    return !done_ && expires_ >= chrono::steady_clock::now();
}

void Notifications::Notifier::cancelTimer()
{
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
    LOG_TRACE << "Notifications::Notifier::process() Initiating notifications for "
              << fqdn_ << "/" << id_;

    mb_ = make_shared<MessageBuilder>();
    mb_->setMaxBufferSize(512);
    mb_->createHeader(id(), false, MessageBuilder::Header::OPCODE::NOTIFY, false);
    mb_->addQuestion(fqdn_, TYPE_SOA);

    boost::asio::spawn(parent_.engine().ctx(), [this] (boost::asio::yield_context yield) {
        auto self = shared_from_this();
        try {
            yield_ = &yield;
            resolve(yield);
            process(yield);
        }  catch (const exception& ex) {
            LOG_ERROR << "Notifications::Notifier::init: Processing failed with exception: "
                      << ex.what();
        }
        LOG_TRACE << "Notifications::Notifier::init - Done with " << fqdn_ << "/" << id_;
    });
}

void Notifications::Notifier::resolve(boost::asio::yield_context &yield)
{
    vector<string> hosts;
    auto trx = parent_.engine().resource().transaction();

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
            const auto res = resolver.async_resolve(host, "53", yield);
            for(const auto& r : res) {
                pending_.emplace_back(udp_t::endpoint{r.endpoint()});
            }
            continue;
        }
        for(const auto& rr : ne) {
            const auto type = rr.type();
            if (type == TYPE_A || type == TYPE_AAAA) {
                const RrA a{ne.buffer(), rr.offset()};
                //udp_t::endpoint ep{a.address(), 53};
                pending_.emplace_back(udp_t::endpoint{a.address(), 53});
            }
        }
    }

    if (pending_.empty()) {
        LOG_WARN << "Notifications::Notifier::process - Noone to notify for "
                 << fqdn_ << "/" << id_;
        throw runtime_error{"Noone to notify"};
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
                return;
            }

            if (send_notifications) {
                for(const auto& ep: pending_) {
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

    parent_.engine().send(mb_->span(), get<0>(ep), cb);
}

void Notifications::notify(const std::string& zoneFqdn)
{
    lock_guard<mutex> lock{mutex_};
    if (auto it = notifiers_.find(zoneFqdn); it != notifiers_.end()) {
        it->second->cancel();
        it->second = make_shared<Notifier>(*this, zoneFqdn);
        return;
    }

    notifiers_[zoneFqdn] = make_shared<Notifier>(*this, zoneFqdn);
}

void Notifications::notified(const string &zoneFqdn, Notifications::Notifier::endpoint_t &ep, uint32_t id)
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

} // ns
