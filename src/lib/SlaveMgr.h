#pragma once

#include <unordered_map>
#include <boost/asio.hpp>

#include "nsblast/nsblast.h"
#include "nsblast/ResourceIf.h"

#include "proto/nsblast.pb.h"


namespace nsblast::lib {

class Slave;

/*! Manager for slave zones
 *
 *  If a server is a slave (authorative for one or mores zones, but not the primary
 *  nameserver where the changes are applied), an instance of this class
 *  will take care of keeping the zone(s) in sync with the master(s).
 *
 *  Each zone is configured individually, so that we have great freedom in the
 *  deployment of nsblast. We can use a strict server/slave deployment, mixed
 *  deployments where an instance is a primary for some zones and a slave for
 *  other zones, and we can act as a slave server for any nameserver that
 *  implements the appropriate standards for server/slave synchronization.
 */

class SlaveMgr {
public:
    SlaveMgr(const Config& config, ResourceIf& resource, boost::asio::io_context& ctx);

    void getZone(std::string_view fqdn, pb::Zone& zone);
    void addZone(std::string_view fqdn, const pb::Zone& zone);
    void replaceZone(std::string_view fqdn, const pb::Zone& zone);
    void mergeZone(std::string_view fqdn, const pb::Zone& zone);
    void deleteZone(std::string_view fqdn);

    void init();
    void reload(std::string_view fqdn);
    void reload(std::string_view fqdn, pb::Zone& zone);

    auto& ctx() const noexcept {
        return ctx_;
    }

    auto& config() const noexcept {
        return config_;
    }

    auto& db() {
        return db_;
    }

private:

    // List of active slave zones
    std::unordered_map<std::string, std::shared_ptr<Slave>> zones_;
    std::mutex mutex_;

    const Config& config_;
    ResourceIf& db_;
    boost::asio::io_context& ctx_;
};

} // ns
