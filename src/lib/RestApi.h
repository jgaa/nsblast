#pragma once

#include "gtest/gtest_prod.h"
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
        std::string_view fqdn;
        std::string_view operation;
    };

    struct ZoneInfo {
        std::string fqdn;
        Zone zone;
    };

    RestApi(Db& db, const Config& config);

    Response onReqest(const Request &req) override;

    template <typename T>
    bool fromJson(const std::string& json, T& obj) {
        const auto res = google::protobuf::util::JsonStringToMessage(json, &obj);
        if (!res.ok()) {
//            std::clog << "Failed to convert json to "
//                     << typeid(T).name() << ": "
//                     << res.ToString() << std::endl;
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
     *  @param fqdn Zone to search for
     *  @param recurseDown If true, reduce the fqdn from left and search
     *      for a match until it's found or the fqdn is empty.
     */
    std::optional<ZoneInfo> lookupZone(std::string_view fqdn, bool recurseDown = true);

    /*! Removes the leftmost hostname from fqdn */
    std::string_view reduce(const std::string_view fqdn);
private:
    FRIEND_TEST(testZoneApi, zonePOST);
    FRIEND_TEST(testRrApi, updateResourceRecordAdd);
    FRIEND_TEST(testRrApi, updateResourceRecordMerge);
    FRIEND_TEST(testRrApi, updateResourceRecordReplace);
    FRIEND_TEST(testRrApi, deleteResourceRecord);

    Response onZone(const Request &req, const Parsed& parsed);
    Response updateZone(const Request &req, const Parsed& parsed,
                        std::optional<bool> isNew, bool merge);
    Response deleteZone(const Request &req, const Parsed& parsed);
    Response onResourceRecord(const Request &req, const Parsed& parsed);
    Response updateResourceRecord(const Request &req, const Parsed& parsed,
                                  const ZoneInfo& zi,
                                  std::optional<bool> isNew, bool merge);
    Response deleteResourceRecord(const Request &req, const Parsed& parsed,
                                  const ZoneInfo &zi);

    const Config& config_;
    Db& db_;
};

} // ns
