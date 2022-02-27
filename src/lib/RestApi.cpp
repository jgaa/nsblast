#include "RestApi.h"
#include "Db.h"

#include "nsblast/logging.h"
#include "google/protobuf/util/json_util.h"

using namespace std;
using namespace std::string_literals;

namespace nsblast::lib {

//Parsed parse(const Request &req) {
//    auto target = string_view{req.target};
//    target = target.substr(req.route.size());
//}


RestApi::RestApi(Db &db, const Config &config)
    : config_{config}, db_{db}
{

}

Response RestApi::onReqest(const Request &req)
{
    const auto p = parse(req);

    if (p.what == "zone") {
        return onZone(req, p);
    }

    LOG_INFO << "Unknown subpath: " << p.what;
    return {404, "Unknown subpath"};
}

RestApi::Parsed RestApi::parse(const Request &req)
{
    // target syntax: /api/v1/zone/{fqdn}[/verb]
    //                        ^
    //                        +---                  = what
    //                        |    -----            = fdqn
    //                        |           ----      = operation
    //                        +-----------------    = base

    Parsed p;
    p.base = req.target;
    p.base = p.base.substr(req.route.size());
    while(!p.base.empty() && p.base.at(0) == '/') {
        p.base = p.base.substr(1);
    }

    if (auto pos = p.base.find('/'); pos != string_view::npos) {
        p.what = p.base.substr(0, pos);

        if (p.base.size() > pos) {
            p.fdqn = p.base.substr(pos + 1);

            if (auto end = p.fdqn.find('/') ; end != string_view::npos) {
                if (p.fdqn.size() > end) {
                    p.operation = p.fdqn.substr(end + 1);
                }
                p.fdqn = p.fdqn.substr(0, end);
            }
        }
    }

    return p;
}

Response RestApi::onZone(const Request &req, const Parsed &parsed)
{
    try {
    switch(req.type) {
        case Request::Type::POST:
            return updateZone(req, parsed, true, false);
        case Request::Type::PUT:
            return updateZone(req, parsed, false, false);
        case Request::Type::PATCH:
            return updateZone(req, parsed, {}, true);
        case Request::Type::DELETE:
            return deleteZone(req, parsed);

        default:
            return {405, "Method not allowed"};
        }
    } catch (const Db::AlreadyExistException& ex) {
        LOG_INFO << "Operation failed: Already exists " << ex.what();
        return {409, "The object already exists"};
    } catch (const Db::NotFoundException& ex) {
        LOG_INFO << "Object not found: " << ex.what();
        return {404, "Object not found: "s + ex.what()};
    } catch (const exception& ex) {
        LOG_INFO << "Operation failed: " << ex.what();
        return {400, "Operation failed: "s + ex.what()};
    }

    return {500, "Not here"};
}

Response RestApi::updateZone(const Request &req, const Parsed &parsed,
                             std::optional<bool> isNew, bool merge)
{
    Zone zone;

    if (!fromJson(req.body, zone)) {
        return {400, "Failed to parse json payload into Zone object"};
    }

    db_.writeZone(parsed.fdqn, zone, isNew, merge);

    return {};
}

Response RestApi::deleteZone(const Request &req, const Parsed& parsed)
{
    try {
        db_.deleteZone(parsed.fdqn);
    } catch (Db::NotFoundException&) {
        return {404, "Not found"};
    }

    return {};
}

} // ns
