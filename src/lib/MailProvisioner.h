#pragma once
#include "RestApi.h"
#include "TimedTasks.h"
#include "AuthMgr.h"

namespace nsblast::lib {
class MailProvisioner {
public:
    explicit MailProvisioner(Server& server);
    yahat::Response request(const yahat::Request& req, std::string_view domain, std::string_view operation = {});
    yahat::Response policy(const yahat::Request& req);
    TimedTasks::Execution execute(ResourceIf::TransactionIf& trx, const boost::json::object& payload);
private:
    struct Plan;
    Plan plan(ResourceIf::TransactionIf& trx, const Session& session, const std::string& domain,
              yahat::Request::Type method, const boost::json::object& input, bool preserve_ip);
    std::shared_ptr<void> lockZones(ResourceIf::TransactionIf& trx, const std::string& domain,
                                   const boost::json::object& input);
    Server& server_;
};
} // namespace nsblast::lib
