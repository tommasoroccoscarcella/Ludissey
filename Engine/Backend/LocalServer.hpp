#pragma once

#include "Engine/Backend/Backend.hpp"
#include "Engine/Backend/ServerState.hpp"
#include "Engine/Core/Ids.hpp"
#include "Engine/Core/Json.hpp"

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace ld {

class LocalServer : public std::enable_shared_from_this<LocalServer> {
public:
    static std::shared_ptr<LocalServer> standard();
    explicit LocalServer(std::filesystem::path folder);
    ~LocalServer();
    LocalServer(const LocalServer&) = delete;
    LocalServer& operator=(const LocalServer&) = delete;

    const std::filesystem::path folder;
    std::filesystem::path blobs() const { return folder / "Risorse"; }
    Json invokeNow(const PlayerId& player, const std::string& name, const Json& parameters);
    Json clientNow(const PlayerId& player, const std::string& name, const Json& parameters);
    void enrollNow(const PlayerId& player, const PlayerProfile& profile);
    Async<Json> invoke(PlayerId player, std::string name, Json parameters);
    Async<Json> client(PlayerId player, std::string name, Json parameters);
    Async<void> enroll(PlayerId player, PlayerProfile profile);
    std::optional<std::filesystem::path> blob(const std::string& key) const;
    std::shared_ptr<HttpTransport> storage();

private:
    std::unique_ptr<ServerState> engine;
    std::recursive_mutex lock;
    std::optional<std::string> loaded;
    Json call(bool client, const PlayerId& player, const std::string& name, const Json& parameters);
    template <typename F>
    auto exclusive(F body) -> decltype(body());
};

}
