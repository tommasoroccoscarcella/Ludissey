#pragma once

#include "Engine/Core/Math.hpp"
#include "Engine/Core/Rules.hpp"
#include "Engine/Experience/Experience.hpp"
#include "Engine/Terrain/Terrain.hpp"

#include <functional>
#include <optional>
#include <vector>

namespace ld {

struct CharacterInput {
    double forward = 0;
    double strafe = 0;
    bool jump = false;
    bool ascend = false;
    double pace = 1;
    double leap = 1;
    bool operator==(const CharacterInput&) const = default;
};

struct CharacterState {
    double x = 0, y = 0, z = 0;
    double yaw = 0;
    double vertical = 0;
    bool grounded = true;
    bool moving = false;
    bool swimming = false;
    bool operator==(const CharacterState&) const = default;
    Vec3 position() const { return {x, y, z}; }
};

namespace world_space {
Vec3 forward(double yaw, double pitch);
Vec3 right(double yaw);
Vec3 orbit(double yaw, double pitch, double distance);
}

namespace motion {
inline constexpr double speed = 4;
inline constexpr double gravity = 18;
inline constexpr double jumpSpeed = 6.5;
inline constexpr double radius = 0.22;
inline constexpr double height = 1.6;
inline constexpr double voidHeight = rules::world::voidHeight;
inline constexpr double turnSpeed = 14;
inline constexpr double epsilon = 0.0001;
inline constexpr double stepUp = rules::terrain::stepUp;
inline constexpr double stepHeight = rules::locomotion::stepHeight;
double slope();
inline constexpr double substep = 1.0 / rules::locomotion::substepHertz;

Vec2 direction(const CharacterInput& input, double cameraYaw);
double shortestAngle(double from, double to);
double heading(double dx, double dz);
CharacterState step(const CharacterState& state, const CharacterInput& input, double cameraYaw, const std::vector<WorldBlock>& blocks, const Terrain& terrain, double delta);
CharacterState integrate(const CharacterState& state, const CharacterInput& input, double cameraYaw, const std::vector<const WorldBlock*>& solids, const Terrain& terrain, double delta);
std::optional<double> footprint(const Terrain& terrain, double x, double z, double top);
bool fallen(const CharacterState& state);
CharacterState spawn(const std::vector<WorldBlock>& blocks, const Terrain& terrain, double x = 0, double z = 5);
const WorldBlock* spawnLocation(const std::vector<WorldBlock>& blocks, const std::optional<std::string>& teamColor = std::nullopt);
CharacterState arrival(const std::vector<WorldBlock>& blocks, const Terrain& terrain, const std::optional<std::string>& teamColor = std::nullopt);
}

struct CharacterPose {
    double x = 0, y = 0, z = 0, yaw = 0;
};

class CharacterDriver {
public:
    static constexpr double step = 1.0 / rules::locomotion::simulationHertz;
    static constexpr double maximumFrame = 0.25;
    static constexpr double teleport = 3.0;
    static constexpr double horizontalResponse = 30.0;
    static constexpr double verticalResponse = 10.0;
    static constexpr double stepResponse = 22.0;

    const CharacterState& previous() const { return last; }
    const CharacterState& state() const { return now; }
    void setState(const CharacterState& state) { reset(state); }
    double alpha() const { return accumulator / step; }
    void reset(const CharacterState& state);
    int advance(double delta, const std::function<std::optional<CharacterState>(const CharacterState&, double)>& simulate);
    CharacterPose pose(double delta);
    Vec3 follow(Vec3 head, double delta);

private:
    CharacterState last;
    CharacterState now;
    double accumulator = 0;
    std::optional<double> renderedY;
    std::optional<double> aimed;
    bool catching = false;
    std::vector<double> recent;
    std::optional<Vec3> focus;
};

class FramePacer {
public:
    double next(double delta, double refresh);

private:
    double debt = 0;
};

namespace camera_input {
inline constexpr double lookSpeed = 0.008;
inline constexpr double pitchLimit = 1.54;
inline constexpr double panSpeed = 0.0022;
inline constexpr double characterPitchLow = -1.3;
inline constexpr double characterPitchHigh = 1.4;
Vec3 forward(double yaw, double pitch);
Vec3 right(double yaw);
Vec3 up(double yaw, double pitch);
Vec2 look(double yaw, double pitch, double dx, double dy, double sensitivity = 1, double low = -pitchLimit, double high = pitchLimit);
Vec3 pan(Vec3 point, double yaw, double pitch, double dx, double dy, double scale);
Vec3 eye(Vec3 target, double yaw, double pitch, double distance);
Vec3 target(Vec3 eye, double yaw, double pitch, double distance);
std::optional<Vec3> fly(double move, double strafe, double rise, double yaw, double pitch);
}

namespace camera_rig {
inline constexpr double minimumDistance = 0;
inline constexpr double maximumDistance = 40;
inline constexpr double firstPersonDistance = 1.1;
inline constexpr double skin = 0.25;
inline constexpr double recoverySpeed = 9;
double zoom(double distance, double amount);
bool firstPerson(double distance);
std::optional<double> hit(const WorldBlock& block, Vec3 origin, Vec3 direction, double length);
double clearance(Vec3 head, double yaw, double pitch, double distance, const std::vector<WorldBlock>& blocks, const Terrain& terrain);
struct Placement {
    Vec3 position;
    bool firstPerson = false;
};
Placement place(Vec3 head, double yaw, double pitch, double distance, const std::vector<WorldBlock>& blocks, const Terrain& terrain);
double recover(double current, double target, double delta);
}

namespace editor_flight {
inline constexpr double speed = 14;
inline constexpr double preciseSpeed = 3.5;
inline constexpr double maximumStep = 0.05;
}

}
