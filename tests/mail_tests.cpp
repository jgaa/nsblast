#include <atomic>
#include <thread>
#include <boost/asio/post.hpp>
#include <boost/asio/spawn.hpp>
#include "gtest/gtest.h"
#include "TmpDb.h"
#include "RestApi.h"
#include "MailProvisioner.h"
#include "TimedTasks.h"
#ifdef NSBLAST_CLUSTER
#include "FollowerReplication.h"
#endif

using namespace nsblast;
using namespace nsblast::lib;
namespace json = boost::json;
namespace {
using Method = yahat::Request::Type;
using Class = ResourceIf::RealKey::Class;
constexpr auto account = ResourceIf::Category::ACCOUNT;

class MailServer : public MockServer {
public:
    int64_t now = 1700000000;
    explicit MailServer(bool auth = false) : MockServer{std::make_shared<TmpDb>(auth)} {
        startApi();
        timed_tasks_ = std::make_shared<TimedTasks>(*this, [this] { return now; });
        timed_tasks_->registerHandler("mail-retirement/v1", [this](auto& trx, const auto& payload) { return mail().execute(trx, payload); });
        // Exercise the actual zone API and RR index, not synthetic unowned entries.
        auto response = call("zone/example.com", Method::POST, R"({"soa":{"mname":"ns1.example.com","rname":"hostmaster.example.com","serial":1000,"refresh":3600,"retry":600,"expire":86400,"minimum":300},"ns":["ns1.example.com","ns2.example.com"]})");
        if (response.code != 201) throw std::runtime_error{"Test zone creation failed"};
    }
    yahat::Response call(std::string path, Method method, std::string body = {}) {
        yahat::Request req{"/api/v1/" + path, std::move(body), method, nullptr};
        req.route = "/api/v1";
        req.auth = getAdminSession()->getAuth();
        return api().onReqest(req);
    }
    yahat::Response mailCall(Method method, const json::object& input = {}, std::string suffix = {}) {
        return call("provision/mail/example.com" + suffix, method, method == Method::GET || method == Method::DELETE ? std::string{} : json::serialize(input));
    }
    json::object config(std::string host = "mail.example.com", bool addresses = true) {
        json::object server{{"hostname", host}};
        if (addresses) server["ipv4"] = json::array{"192.0.2.10"};
        return {{"mail_servers", json::array{server}}};
    }
    uint32_t serial() { auto trx = resource().transaction(); return trx->lookup("example.com").getSoa().serial(); }
    std::string data(std::string_view owner) { auto trx = resource().transaction(); auto entry = trx->lookup(owner); auto raw = entry.buffer(); return {raw.data(), raw.size()}; }
    bool has(std::string_view owner, uint16_t type, std::string_view txt = {}) {
        auto trx = resource().transaction(); auto entry = trx->lookup(owner);
        for (const auto& rr : entry) if (rr.type() == type && (txt.empty() || (type == TYPE_TXT && RrTxt{rr.span(), rr.offset()}.string() == txt))) return true;
        return false;
    }
    json::object status() { return json::parse(mailCall(Method::GET, {}, "/status").body).at("value").as_object(); }
    void drain() { ctx().restart(); while (ctx().poll()) {} }
    void runTasks() { timedTasks().start(); drain(); }
    void advance(int64_t seconds) { now += seconds; timedTasks().wake(); drain(); }
};

TEST(MailProvisioning, DedicatedAtomicCreateAndIdempotentPut) {
    MailServer s;
    auto config = s.config();
    const auto before = s.serial();
    ASSERT_EQ(s.mailCall(Method::POST, config).code, 201);
    EXPECT_EQ(s.serial(), before + 1);
    EXPECT_TRUE(s.has("mail.example.com", TYPE_A));
    EXPECT_TRUE(s.has("example.com", TYPE_MX));
    EXPECT_TRUE(s.has("example.com", TYPE_TXT, "v=spf1 mx -all"));
    EXPECT_TRUE(s.has("_dmarc.example.com", TYPE_TXT, "v=DMARC1; p=none"));
    auto prior = s.data("example.com");
    const auto serial = s.serial();
    ASSERT_EQ(s.mailCall(Method::PUT, config).code, 200);
    EXPECT_EQ(s.serial(), serial);
    EXPECT_EQ(s.data("example.com"), prior);
    EXPECT_EQ(s.mailCall(Method::POST, config).code, 409);
}
TEST(MailProvisioning, InvalidPutPreservesCompleteConfiguration) {
    MailServer s;
    auto config = s.config();
    ASSERT_EQ(s.mailCall(Method::POST, config).code, 201);
    auto before = s.data("example.com"), host = s.data("mail.example.com");
    config["dkim"] = json::array{json::object{{"selector", "s1"}, {"public_key", "not-a-key"}}};
    EXPECT_EQ(s.mailCall(Method::PUT, config).code, 400);
    EXPECT_EQ(s.data("example.com"), before);
    EXPECT_EQ(s.data("mail.example.com"), host);
    EXPECT_FALSE(s.has("s1._domainkey.example.com", TYPE_TXT));
}
TEST(MailProvisioning, InvalidLaterHostDoesNotPartiallyCreateEarlierHost) {
    MailServer s;
    auto config = s.config();
    config["mail_servers"].as_array().push_back(json::object{{"hostname", "mx2.example.com"}, {"ipv6", json::array{"not-an-ip"}}});
    const auto serial = s.serial();
    EXPECT_EQ(s.mailCall(Method::POST, config).code, 400);
    EXPECT_EQ(s.serial(), serial);
    EXPECT_FALSE(s.has("mail.example.com", TYPE_A));
    EXPECT_FALSE(s.has("example.com", TYPE_MX));
}
TEST(MailProvisioning, DeletePreservesZoneAndUnrelatedTxtAndAddresses) {
    MailServer s;
    ASSERT_EQ(s.call("rr/example.com", Method::PATCH, R"({"txt":["site-verification=abc"],"a":["192.0.2.80"]})").code, 200);
    ASSERT_EQ(s.mailCall(Method::POST, s.config()).code, 201);
    const auto before = s.serial();
    auto response = s.mailCall(Method::DELETE);
    ASSERT_EQ(response.code, 200);
    EXPECT_EQ(s.serial(), before + 1);
    EXPECT_TRUE(s.has("example.com", TYPE_SOA));
    EXPECT_TRUE(s.has("example.com", TYPE_NS));
    EXPECT_TRUE(s.has("example.com", TYPE_A));
    EXPECT_TRUE(s.has("example.com", TYPE_TXT, "site-verification=abc"));
    EXPECT_FALSE(s.has("example.com", TYPE_MX));
    EXPECT_FALSE(s.has("mail.example.com", TYPE_A));
    const auto serial = s.serial();
    EXPECT_EQ(s.mailCall(Method::DELETE).code, 200);
    EXPECT_EQ(s.serial(), serial);
}
TEST(MailProvisioning, PreserveIpAndManualModificationInvalidateExclusiveOwnership) {
    MailServer s;
    ASSERT_EQ(s.mailCall(Method::POST, s.config()).code, 201);
    ASSERT_EQ(s.mailCall(Method::DELETE, {}, "?preserve_ip=true").code, 200);
    EXPECT_TRUE(s.has("mail.example.com", TYPE_A));
    ASSERT_EQ(s.mailCall(Method::PUT, s.config("mail.example.com", false)).code, 200);
    ASSERT_EQ(s.call("rr/mail.example.com", Method::PATCH, R"({"txt":["also-a-website"]})").code, 200);
    auto response = s.mailCall(Method::DELETE);
    EXPECT_EQ(response.code, 200);
    EXPECT_TRUE(s.has("mail.example.com", TYPE_A));
    EXPECT_FALSE(json::parse(response.body).at("value").at("warnings").as_array().empty());
}
TEST(MailProvisioning, SharedHostIsPreservedWhenAnotherMxReferencesIt) {
    MailServer s;
    ASSERT_EQ(s.mailCall(Method::POST, s.config()).code, 201);
    ASSERT_EQ(s.call("rr/customer.example.com", Method::POST, R"({"mx":[{"host":"mail.example.com","priority":10}]})").code, 201);
    ASSERT_EQ(s.mailCall(Method::DELETE).code, 200);
    EXPECT_TRUE(s.has("mail.example.com", TYPE_A));
    EXPECT_TRUE(s.has("customer.example.com", TYPE_MX));
}
TEST(MailProvisioning, ManagedAddressUpdatesAndAmbiguousAddressProtection) {
    MailServer s;
    auto config = s.config();
    ASSERT_EQ(s.mailCall(Method::POST, config).code, 201);
    config["mail_servers"].as_array()[0].as_object()["ipv4"] = json::array{"192.0.2.11"};
    EXPECT_EQ(s.mailCall(Method::PUT, config).code, 200);
    ASSERT_EQ(s.call("rr/mail.example.com", Method::PATCH, R"({"txt":["shared-service"]})").code, 200);
    config["mail_servers"].as_array()[0].as_object()["ipv4"] = json::array{"192.0.2.12"};
    const auto before = s.data("example.com");
    EXPECT_EQ(s.mailCall(Method::PUT, config).code, 409);
    EXPECT_EQ(s.data("example.com"), before);
}
TEST(MailProvisioning, RemovesDottedSelectorsWithoutVersionAndPreservesUnrelatedTxt) {
    MailServer s;
    ASSERT_EQ(s.call("rr/old.key._domainkey.example.com", Method::POST, R"({"txt":["k=rsa; p=QUJDRA==", "verification=keep"]})").code, 201);
    EXPECT_EQ(s.mailCall(Method::PUT, s.config()).code, 200);
    EXPECT_TRUE(s.has("old.key._domainkey.example.com", TYPE_TXT, "verification=keep"));
    EXPECT_FALSE(s.has("old.key._domainkey.example.com", TYPE_TXT, "k=rsa; p=QUJDRA=="));
}
TEST(MailProvisioning, RejectsCnameAndMissingLocalMxAddressesBeforeWriting) {
    MailServer s;
    ASSERT_EQ(s.call("rr/alias.example.com", Method::POST, R"({"cname":"outside.example.net"})").code, 201);
    EXPECT_EQ(s.mailCall(Method::POST, s.config("alias.example.com", false)).code, 409);
    EXPECT_EQ(s.mailCall(Method::POST, s.config("missing.example.com", false)).code, 409);
    EXPECT_FALSE(s.has("example.com", TYPE_MX));
}
TEST(MailProvisioning, ValidatesNamesLimitsSpfAndReporting) {
    MailServer s;
    auto input = s.config();
    input["spf"] = json::object{{"policy", "v=spf1 ip4:bad -all"}};
    EXPECT_EQ(s.mailCall(Method::POST, input).code, 400);
    input = s.config(); input["dmarc"] = json::object{{"rua", "mailto:x@example.com; p=reject"}};
    EXPECT_EQ(s.mailCall(Method::POST, input).code, 400);
    input = s.config(); input["ttl"] = 0;
    EXPECT_EQ(s.mailCall(Method::POST, input).code, 400);
    EXPECT_EQ(s.call("provision/mail/example.com..", Method::DELETE).code, 400);
    EXPECT_EQ(s.call("provision/mail/example.com", Method::POST, std::string(131073, 'x')).code, 400);
}
TEST(MailProvisioning, PolicyPublicHostValidationAndAnonymousRead) {
    MailServer s;
    auto input = s.config(); input["mta_sts"] = json::object{{"enabled", true}, {"mode", "enforce"}, {"max_age", 600}};
    ASSERT_EQ(s.mailCall(Method::POST, input).code, 201);
    yahat::Request req{"/.well-known/mta-sts.txt", {}, Method::GET, nullptr};
    req.headers.set("Host", "MTA-STS.Example.Com:443");
    auto policy = s.api().onReqest(req);
    EXPECT_EQ(policy.code, 200);
    EXPECT_EQ(policy.mime_type, "text/plain");
    EXPECT_NE(policy.body.find("mode: enforce\n"), std::string::npos);
    req.headers.set("Host", "unknown.example.com");
    req.headers.set("X-Forwarded-Host", "mta-sts.example.com");
    EXPECT_EQ(s.api().onReqest(req).code, 404);
    req.headers.set("Host", "mta-sts.example.com.evil.test");
    EXPECT_EQ(s.api().onReqest(req).code, 404);
}
TEST(MailProvisioning, RetirementPreservesMxAndExecutesOneAtomicReplacement) {
    MailServer s;
    auto original = s.config(); original["mta_sts"] = json::object{{"enabled", true}, {"mode", "enforce"}, {"max_age", 600}};
    ASSERT_EQ(s.mailCall(Method::POST, original).code, 201);
    auto replacement = s.config("mx.new-provider.test", false);
    auto response = s.mailCall(Method::PUT, replacement);
    ASSERT_EQ(response.code, 200);
    EXPECT_EQ(json::parse(response.body).at("value").at("status"), "pending");
    EXPECT_TRUE(s.has("mail.example.com", TYPE_A));
    const auto before = s.data("example.com");
    auto status = s.status(); const auto id = status.at("transition"); const auto due = status.at("earliest_completion").as_int64();
    EXPECT_EQ(due, s.now + 600);
    s.runTasks();
    EXPECT_EQ(s.data("example.com"), before);
    ASSERT_EQ(s.mailCall(Method::PUT, replacement).code, 200);
    EXPECT_EQ(s.status().at("transition"), id);
    EXPECT_EQ(s.status().at("earliest_completion"), due);
    s.advance(600);
    EXPECT_EQ(s.status().at("status"), "complete");
    EXPECT_FALSE(s.has("mail.example.com", TYPE_A));
    EXPECT_FALSE(s.has("_mta-sts.example.com", TYPE_TXT));
    auto trx = s.resource().transaction(); auto task = s.timedTasks().get(*trx, std::string{id.as_string()});
    EXPECT_EQ(task.at("state"), "completed");
}
TEST(MailProvisioning, DeferredDeleteDetectsManualMxAndDoesNotRemoveReplacement) {
    MailServer s;
    auto input = s.config(); input["mta_sts"] = json::object{{"enabled", true}, {"mode", "enforce"}, {"max_age", 300}};
    ASSERT_EQ(s.mailCall(Method::POST, input).code, 201);
    ASSERT_EQ(s.mailCall(Method::DELETE).code, 200);
    ASSERT_EQ(s.call("rr/example.com", Method::PATCH, R"({"mx":[{"host":"new.example.net","priority":1}]})").code, 200);
    auto replacement = s.data("example.com");
    s.runTasks(); s.advance(300);
    EXPECT_EQ(s.status().at("status"), "failed");
    EXPECT_EQ(s.data("example.com"), replacement);
    EXPECT_TRUE(s.has("mail.example.com", TYPE_A));
}
TEST(MailProvisioning, SupersededRetirementDoesNotDeleteReenabledPolicy) {
    MailServer s;
    auto input = s.config(); input["mta_sts"] = json::object{{"enabled", true}, {"mode", "enforce"}, {"max_age", 300}};
    ASSERT_EQ(s.mailCall(Method::POST, input).code, 201);
    ASSERT_EQ(s.mailCall(Method::DELETE).code, 200);
    EXPECT_EQ(s.mailCall(Method::PUT, input).code, 200);
    auto before = s.data("example.com");
    s.runTasks(); s.advance(300);
    EXPECT_EQ(s.data("example.com"), before);
    EXPECT_TRUE(s.has("_mta-sts.example.com", TYPE_TXT));
    EXPECT_EQ(s.status().at("status"), "complete");
}
TEST(MailProvisioning, OlderLongerPolicyStillControlsRetirementDeadline) {
    MailServer s;
    auto input = s.config(); input["mta_sts"] = json::object{{"enabled", true}, {"mode", "enforce"}, {"max_age", 10000}};
    ASSERT_EQ(s.mailCall(Method::POST, input).code, 201);
    input["mta_sts"].as_object()["max_age"] = 60;
    ASSERT_EQ(s.mailCall(Method::PUT, input).code, 200);
    s.now += 10;
    ASSERT_EQ(s.mailCall(Method::DELETE).code, 200);
    EXPECT_EQ(s.status().at("earliest_completion"), s.now - 10 + 10000);
}

TEST(TimedExecution, RetriesSurviveRecreationAndStopAtPersistedLimit) {
    MailServer s;
    s.setVar("timed_task_max_retries", 2);
    auto queue = std::make_shared<TimedTasks>(s, [&] { return s.now; });
    auto register_failure = [&](const auto& q) {
        q->registerHandler("transient/v1", [](auto& trx, const auto&) -> TimedTasks::Execution {
            trx.write({"must-rollback", Class::META}, "value", false, account);
            throw std::runtime_error{"temporary failure"};
        });
    };
    register_failure(queue);
    auto trx = s.resource().transaction();
    const auto id = queue->schedule(*trx, "transient/v1", s.now, {}); trx->commit();
    queue->start(); s.drain();
    trx = s.resource().transaction(); auto task = queue->get(*trx, id);
    EXPECT_EQ(task.at("attempts"), 1);
    EXPECT_FALSE(trx->keyExists({"must-rollback", Class::META}, account));
    auto next = task.at("next").as_int64(); EXPECT_GE(next, s.now + 30); EXPECT_LE(next, s.now + 38);
    queue->stop(); s.drain(); queue.reset();
    queue = std::make_shared<TimedTasks>(s, [&] { return s.now; }); register_failure(queue);
    s.now = next; queue->start(); s.drain();
    trx = s.resource().transaction(); task = queue->get(*trx, id); EXPECT_EQ(task.at("attempts"), 2);
    s.now = task.at("next").as_int64(); queue->wake(); s.drain();
    trx = s.resource().transaction(); task = queue->get(*trx, id);
    EXPECT_EQ(task.at("state"), "failed"); EXPECT_EQ(task.at("attempts"), 3);
    s.now += 100000; queue->wake(); s.drain();
    trx = s.resource().transaction(); EXPECT_EQ(queue->get(*trx, id).at("attempts"), 3);
    queue->stop(); s.drain();
}
TEST(TimedExecution, PermanentFailureNeverRetriesAndSuccessCommitsEffects) {
    MailServer s;
    auto queue = std::make_shared<TimedTasks>(s, [&] { return s.now; });
    queue->registerHandler("permanent/v1", [](auto&, const auto&) -> TimedTasks::Execution { throw TimedTasks::PermanentFailure{"revoked"}; });
    queue->registerHandler("success/v1", [](auto& trx, const auto&) -> TimedTasks::Execution { trx.write({"effect", Class::META}, "committed", false, account); return {}; });
    auto trx = s.resource().transaction(); auto failed = queue->schedule(*trx, "permanent/v1", s.now, {}); auto ok = queue->schedule(*trx, "success/v1", s.now, {}); trx->commit();
    queue->start(); s.drain(); trx = s.resource().transaction();
    EXPECT_EQ(queue->get(*trx, failed).at("state"), "failed");
    EXPECT_EQ(queue->get(*trx, failed).at("attempts"), 1);
    EXPECT_EQ(queue->get(*trx, ok).at("state"), "completed");
    EXPECT_TRUE(trx->keyExists({"effect", Class::META}, account));
    queue->stop(); s.drain();
}
TEST(TimedExecution, QueuedAndRunningClaimsRespectDefaultAndConfiguredCapacity) {
    MailServer s;
    auto queue = std::make_shared<TimedTasks>(s, [&] { return s.now; });
    queue->registerHandler("noop/v1", [](auto&, const auto&) -> TimedTasks::Execution { return {}; });
    auto trx = s.resource().transaction(); std::vector<std::string> ids;
    for (size_t i = 0; i < 20; ++i) ids.push_back(queue->schedule(*trx, "noop/v1", s.now, {})); trx->commit();
    queue->start(); s.ctx().restart(); ASSERT_EQ(s.ctx().poll_one(), 1); // Pump claims but handlers are still queued.
    auto count_running = [&] { size_t result = 0; auto read = s.resource().transaction(); for (const auto& id : ids) if (queue->get(*read, id).at("state") == "running") ++result; return result; };
    EXPECT_EQ(count_running(), 8);
    s.setVar("timed_task_concurrency", 2);
    queue->wake(); s.drain();
    trx = s.resource().transaction(); for (const auto& id : ids) EXPECT_EQ(queue->get(*trx, id).at("state"), "completed");
    queue->stop(); s.drain();
}
TEST(TimedExecution, EarlierInsertionRearmsTimerAndNoTaskRunsBeforeDeadline) {
    MailServer s;
    auto queue = std::make_shared<TimedTasks>(s, [&] { return s.now; });
    queue->registerHandler("noop/v1", [](auto&, const auto&) -> TimedTasks::Execution { return {}; });
    auto trx = s.resource().transaction(); auto later = queue->schedule(*trx, "noop/v1", s.now + 100, {}); trx->commit();
    queue->start(); s.drain();
    trx = s.resource().transaction(); auto earlier = queue->schedule(*trx, "noop/v1", s.now + 10, {}); trx->commit(); queue->wake(); s.drain();
    s.now += 10; queue->wake(); s.drain();
    trx = s.resource().transaction(); EXPECT_EQ(queue->get(*trx, earlier).at("state"), "completed"); EXPECT_EQ(queue->get(*trx, later).at("attempts"), 0);
    queue->stop(); s.drain();
}

yahat::Auth bearer(MailServer& server, const std::string& token) {
    boost::asio::io_context ctx;
    yahat::Auth result;
    boost::asio::spawn(ctx, [&](boost::asio::yield_context yield) {
        yahat::Request req{"/api/v1/provision/mail/example.com", {}, Method::PUT, &yield};
        const auto header = "Bearer " + token;
        yahat::AuthReq ar{req, yield}; ar.auth_header = header;
        result = server.auth().authorize(ar);
    }, [](std::exception_ptr error) { if (error) std::rethrow_exception(error); });
    ctx.run(); return result;
}
std::string mailKey(MailServer& server, bool recursive = true) {
    const auto tenant_id = std::string{server.getAdminSession()->tenant()};
    auto tenant = server.auth().getTenant(tenant_id).value();
    auto role = tenant.add_roles(); role->set_name("mail-only");
    for (auto permission : {pb::USE_API, pb::CREATE_MAIL, pb::UPDATE_MAIL, pb::DELETE_MAIL}) role->add_permissions(permission);
    role->mutable_filter()->set_fqdn("example.com"); role->mutable_filter()->set_recursive(recursive);
    server.auth().upsertTenant(tenant_id, tenant, false);
    pb::ApiKey key; key.set_name("mail-automation"); key.add_roles("mail-only");
    return server.auth().createApiKey(tenant_id, key);
}
TEST(MailProvisioning, MailOnlyBearerCanProvisionButCannotWriteArbitraryRecords) {
    MailServer s{true}; const auto token = mailKey(s);
    yahat::Request request{"/api/v1/provision/mail/example.com", json::serialize(s.config()), Method::POST, nullptr};
    request.route = "/api/v1"; request.auth = bearer(s, token); ASSERT_TRUE(request.auth.access);
    EXPECT_EQ(s.api().onReqest(request).code, 201);
    yahat::Request arbitrary{"/api/v1/rr/website.example.com", R"({"a":["192.0.2.99"]})", Method::PUT, nullptr};
    arbitrary.route = "/api/v1"; arbitrary.auth = bearer(s, token);
    EXPECT_EQ(s.api().onReqest(arbitrary).code, 403);
    request.type = Method::DELETE; request.body.clear(); request.auth = bearer(s, token);
    EXPECT_EQ(s.api().onReqest(request).code, 200);
    EXPECT_TRUE(s.has("example.com", TYPE_SOA));
}
TEST(MailProvisioning, FiltersCoverEachAffectedNameBeforeAnyChanges) {
    MailServer s{true}; const auto token = mailKey(s, false);
    const auto before = s.data("example.com");
    yahat::Request request{"/api/v1/provision/mail/example.com", json::serialize(s.config()), Method::POST, nullptr};
    request.route = "/api/v1"; request.auth = bearer(s, token);
    EXPECT_EQ(s.api().onReqest(request).code, 403);
    EXPECT_EQ(s.data("example.com"), before);
    EXPECT_FALSE(s.has("mail.example.com", TYPE_A));
}
TEST(MailProvisioning, RevokedKeyCannotApplyDeferredDeletion) {
    MailServer s{true}; const auto token = mailKey(s);
    auto input = s.config(); input["mta_sts"] = json::object{{"enabled", true}, {"mode", "enforce"}, {"max_age", 300}};
    yahat::Request request{"/api/v1/provision/mail/example.com", json::serialize(input), Method::POST, nullptr};
    request.route = "/api/v1"; request.auth = bearer(s, token);
    ASSERT_EQ(s.api().onReqest(request).code, 201);
    request.type = Method::DELETE; request.body.clear(); request.auth = bearer(s, token);
    ASSERT_EQ(s.api().onReqest(request).code, 200);
    ASSERT_TRUE(s.auth().deleteApiKey(s.getAdminSession()->tenant(), "mail-automation"));
    auto before = s.data("example.com"); s.runTasks(); s.advance(300);
    EXPECT_EQ(s.status().at("status"), "failed");
    EXPECT_EQ(s.data("example.com"), before);
    EXPECT_TRUE(s.has("mail.example.com", TYPE_A));
}
TEST(TimedExecution, RecoversInterruptedClaimAfterLeaseAndKeepsAttempts) {
    MailServer s;
    auto first = std::make_shared<TimedTasks>(s, [&] { return s.now; });
    first->registerHandler("noop/v1", [](auto&, const auto&) -> TimedTasks::Execution { return {}; });
    auto trx = s.resource().transaction(); auto id = first->schedule(*trx, "noop/v1", s.now, {}); trx->commit();
    first->start(); s.ctx().restart(); ASSERT_EQ(s.ctx().poll_one(), 1);
    first->stop(); s.drain(); // The queued handler leaves its persisted claim for recovery.
    trx = s.resource().transaction(); auto interrupted = first->get(*trx, id);
    ASSERT_EQ(interrupted.at("state"), "running"); ASSERT_EQ(interrupted.at("attempts"), 1);
    auto second = std::make_shared<TimedTasks>(s, [&] { return s.now; });
    second->registerHandler("noop/v1", [](auto&, const auto&) -> TimedTasks::Execution { return {}; });
    second->start(); s.drain(); trx = s.resource().transaction(); EXPECT_EQ(second->get(*trx, id).at("attempts"), 1);
    s.now += 300; second->wake(); s.drain(); trx = s.resource().transaction();
    EXPECT_EQ(second->get(*trx, id).at("state"), "completed"); EXPECT_EQ(second->get(*trx, id).at("attempts"), 2);
    second->stop(); s.drain();
}
TEST(MailProvisioning, MultipleHostsAndCrossZoneFailureRemainAtomic) {
    MailServer s;
    auto input = s.config();
    input["mail_servers"].as_array().push_back(json::object{{"hostname", "mx2.example.com"}, {"priority", 20}, {"ipv6", json::array{"2001:db8::20"}}});
    ASSERT_EQ(s.mailCall(Method::POST, input).code, 201);
    EXPECT_TRUE(s.has("mx2.example.com", TYPE_AAAA));
    const auto before = s.data("example.com");
    input["mail_servers"].as_array().push_back(json::object{{"hostname", "external.invalid"}, {"ipv4", json::array{"192.0.2.55"}}});
    EXPECT_EQ(s.mailCall(Method::PUT, input).code, 404);
    EXPECT_EQ(s.data("example.com"), before);
}

TEST(MailProvisioning, BatchProducesCompleteIxfrAndOneSoaIncrement) {
    MailServer s;
    auto input = s.config();
    const auto serial = s.serial();
    ASSERT_EQ(s.mailCall(Method::POST, input).code, 201);
    ASSERT_EQ(s.serial(), serial + 1);
    auto trx = s.resource().transaction(); std::string raw;
    ASSERT_TRUE(trx->read({"example.com", serial + 1, Class::DIFF}, raw, ResourceIf::Category::DIFF, false));
    Entry diff{raw}; std::set<std::string> names; size_t soas = 0;
    for (const auto& rr : diff) { names.insert(rr.labels().string()); if (rr.type() == TYPE_SOA) ++soas; }
    EXPECT_EQ(soas, 2);
    EXPECT_TRUE(names.contains("example.com")); EXPECT_TRUE(names.contains("mail.example.com")); EXPECT_TRUE(names.contains("_dmarc.example.com"));
    ASSERT_TRUE(trx->keyExists({"example.com", "mail.example.com", Class::ZRR}, account));
}
TEST(MailProvisioning, NonMailReferencesPreserveAddressOwnershipSafety) {
    MailServer s; ASSERT_EQ(s.mailCall(Method::POST, s.config()).code, 201);
    ASSERT_EQ(s.call("rr/web.example.com", Method::POST, R"({"cname":"mail.example.com"})").code, 201);
    EXPECT_EQ(s.mailCall(Method::DELETE).code, 200);
    EXPECT_TRUE(s.has("mail.example.com", TYPE_A)); EXPECT_TRUE(s.has("web.example.com", TYPE_CNAME));
}
TEST(MailProvisioning, CrossTenantKeyCannotAlterAnotherTenantsZone) {
    MailServer s{true}; pb::Tenant tenant; tenant.set_active(true);
    auto role = tenant.add_roles(); role->set_name("mail");
    for (auto permission : {pb::USE_API, pb::CREATE_MAIL, pb::UPDATE_MAIL, pb::DELETE_MAIL}) role->add_permissions(permission);
    auto id = s.auth().createTenant(tenant);
    pb::ApiKey key; key.set_name("foreign"); key.add_roles("mail"); auto token = s.auth().createApiKey(id, key);
    yahat::Request req{"/api/v1/provision/mail/example.com", json::serialize(s.config()), Method::PUT, nullptr};
    req.route = "/api/v1"; req.auth = bearer(s, token);
    const auto before = s.data("example.com");
    EXPECT_EQ(s.api().onReqest(req).code, 403); EXPECT_EQ(s.data("example.com"), before);
}
TEST(MailProvisioning, SchemaUpgradeRebuildsReferenceAndOwnerIndexesIdempotently) {
    MailServer s; ASSERT_EQ(s.mailCall(Method::POST, s.config()).code, 201);
    auto trx = s.resource().transaction();
    trx->remove({"mail.example.com", "example.com", Class::DNS_REF}, false, account);
    trx->write({"bogus", "mail.example.com", Class::ZRR}, "", false, account);
    std::string version(sizeof(uint32_t), '\0'); setValueAt(version, 0, uint32_t{3});
    trx->write({"data_schema_version", Class::META}, version, false, account); trx->commit();
    s.auth().migrateStorage();
    EXPECT_EQ(s.auth().dataSchemaVersion(), CURRENT_DATA_SCHEMA_VERSION);
    trx = s.resource().transaction(); std::string kind;
    ASSERT_TRUE(trx->read({"mail.example.com", "example.com", Class::DNS_REF}, kind, account, false)); EXPECT_EQ(kind, "mx");
    EXPECT_FALSE(trx->keyExists({"bogus", "mail.example.com", Class::ZRR}, account));
    EXPECT_TRUE(trx->keyExists({"example.com", "mail.example.com", Class::ZRR}, account));
    s.auth().migrateStorage(); EXPECT_EQ(s.auth().dataSchemaVersion(), CURRENT_DATA_SCHEMA_VERSION);
}
#ifdef NSBLAST_CLUSTER
TEST(MailProvisioning, ReplicationFailureRollsBackEveryPart) {
    MailServer s;
    FollowerReplication follower{s}; FollowerReplication::Agent agent{follower};
    pb::Transaction update; update.set_id(999999);
    ResourceIf::RealKey key{"replication-must-rollback", Class::META};
    auto part = update.add_parts(); part->set_key(key.data(), key.size()); part->set_value("value"); part->set_columnfamilyix(static_cast<int>(account));
    part = update.add_parts(); part->set_key(key.data(), key.size()); part->set_value("invalid"); part->set_columnfamilyix(999);
    EXPECT_THROW(agent.onTrx(update), std::out_of_range);
    auto trx = s.resource().transaction(); EXPECT_FALSE(trx->keyExists(key, account));
    EXPECT_FALSE(trx->keyExists({uint64_t{999999}, Class::TRXID}, ResourceIf::Category::TRXLOG));
}
#endif
} // namespace
