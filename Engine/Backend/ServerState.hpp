#pragma once

#include "Engine/Core/Json.hpp"

#include <functional>
#include <string>

namespace ld {

class ServerState {
public:
    ServerState();
    Json state;
    Json title = Json::object();
    Json blobs = Json::object();
    std::function<double()> clock;
    std::function<std::string()> random;
    std::string region = "local";
    void enroll(const std::string& player, const std::string& name, const Json& profile);
    Json invoke(const std::string& player, const std::string& name, const Json& args);
    Json client(const std::string& player, const std::string& name, const Json& args);

private:
    Json& account(const std::string& id);
    Json read(const std::string& id, const std::string& key, Json fallback = nullptr);
    void save(const std::string& id, const std::string& key, const Json& value);
    Json profile(const std::string& id);
    Json catalog();
    Json inventory(const std::string& id);
    Json published(const std::string& id);
    Json stored(const std::string& owner, const std::string& id, const std::string& prefix);
    Json migrate(Json world);
    Json attach(const std::string& owner, Json world, const std::string& prefix);
    Json experience(const std::string& player, const Json& id);
    Json persist(const std::string& player, const Json& world, bool publish);
    Json observe(const Json& world);
    Json transact(const Json& world, const std::function<Json(Json&, double)>& mutate);
    Json sessions(const std::string& player, const std::string& name, const Json& args);
    Json describe(const Json& server);
    void leavePresence(const std::string& server, const std::string& player, const std::string& session);
    void prunePresence(const Json& server);
    std::string identifier();
};

}
