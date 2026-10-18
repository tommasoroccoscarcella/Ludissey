#pragma once

#include "Engine/Audio/Audio.hpp"
#include "Engine/DataModel/DataModel.hpp"
#include "Engine/Gui/GuiLayout.hpp"
#include "Engine/Scripting/ScriptEngine.hpp"
#include "Engine/Terrain/Terrain.hpp"

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace ld {

struct Limb {
    std::string name;
    Vec3 size;
    Vec3 offset;
};

struct ToolSlot {
    std::string id;
    std::string name;
    bool equipped = false;
};

class WorldSession {
public:
    enum class Mode { play, run };
    static const std::map<std::uint16_t, std::string>& keyCodes();
    static const std::vector<Limb>& limbs();

    WorldSession(const Experience& experience, Mode mode, std::string playerName, double userId, bool studio, std::function<void(const OutputLine&)> output);
    ~WorldSession();

    std::unique_ptr<DataModel> model;
    std::unique_ptr<ScriptEngine> engine;
    Terrain terrain;
    const Mode mode;
    const std::string playerName;
    bool physics = true;

    InstancePtr player() const { return playerNode; }
    InstancePtr character() const { return characterNode; }
    InstancePtr humanoid() const { return humanoidNode; }
    int revision() const { return model->revision(); }
    int running() const { return engine->running(); }
    bool dead() const { return died; }
    std::vector<WorldBlock> parts() const;
    double health() const;
    double maximumHealth() const;
    double speedFactor() const { return seated ? 0 : humanoidNumber("WalkSpeed", 16) / 16; }
    bool sitting() const { return seated != nullptr; }
    double jumpFactor() const { return humanoidNumber("JumpPower", 50) / 50; }
    static bool disabled(const InstancePtr& node);

    void start(Vec3 point);
    void spawn(Vec3 point);
    void place(double x, double y, double z, double yaw);
    std::optional<Vec3> teleport();
    void kill();
    void key(std::uint16_t code, bool down);
    std::vector<GuiNode> gui() const;
    std::vector<ToolSlot> tools() const;
    void activate(const std::string& id);
    void commit(const std::string& id, const std::string& text);
    void scroll(const std::string& id, double amount);
    void equip(int slot);
    void activateTool();
    void advance(double delta);
    void stop();
    std::optional<std::string> teamColor() const;
    std::optional<ScriptContext> context(const InstancePtr& node) const;

private:
    InstancePtr playerNode;
    InstancePtr characterNode;
    InstancePtr humanoidNode;
    std::set<std::string> launched;
    std::vector<InstancePtr> pending;
    std::set<std::string> contacts;
    bool departed = false;
    InstancePtr seated;
    double clock = 0;
    double seatReady = 0;
    std::map<std::string, audio::Playback> sounds;
    void sound(const InstancePtr& node, bool playing);
    void silence();
    void sit(const InstancePtr& seat);
    void stand();
    void assignTeam();
    std::optional<Vec3> placed;
    bool died = false;

    double humanoidNumber(const std::string& name, double standard) const;
    InstancePtr container(const std::string& className) const;
    void discover(const InstancePtr& node);
    void flush();
    void observeDeath();
    void simulate(double delta);
    bool isCharacter(const InstancePtr& node) const;
    void touch();
};

}
