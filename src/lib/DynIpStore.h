#pragma once

#include <optional>
#include <string>
#include <vector>

#include "nsblast/ResourceIf.h"
#include "proto/nsblast.pb.h"

namespace nsblast::lib {

class DynIpStore {
public:
    struct CreatedHost {
        pb::DynipHost host;
        std::string token;
    };

    explicit DynIpStore(ResourceIf& resource)
        : resource_{resource} {}

    static std::string normalizeLabel(std::string_view label);
    static bool isValidLabel(std::string_view label);
    static std::string makeRootFqdn(std::string_view root, std::string_view realm);
    static std::string makeHostFqdn(std::string_view host, std::string_view root, std::string_view realm);

    pb::DynipRoot createRoot(std::string_view tenantId,
                             std::string_view root,
                             std::string_view realm,
                             uint32_t hostLimit);
    std::vector<pb::DynipRoot> listRoots(std::string_view tenantId);
    bool deleteRoot(std::string_view tenantId, std::string_view root);

    CreatedHost createHost(std::string_view tenantId,
                           std::string_view root,
                           std::string_view host,
                           std::string_view realm,
                           uint32_t ttl,
                           uint32_t maxHostsPerRoot);
    CreatedHost rotateHostToken(std::string_view tenantId,
                                std::string_view root,
                                std::string_view host);
    std::vector<pb::DynipHost> listHosts(std::string_view tenantId, std::string_view root);
    bool deleteHost(std::string_view tenantId, std::string_view root, std::string_view host);
    void recordHostUpdate(std::string_view root,
                          std::string_view host,
                          std::string_view ip,
                          std::string_view updatedAt);

    std::optional<pb::DynipHost> lookupHost(std::string_view root, std::string_view host);
    std::optional<pb::DynipTokenIndex> lookupByToken(std::string_view token);

private:
    ResourceIf& resource_;
};

} // namespace nsblast::lib
