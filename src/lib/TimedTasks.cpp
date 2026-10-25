#include "TimedTasks.h"
#include "nsblast/Vars.h"
#include "nsblast/logging.h"
#include <boost/asio/post.hpp>
#include <boost/uuid/uuid_io.hpp>

namespace nsblast::lib {
namespace {
using Class = ResourceIf::RealKey::Class;
constexpr auto category = ResourceIf::Category::ACCOUNT;
std::string text(const boost::json::object& o, std::string_view k) { return std::string{o.at(k).as_string()}; }
int64_t number(const boost::json::object& o, std::string_view k) { return o.at(k).as_int64(); }
std::string uuid() { return boost::uuids::to_string(newUuid()); }
}

int64_t TimedTasks::utcNow() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
TimedTasks::TimedTasks(Server& server, Clock clock)
    : server_{server}, clock_{std::move(clock)}, strand_{boost::asio::make_strand(server.ctx())}, timer_{strand_} {}

void TimedTasks::registerHandler(std::string type, Handler handler) {
    std::lock_guard lock{mutex_};
    if (!stopped_) throw std::logic_error{"Register timed handlers before start"};
    if (!handlers_.emplace(std::move(type), std::move(handler)).second) throw std::logic_error{"Duplicate timed handler"};
}
ResourceIf::RealKey TimedTasks::dueKey(int64_t due, std::string_view id) {
    return {std::format("{:020}/{}", due, id), Class::TASK_DUE};
}
void TimedTasks::save(ResourceIf::TransactionIf& trx, const Object& task) const {
    trx.write({text(task, "id"), Class::TIMED_TASK}, boost::json::serialize(task), false, category);
}
TimedTasks::Object TimedTasks::get(ResourceIf::TransactionIf& trx, std::string_view id) const {
    std::string raw;
    if (!trx.read({id, Class::TIMED_TASK}, raw, category, false)) return {};
    return boost::json::parse(raw).as_object();
}
std::string TimedTasks::schedule(ResourceIf::TransactionIf& trx, std::string_view type, int64_t due, Object payload) {
    if (due < 0) throw std::invalid_argument{"Negative task deadline"};
    const auto id = uuid();
    Object task{{"version", 1}, {"id", id}, {"type", type}, {"due", due}, {"next", due},
                {"state", "pending"}, {"attempts", 0},
                {"max_retries", static_cast<int64_t>(server_.vars().snapshot()->timed_task_max_retries())},
                {"payload", std::move(payload)}, {"failure", ""}, {"claim", ""}};
    Object context;
    const auto& detail = task.at("payload").as_object();
    if (auto* tenant = detail.if_contains("tenant")) context["tenant"] = *tenant;
    if (auto* domain = detail.if_contains("domain")) context["resource"] = *domain;
    else if (auto* resource = detail.if_contains("resource")) context["resource"] = *resource;
    if (auto* generation = detail.if_contains("generation")) context["generation"] = *generation;
    for (const auto& item : context) task[item.key()] = item.value();
    save(trx, task);
    trx.write(dueKey(due, id), id, false, category);
    LOG_DEBUG << "Scheduled timed task " << id << " type=" << type << " due=" << due;
    return id;
}
void TimedTasks::cancel(ResourceIf::TransactionIf& trx, std::string_view id, std::string_view reason) {
    std::lock_guard lock{mutex_};
    auto task = get(trx, id);
    if (task.empty() || (text(task, "state") != "pending" && text(task, "state") != "running")) return;
    trx.remove(dueKey(number(task, "next"), id), false, category);
    task["state"] = "cancelled";
    task["payload"] = Object{};
    task["failure"] = reason;
    save(trx, task);
}
bool TimedTasks::writer() const { return !server_.isDone() && server_.role() != Server::Role::CLUSTER_FOLLOWER; }
void TimedTasks::start() {
    { std::lock_guard lock{mutex_}; stopped_ = false; }
    wake();
}
void TimedTasks::wake() {
    if (wake_pending_.exchange(true)) return;
    boost::asio::post(strand_, [self = shared_from_this()] {
        self->wake_pending_ = false;
        self->pump();
    });
}
void TimedTasks::stop() {
    { std::lock_guard lock{mutex_}; stopped_ = true; }
    boost::asio::post(strand_, [self = shared_from_this()] {
        std::lock_guard lock{self->mutex_};
        if (self->stopped_) self->timer_.cancel();
    });
}
void TimedTasks::pump() {
    std::lock_guard lock{mutex_};
    timer_.cancel();
    if (stopped_ || !writer()) return;
    try {
        const auto limit = server_.vars().snapshot()->timed_task_concurrency();
        // Completion wakes us when full: never spin an expired timer at capacity.
        if (active_.size() >= limit) return;
        auto trx = server_.resource().transaction();
        std::vector<std::pair<ResourceIf::RealKey, std::string>> due;
        std::optional<int64_t> next;
        const auto now = clock_();
        trx->iterate({"", Class::TASK_DUE}, [&](const auto& key, auto value) {
            const auto data = key.dataAsString();
            if (data.size() < 22) throw std::runtime_error{"Invalid timed-task index"};
            const auto when = std::stoll(data.substr(0, 20));
            if (when > now) { next = when; return false; }
            const std::string id{value.data(), value.size()};
            if (!active_.contains(id)) due.emplace_back(key, id);
            return due.size() < limit - active_.size();
        }, category);
        std::vector<std::pair<std::string, std::string>> dispatch;
        for (const auto& [key, id] : due) {
            trx->remove(key, false, category); // Acquire the index write lock before rereading state.
            auto task = get(*trx, id);
            if (task.empty()) continue;
            const auto state = text(task, "state");
            if (state != "pending" && state != "running") continue;
            const auto eligible = std::max(number(task, "next"), number(task, "due"));
            if (eligible > now) {
                trx->write(dueKey(eligible, id), id, false, category);
                continue;
            }
            if (number(task, "attempts") > number(task, "max_retries")) {
                task["state"] = "failed";
                task["payload"] = Object{};
                task["failure"] = "Retry budget exhausted after interrupted execution";
                save(*trx, task);
                continue;
            }
            const auto claim = uuid();
            task["attempts"] = number(task, "attempts") + 1;
            task["claim"] = claim;
            task["state"] = "running";
            task["next"] = now + 300; // Recover an interrupted claim after its lease.
            trx->write(dueKey(now + 300, id), id, false, category);
            save(*trx, task);
            dispatch.emplace_back(id, claim);
            // Dispatch only after the claim transaction commits below.
        }
        trx->commit();
        for (const auto& [id, claim] : dispatch) {
            active_.insert(id);
            boost::asio::post(server_.ctx(), [self = shared_from_this(), id, claim] { self->run(id, claim); });
        }
        if (!due.empty()) {
            boost::asio::post(strand_, [self = shared_from_this()] { self->pump(); });
        } else if (next) {
            // Arm only the next stored deadline. Recheck UTC when the timer fires.
            timer_.expires_after(std::chrono::seconds{std::max(int64_t{1}, *next - now)});
            timer_.async_wait([self = shared_from_this()](auto ec) { if (!ec) self->pump(); });
        }
    } catch (const std::exception& ex) {
        LOG_ERROR << "Timed task dispatch failed: " << ex.what();
        // Database recovery is independent of any individual task's retry budget.
        timer_.expires_after(std::chrono::seconds{30});
        timer_.async_wait([self = shared_from_this()](auto ec) { if (!ec) self->pump(); });
    }
}
void TimedTasks::run(std::string id, std::string claim) {
    try {
        Handler handler;
        {
            std::lock_guard lock{mutex_};
            if (stopped_ || !writer()) throw PermanentFailure{"Writer stopped"};
            auto read = server_.resource().transaction();
            const auto task = get(*read, id);
            const auto it = handlers_.find(text(task, "type"));
            if (it == handlers_.end()) throw PermanentFailure{"Unsupported timed task type/version"};
            handler = it->second;
        }
        auto trx = server_.resource().transaction();
        auto task = get(*trx, id);
        if (text(task, "claim") != claim || text(task, "state") != "running") throw PermanentFailure{"Stale claim"};
        if (number(task, "version") != 1) throw PermanentFailure{"Unsupported task version"};
        if (clock_() < number(task, "due")) throw std::runtime_error{"Task safety deadline has not elapsed"};
        auto execution = handler(*trx, task.at("payload").as_object());
        const auto current = get(*trx, id);
        if (text(current, "claim") != claim || text(current, "state") != "running") throw PermanentFailure{"Task cancelled or claim superseded"};
        if (!writer()) throw PermanentFailure{"Write authority changed"};
        task["state"] = "completed";
        task["payload"] = Object{};
        task["failure"] = "";
        trx->remove(dueKey(number(task, "next"), id), false, category);
        save(*trx, task);
        trx->commit();
        execution.guard.reset();
        if (execution.committed) execution.committed();
        LOG_DEBUG << "Completed timed task " << id;
    } catch (const PermanentFailure& ex) { fail(id, claim, true, ex.what()); }
      catch (const std::exception& ex) { fail(id, claim, false, ex.what()); }
      catch (...) { fail(id, claim, true, "Unsupported handler exception"); }
    { std::lock_guard lock{mutex_}; active_.erase(id); }
    wake();
}
void TimedTasks::fail(std::string_view id, std::string_view claim, bool permanent, std::string_view reason) {
    std::lock_guard lock{mutex_};
    if (stopped_ || !writer()) return; // Retain the claim for recovery on restart.
    try {
        auto trx = server_.resource().transaction();
        auto task = get(*trx, id);
        if (task.empty() || text(task, "claim") != claim || text(task, "state") != "running") return;
        trx->remove(dueKey(number(task, "next"), id), false, category);
        const bool exhausted = number(task, "attempts") > number(task, "max_retries");
        task["failure"] = reason.substr(0, 1024);
        task["state"] = permanent || exhausted ? "failed" : "pending";
        if (permanent || exhausted) task["payload"] = Object{};
        if (!permanent && !exhausted) {
            const auto exponent = std::min<int64_t>(number(task, "attempts") - 1, 7);
            const auto delay = std::min<int64_t>(3600, 30 * (int64_t{1} << exponent));
            const auto jitter = std::hash<std::string>{}(text(task, "claim")) % (delay / 4 + 1);
            const auto next = std::max(number(task, "due"), clock_() + std::min<int64_t>(3600, delay + static_cast<int64_t>(jitter)));
            task["next"] = next;
            trx->write(dueKey(next, id), id, false, category);
        }
        save(*trx, task);
        trx->commit();
        LOG_WARN << "Timed task " << id << " state=" << text(task, "state") << " reason=" << reason;
    } catch (const std::exception& ex) { LOG_ERROR << "Cannot persist task failure " << id << ": " << ex.what(); }
}
} // namespace nsblast::lib
