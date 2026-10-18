#pragma once

#include "Engine/Character/Character.hpp"
#include "Engine/Multiplayer/PlayerSession.hpp"
#include "Engine/Rendering/World.hpp"
#include "Engine/Scripting/WorldSession.hpp"

namespace ld::render {

class PlayerScene {
public:
    PlayerScene(const Experience& experience, const AvatarConfiguration& avatar, std::shared_ptr<PlayerGameplay> gameplay, decltype(ScriptEngine::storage) storage = {});
    ~PlayerScene();
    PlayerScene(const PlayerScene&) = delete;
    PlayerScene& operator=(const PlayerScene&) = delete;

    void tick(double now, double delta, double wallTime);
    void key(std::uint16_t code, bool down, bool repeat = false);
    void release();
    void activateTool();
    void look(double dx, double dy);
    void zoom(double amount, bool precise);
    void stop();

    const Scene& scene() const { return world->scene; }
    const Node& camera() const { return *eye; }
    const CharacterState& state() const { return driver.state(); }
    WorldSession& runtime() { return *session; }
    const std::shared_ptr<PlayerGameplay> gameplay;
    std::function<void()> changed;

private:
    std::unique_ptr<WorldScene> world;
    std::shared_ptr<WorldSession> session;
    CharacterPtr body;
    NodePtr eye;
    CharacterDriver driver;
    std::vector<WorldBlock> blocks;
    std::map<std::string, std::string> structures;
    std::map<SessionId, CharacterPtr> others;
    std::set<std::uint16_t> held;
    bool jumpRequested = false;
    bool stopped = false;
    int shown = -1;
    int interface = -1;
    double yaw = 0;
    double pitch = .25;
    double distance = 7;
    double respawnAt = 0;

    void synchronize();
    void remotes(double wallTime, double delta);
    void frame(double delta);
    void die(double now);
    void respawn();
};

}
