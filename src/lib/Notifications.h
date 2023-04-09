#pragma once

#include <set>
#include <chrono>
#include <memory>

#include "nsblast/nsblast.h"
#include "nsblast/DnsEngine.h"

namespace nsblast::lib {

class Notifications {
public:
    using udp_t = boost::asio::ip::udp;
    using tcp_t = boost::asio::ip::tcp;

    class Notifier : public std::enable_shared_from_this<Notifier> {
    public:
        using endpoint_t = std::variant<udp_t::endpoint, tcp_t::endpoint>;

        Notifier(Notifications& parent, std::string_view zoneFqdn)
            : parent_{parent}, id_{parent_.engine().getNewId()}
            , timer_{parent.engine().ctx()}, fqdn_{zoneFqdn}
            , expires_{std::chrono::steady_clock::now() + std::chrono::seconds{60}}
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


    Notifications(DnsEngine& engine)
        : engine_{engine} {}

    /*! Start notifying slave servers */
    void notify(const std::string& zoneFqdn);

    /*! Got ack for a notification */
    void notified(const std::string& zoneFqdn,
                  Notifier::endpoint_t& ep,
                  uint32_t id);

    /*! Done with the Notifier for this zone/ request-id */
    void done(std::string_view zoneFqdn, uint32_t id);

    DnsEngine& engine() const noexcept {
        return engine_;
    }

private:
    std::shared_ptr<Notifier> getNotifier(const std::string& zoneFqdn, uint32_t id);

    DnsEngine& engine_;
    boost::unordered_flat_map<std::string, std::shared_ptr<Notifier>> notifiers_;
    std::mutex mutex_;
};

inline void Notifications::done(std::string_view zoneFqdn, uint32_t id)
{

}

} // ns
