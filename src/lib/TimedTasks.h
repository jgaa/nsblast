#pragma once

#include <map>
#include <set>
#include <mutex>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/json.hpp>
#include "nsblast/Server.h"
#include "nsblast/ResourceIf.h"

namespace nsblast::lib {

// A handler's database effects and task completion are committed together. Handlers
// must be idempotent and must fence resource generations before changing anything.
class TimedTasks : public std::enable_shared_from_this<TimedTasks> {
public:
    using Object = boost::json::object;
    struct Execution {
        std::shared_ptr<void> guard; // Keep resource locks until the transaction commits.
        std::function<void()> committed;
    };
    using Handler = std::function<Execution(ResourceIf::TransactionIf&, const Object&)>;
    using Clock = std::function<int64_t()>;
    struct PermanentFailure : std::runtime_error { using runtime_error::runtime_error; };

    explicit TimedTasks(Server& server, Clock clock = utcNow);
    static int64_t utcNow();
    void registerHandler(std::string type, Handler handler);
    std::string schedule(ResourceIf::TransactionIf& trx, std::string_view type,
                         int64_t due, Object payload);
    void cancel(ResourceIf::TransactionIf& trx, std::string_view id, std::string_view reason);
    Object get(ResourceIf::TransactionIf& trx, std::string_view id) const;
    void start();
    void wake(); // Call after committing new work or changing runtime configuration.
    void stop();
    int64_t now() const { return clock_(); }
private:
    void pump();
    void run(std::string id, std::string claim);
    void fail(std::string_view id, std::string_view claim, bool permanent, std::string_view reason);
    static ResourceIf::RealKey dueKey(int64_t due, std::string_view id);
    void save(ResourceIf::TransactionIf& trx, const Object& task) const;
    bool writer() const;

    Server& server_;
    Clock clock_;
    boost::asio::strand<boost::asio::io_context::executor_type> strand_;
    boost::asio::steady_timer timer_;
    std::mutex mutex_; // Claims and retry updates; never held during handler execution.
    std::map<std::string, Handler> handlers_;
    std::set<std::string> active_;
    std::atomic_bool wake_pending_{false};
    bool stopped_ = true;
};
} // namespace nsblast::lib
