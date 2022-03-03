#pragma once

#include "HttpServer.h"
#include "nsblast/logging.h"
#include "google/protobuf/util/json_util.h"
#include "data.pb.h"

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

    struct ZoneInfo {
        std::string fdqn;
        Zone zone;
    };

    RestApi(Db& db, const Config& config);

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
    /*! Lookup a zone
     *
     *  @param fdqn Zone to search for
     *  @param recurseDown If true, reduce the fdqn from left and search
     *      for a match until it's found or the fdqn is empty.
     */
    std::optional<ZoneInfo> lookupZone(std::string_view fdqn, bool recurseDown = true);

    /*! Removes the leftmost hostname from fdqn */
    std::string_view reduce(const std::string_view fdqn);
private:
    Response onZone(const Request &req, const Parsed& parsed);
    Response updateZone(const Request &req, const Parsed& parsed,
                        std::optional<bool> isNew, bool merge);
    Response deleteZone(const Request &req, const Parsed& parsed);
    Response onResourceRecord(const Request &req, const Parsed& parsed);
    Response updateResourceRecord(const Request &req, const Parsed& parsed,
                                  const ZoneInfo& zi,
                                  std::optional<bool> isNew, bool merge);
    Response deleteResourceRecord(const Request &req, const Parsed& parsed);

    const Config& config_;
    Db& db_;
};

} // ns
