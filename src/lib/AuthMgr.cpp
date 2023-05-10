
#include "AuthMgr.h"
#include "nsblast/logging.h"

using namespace std;

namespace nsblast::lib {


optional<pb::Tenant> AuthMgr::getTenant(std::string_view tenantId) const
{
    auto trx = server_.resource().transaction();

    ResourceIf::RealKey key{tenantId, ResourceIf::RealKey::Class::TENANT};
    std::string value;
    if (!trx->read(key, value, ResourceIf::Category::ACCOUNT, false)) {
        return {};
    }

    optional<nsblast::pb::Tenant> tenant;
    tenant.emplace();

    if (tenant->ParseFromArray(value.data(), value.size())) {
        return tenant;
    }

    LOG_ERROR << "AuthMgr::get Failed to deserialize from " << key;
    throw runtime_error{"Failed to deserialize Tenant"};
}

} // ns
