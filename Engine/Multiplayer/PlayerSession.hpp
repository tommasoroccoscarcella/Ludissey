#pragma once

#include "Engine/Backend/Assets.hpp"
#include "Engine/Backend/Backend.hpp"
#include "Engine/Experience/Experience.hpp"
#include "Engine/Gui/GuiLayout.hpp"
#include "Engine/Multiplayer/Replication.hpp"
#include "Engine/Networking/Join.hpp"
#include "Engine/Scripting/ScriptEngine.hpp"

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace ld {

struct ChatMessage {
    int sequence = 0;
    std::string name;
    std::string text;
    bool operator==(const ChatMessage&) const = default;
};

struct PlayerTool {
    std::string id;
    std::string name;
    bool equipped = false;
    bool operator==(const PlayerTool&) const = default;
};

class PlayerGameplay {
public:
    static constexpr double maximumHealth = 100;
    double health = maximumHealth;
    bool dead = false;
    bool menuOpen = false;
    bool chatFocused = false;
    bool chatOpen = false;
    bool leaveRequested = false;
    std::string menuTab = "Giocatori";
    double sensitivity = 1.0;
    int deaths = 0;
    std::vector<OutputLine> console;
    bool loaded = false;
    std::vector<GuiNode> gui;
    std::vector<PlayerTool> tools;
    std::function<void(const std::string&)> activate;
    std::function<void(const std::string&, const std::string&)> commit;
    std::function<void(const std::string&, double)> scroll;
    std::function<void(int)> equip;
    bool resetRequested = false;

    std::optional<AvatarSnapshot> local() const;
    void setLocal(std::optional<AvatarSnapshot> snapshot);
    std::map<SessionId, RemoteAvatar> remotes() const;
    void setRemotes(std::map<SessionId, RemoteAvatar> value);
    void report(const OutputLine& line);
    bool suspended() const { return menuOpen || chatFocused; }
    void reset() {
        resetRequested = true;
        menuOpen = false;
    }

private:
    mutable std::mutex lock;
    std::optional<AvatarSnapshot> storedLocal;
    std::map<SessionId, RemoteAvatar> storedRemotes;
};

class PlayerSession : public std::enable_shared_from_this<PlayerSession> {
public:
    PlayerSession(JoinTicket ticket, std::shared_ptr<Backend> backend, std::shared_ptr<AssetService> assets);
    ~PlayerSession();

    const JoinTicket ticket;
    const std::shared_ptr<Backend> backend;
    const std::shared_ptr<AssetService> assets;
    std::shared_ptr<PlayerGameplay> gameplay;
    std::string displayName;
    std::optional<AvatarConfiguration> avatar;
    std::function<bool(const std::filesystem::path&)> meshReadable;
    std::function<void()> changed;

    JoinPhase phase() const { return machine.phase(); }
    const JoinStateMachine& state() const { return machine; }
    std::optional<JoinFailure> failure() const { return failed; }
    const std::optional<Experience>& world() const { return loadedWorld; }
    const std::optional<ExperienceServer>& server() const { return assigned; }
    const std::vector<std::string>& roster() const { return names; }
    const std::vector<ChatMessage>& chat() const { return messages; }
    std::string chatError;
    std::optional<int> visits() const { return visitCount; }
    bool ready() const { return isReady; }
    bool left() const { return hasLeft; }
    const std::map<SessionId, RemoteAvatar>& remotes() const { return remoteAvatars; }

    Async<void> start();
    Async<void> exchange(double now);
    Async<void> flow(double now);
    Async<void> refresh(bool heartbeat);
    Async<void> send(std::string text);
    Async<void> leave();

private:
    JoinStateMachine machine{JoinPhase::connecting};
    std::optional<JoinFailure> failed;
    std::optional<Experience> loadedWorld;
    std::optional<ExperienceServer> assigned;
    std::vector<std::string> names;
    std::vector<ChatMessage> messages;
    std::optional<int> visitCount;
    bool isReady = false;
    bool hasLeft = false;
    std::map<SessionId, RemoteAvatar> remoteAvatars;
    std::optional<std::string> presence;
    std::unique_ptr<DirectTransport> direct;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    int cursor = 0;
    bool opened = false;
    int published = 0;
    std::map<SessionId, int> retired;
    std::optional<std::string> localPlayer;

    Json identity() const;
    void apply(const Json& state);
    std::optional<ReplicationFrame> outgoing();
    void merge(const std::map<SessionId, ReplicationFrame>& frames, double now, bool authoritative);
    void startPulse();
    void startStream();
    void notify();
    Async<void> release();
    void move(JoinPhase next);
};

}
