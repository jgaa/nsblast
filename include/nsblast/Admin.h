#pragma once

#include <string>
#include <string_view>
#include <optional>


namespace nsblast::lib {

class Action {
public:
    enum class What {
       CREATE_ZONE
    };

    //std::optional<Zone> zone;
    //std::optional<Entry> entry;
};

class Context {
public:
    std::string logName = "admin";
};

/*! Interface for access control and business data access */
class Admin {
public:
    virtual ~Admin() = default;

    virtual void create(const Tenant& tenant) = 0;
    virtual void remove(const Tenant& tenant) = 0;
    virtual void update(const Tenant& tenant) = 0;

    virtual std::optional<Context> validateApiKey(const std::string_view& key, const Action& action) = 0;

};

} // ns
