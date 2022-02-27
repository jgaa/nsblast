#pragma once

#include "HttpServer.h"
#include "nsblast/logging.h"
#include "google/protobuf/util/json_util.h"

namespace nsblast::lib {

class Db;

class RestApi : public RequestHandler
{
public:
    struct Parsed {
        std::string_view base;
        std::string_view what;
        std::string_view fdqn;
        std::string_view operation;
    };

    RestApi(Db& db, Config& config);

    Response onReqest(const Request &req) override;

    template <typename T>
    bool fromJson(const std::string& json, T& obj) {
        const auto res = google::protobuf::util::JsonStringToMessage(json, &obj);
        if (!res.ok()) {
            LOG_INFO << "Failed to convert json to "
                     << typeid(T).name() << ": "
                     << res.ToString();
            LOG_TRACE << "Failed json: " << json;
            return false;
        }
        return true;
    }

    Parsed parse(const Request &req);
private:
    Response onZone(const Request &req, const Parsed& parsed);
    Response updateZone(const Request &req, const Parsed& parsed,
                        std::optional<bool> isNew, bool merge);
    Response deleteZone(const Request &req, const Parsed& parsed);

    const Config& config_;
    Db& db_;
};

} // ns
