#pragma once

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <boost/json.hpp>

#include "nsblast/nsblast.h"
#include "nsblast/VarsViewIf.h"
#include "proto/nsblast.pb.h"

namespace nsblast {
class Server;
}

namespace nsblast::lib {

class Vars : public VarsViewIf {
public:
    struct Item {
        std::string name;
        boost::json::value value;
        boost::json::value default_value;
        bool non_mutable = false;
        bool requires_restart = false;
        std::string description;
    };

    struct Error : public std::runtime_error {
        Error(int code, const std::string& msg)
            : std::runtime_error(msg), code_{code} {}

        int code() const noexcept { return code_; }

    private:
        int code_;
    };

    explicit Vars(Server& server);

    void loadOrCreateForUpgrade();
    void bootstrap(std::string_view clusterRole,
                   const std::vector<std::string>& overrides);
    void ensureRequiredForRuntime() const;

    std::vector<Item> list() const;
    std::optional<Item> get(std::string_view name) const;

    void set(std::string_view name,
             const boost::json::value& value,
             bool force = false,
             bool allowForce = false);

    void setFromAssignment(std::string_view assignment,
                           bool force = false,
                           bool allowForce = false);

    void unset(std::string_view name,
               bool force = false,
               bool allowForce = false);

    std::shared_ptr<const pb::VarsSnapshot> snapshot() const override;

private:
    enum class ValueType {
        BOOL,
        UINT32,
        STRING
    };

    struct Def {
        std::string_view name;
        ValueType type;
        bool non_mutable;
        bool requires_restart;
        std::string_view description;
    };

    static const std::vector<Def>& defs();

    static pb::VarsSnapshot defaults(const Config& config);
    static void validate(const pb::VarsSnapshot& snap);
    static void validateRequired(const pb::VarsSnapshot& snap);

    static boost::json::value getValue(const pb::VarsSnapshot& snap, std::string_view name);
    static boost::json::value getDefaultValue(const pb::VarsSnapshot& snap, std::string_view name);

    static bool hasExplicitValue(const pb::VarsSnapshot& snap, std::string_view name);
    static bool isNonMutableSet(const pb::VarsSnapshot& snap, std::string_view name);

    static std::pair<std::string, std::string> splitAssignment(std::string_view assignment);
    static boost::json::value parseStringValue(std::string_view raw, ValueType type);

    static void apply(pb::VarsSnapshot& snap,
                      std::string_view name,
                      const boost::json::value& value,
                      bool isUnset,
                      const pb::VarsSnapshot& defaultValues,
                      bool force,
                      bool allowForce);

    std::optional<pb::VarsSnapshot> readSnapshot() const;
    void persistSnapshot(const pb::VarsSnapshot& snap);
    void setSnapshot(pb::VarsSnapshot snap);

    Server& server_;
    pb::VarsSnapshot defaults_;
    std::shared_ptr<const pb::VarsSnapshot> snapshot_;
};

} // namespace nsblast::lib
