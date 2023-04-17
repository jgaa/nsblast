#pragma once

#include <set>
#include <chrono>
#include <memory>

#include <boost/unordered/unordered_flat_map.hpp>

#include "nsblast/nsblast.h"
#include "nsblast/Server.h"
#include "nsblast/DnsMessages.h"

namespace nsblast::lib {

class Notifications {
public:
    using udp_t = boost::asio::ip::udp;
    using tcp_t = boost::asio::ip::tcp;

    class Notifier : public std::enable_shared_from_this<Notifier> {
    public:
        using endpoint_t = std::variant<udp_t::endpoint, tcp_t::endpoint>;

        Notifier(Notifications& parent, std::string_view zoneFqdn)
            : parent_{parent}, id_{parent_.server().getNewId()}
            , timer_{parent.server().ctx()}, fqdn_{zoneFqdn}
            , expires_{std::chrono::steady_clock::now() + std::chrono::seconds(120)}
        {
            init();
        }

        void notified(const endpoint_t& ep);
        void cancel();
        bool done() const;
        uint32_t id() const noexcept {
            return id_;
        }
        void cancelTimer();

    private:
        void init();
        void resolve(boost::asio::yield_context& yield);
        void process(boost::asio::yield_context& yield);
        void notify(const endpoint_t& ep);

        boost::asio::yield_context *yield_ = {};
        MutableRrSoa soa_;
        Notifications& parent_;
        const uint32_t id_;
        boost::asio::deadline_timer timer_;
        std::vector<endpoint_t> pending_;
        mutable std::mutex mutex_;
        const std::string fqdn_;
        const std::chrono::steady_clock::time_point expires_;
        bool done_ = false;
        std::shared_ptr<MessageBuilder> mb_;
    };


    Notifications(Server& engine)
        : server_{engine} {}

    /*! Start notifying slave servers */
    void notify(const std::string& zoneFqdn);

    /*! Got ack for a notification */
    void notified(const std::string& zoneFqdn,
                  const Notifier::endpoint_t& ep,
                  uint32_t id);

    /*! Done with the Notifier for this zone/ request-id */
    void done(const std::string& zoneFqdn, uint32_t id);

    Server& server() const noexcept {
        return server_;
    }

private:
    std::shared_ptr<Notifier> getNotifier(const std::string& zoneFqdn, uint32_t id);

    Server& server_;
    boost::unordered_flat_map<std::string, std::shared_ptr<Notifier>> notifiers_;
    std::mutex mutex_;
};

} // ns


std::ostream& operator << (std::ostream& out, const nsblast::lib::Notifications::Notifier::endpoint_t& ep);
