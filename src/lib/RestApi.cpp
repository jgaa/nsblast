
#include "RestApi.h"

#include "nsblast/logging.h"

using namespace std;
using namespace std::string_literals;
using namespace yahat;

namespace nsblast::lib {

RestApi::RestApi(const Config &config)
    : config_{config}
{

}

Response RestApi::onReqest(const Request &req, const Auth &auth)
{
    const auto p = parse(req);

    if (p.what == "rr") {
        return onResourceRecord(req, p);
    }

    if (p.what == "zone") {
        return onZone(req, p);
    }


    LOG_DEBUG << "Unknown subpath: " << p.what;
    return {404, "Unknown subpath"};
}

RestApi::Parsed RestApi::parse(const Request &req)
{
    // target syntax: /api/v1/zone|rr/{fqdn}[/verb]
    //                        ^
    //                        +------               = what
    //                        |        -----        = fqdn
    //                        |               ----  = operation
    //                        +-------------------- = base

    Parsed p;
    p.base = req.target;
    p.base = p.base.substr(req.route.size());
    while(!p.base.empty() && p.base.at(0) == '/') {
        p.base = p.base.substr(1);
    }

    if (auto pos = p.base.find('/'); pos != string_view::npos) {
        p.what = p.base.substr(0, pos);

        if (p.base.size() > pos) {
            p.fqdn = p.base.substr(pos + 1);

            if (auto end = p.fqdn.find('/') ; end != string_view::npos) {
                if (p.fqdn.size() > end) {
                    p.operation = p.fqdn.substr(end + 1);
                }
                p.fqdn = p.fqdn.substr(0, end);
            }
        }
    }

    return p;
}

Response RestApi::onZone(const Request &req, const RestApi::Parsed &parsed)
{
    // Check that the zone don't exist

    // check that the rrs include soa and 2 ns (and that one is primary in soa)

    // Build binary buffer

    // submit
}

Response RestApi::onResourceRecord(const Request &req, const RestApi::Parsed &parsed)
{

}


} // ns
