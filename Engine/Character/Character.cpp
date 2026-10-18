#include "Character.hpp"

#include <limits>
#include <numeric>

namespace ld {

namespace world_space {

Vec3 forward(double yaw, double pitch) { return {-std::cos(pitch) * std::sin(yaw), -std::sin(pitch), -std::cos(pitch) * std::cos(yaw)}; }
Vec3 right(double yaw) { return {std::cos(yaw), 0, -std::sin(yaw)}; }
Vec3 orbit(double yaw, double pitch, double distance) {
    Vec3 f = forward(yaw, pitch);
    return {-f.x * distance, -f.y * distance, -f.z * distance};
}

}

namespace motion {

double slope() { return std::tan(static_cast<double>(rules::locomotion::maxSlope) * pi / 180); }

Vec2 direction(const CharacterInput& input, double cameraYaw) {
    Vec3 f = world_space::forward(cameraYaw, 0);
    Vec3 r = world_space::right(cameraYaw);
    double dx = f.x * input.forward + r.x * input.strafe;
    double dz = f.z * input.forward + r.z * input.strafe;
    double length = std::sqrt(dx * dx + dz * dz);
    if (length <= epsilon) return {0, 0};
    return {dx / length, dz / length};
}

double shortestAngle(double from, double to) {
    double delta = std::fmod(to - from, 2 * pi);
    if (delta > pi) delta -= 2 * pi;
    if (delta < -pi) delta += 2 * pi;
    return delta;
}

double heading(double dx, double dz) { return std::atan2(dx, dz); }

CharacterState step(const CharacterState& state, const CharacterInput& input, double cameraYaw, const std::vector<WorldBlock>& blocks, const Terrain& terrain, double delta) {
    if (!(delta > 0)) return state;
    int count = std::max(1, static_cast<int>(std::ceil(delta / substep - 1e-9)));
    std::vector<const WorldBlock*> solids;
    solids.reserve(blocks.size());
    for (const auto& b : blocks)
        if (b.canCollide) solids.push_back(&b);
    CharacterState result = state;
    for (int index = 0; index < count; ++index) {
        CharacterInput slice = input;
        slice.jump = input.jump && index == 0;
        result = integrate(result, slice, cameraYaw, solids, terrain, delta / count);
    }
    return result;
}

CharacterState integrate(const CharacterState& state, const CharacterInput& input, double cameraYaw, const std::vector<const WorldBlock*>& solids, const Terrain& terrain, double delta) {
    CharacterState result = state;
    Vec2 move = direction(input, cameraYaw);
    result.moving = std::abs(move.x) + std::abs(move.y) > epsilon;
    if (result.moving) {
        double desired = heading(move.x, move.y);
        double error = shortestAngle(state.yaw, desired);
        double maximum = turnSpeed * delta;
        result.yaw = state.yaw + std::min(maximum, std::max(-maximum, error));
    }
    bool swimming = terrain.water(state.x, state.y + height * 0.5, state.z);
    result.swimming = swimming;
    bool walking = state.grounded && !swimming && !input.jump;
    double pace = (swimming ? speed * rules::swimming::speedFactor : speed) * input.pace;
    double slopeValue = slope();
    auto blocked = [&](double x, double z) {
        double y = state.y, climb = walking ? stepHeight : 0.08;
        for (const WorldBlock* block : solids) {
            Vec3 half = block->halfExtents();
            if (std::abs(x - block->x) < half.x + radius && std::abs(z - block->z) < half.z + radius && y < block->y + half.y - climb && y + height > block->y - half.y) return true;
        }
        if (terrain.empty()) return false;
        if (terrain.solid(x, y + height * 0.8, z)) return true;
        if (!walking) return terrain.solid(x, y + stepUp + 0.1, z);
        auto ground = terrain.surface(x, z, y + stepUp);
        if (!ground) return terrain.solid(x, y + stepUp, z);
        double run = std::sqrt((x - state.x) * (x - state.x) + (z - state.z) * (z - state.z));
        return *ground - y > slopeValue * run + rules::locomotion::snapDistance;
    };
    double proposedX = state.x + move.x * pace * delta;
    double proposedZ = state.z + move.y * pace * delta;
    if (!blocked(proposedX, state.z)) result.x = proposedX;
    if (!blocked(result.x, proposedZ)) result.z = proposedZ;
    if (swimming) {
        bool submerged = terrain.water(state.x, state.y + height * 0.85, state.z);
        double lift = submerged ? gravity * (1 - rules::swimming::buoyancy) * 2 : -gravity * (1 - rules::swimming::buoyancy);
        result.vertical = state.vertical * (1 - std::min(1.0, rules::swimming::drag * delta)) + lift * delta;
        if (input.ascend || input.jump) result.vertical += (rules::swimming::ascend - result.vertical) * std::min(1.0, 4 * delta);
    } else {
        if (input.jump && state.grounded && input.leap > 0) {
            result.vertical = jumpSpeed * input.leap;
            result.grounded = false;
        }
        result.vertical -= gravity * delta;
    }
    double previousY = state.y;
    double level = previousY + result.vertical * delta;
    double floor = -std::numeric_limits<double>::max();
    double reach = walking ? stepHeight : 0.12;
    for (const WorldBlock* block : solids) {
        Vec3 half = block->halfExtents();
        double top = block->y + half.y;
        if (std::abs(result.x - block->x) < half.x + 0.15 && std::abs(result.z - block->z) < half.z + 0.15 && previousY >= top - reach && result.vertical <= 0) floor = std::max(floor, top);
    }
    if (!terrain.empty() && result.vertical <= 0) {
        if (auto top = footprint(terrain, result.x, result.z, previousY + stepUp)) floor = std::max(floor, *top);
        else if (terrain.solid(result.x, previousY + 0.05, result.z)) {
            if (auto exit = terrain.exit(result.x, result.z, previousY)) floor = std::max(floor, *exit);
        }
    }
    double run = std::sqrt((result.x - state.x) * (result.x - state.x) + (result.z - state.z) * (result.z - state.z));
    double snap = slopeValue * run + rules::locomotion::snapDistance;
    if (level <= floor || (walking && result.vertical <= 0 && previousY - floor <= snap)) {
        level = floor;
        result.vertical = 0;
        result.grounded = true;
    } else result.grounded = false;
    result.y = level;
    return result;
}

std::optional<double> footprint(const Terrain& terrain, double x, double z, double top) {
    auto center = terrain.surface(x, z, top);
    if (!center) return std::nullopt;
    double total = *center, count = 1;
    const double offsets[4][2] = {{radius, 0}, {-radius, 0}, {0, radius}, {0, -radius}};
    for (const auto& o : offsets) {
        auto h = terrain.surface(x + o[0], z + o[1], top);
        if (!h || std::abs(*h - *center) >= stepUp) continue;
        total += *h;
        count += 1;
    }
    return total / count;
}

bool fallen(const CharacterState& state) { return state.y < voidHeight; }

CharacterState spawn(const std::vector<WorldBlock>& blocks, const Terrain& terrain, double x, double z) {
    double top = -std::numeric_limits<double>::max();
    for (const auto& block : blocks) {
        if (!block.canCollide) continue;
        Vec3 half = block.halfExtents();
        if (std::abs(x - block.x) <= half.x && std::abs(z - block.z) <= half.z) top = std::max(top, block.y + half.y);
    }
    if (auto ground = terrain.surface(x, z, Terrain::origin.y + Terrain::cellsY * Terrain::cellSize)) top = std::max(top, *ground);
    CharacterState s;
    s.x = x;
    s.y = top > -std::numeric_limits<double>::max() ? top : 0;
    s.z = z;
    return s;
}

const WorldBlock* spawnLocation(const std::vector<WorldBlock>& blocks, const std::optional<std::string>& teamColor) {
    auto flag = [](const WorldBlock& block, const char* name, bool standard) {
        auto found = block.properties.find(name);
        return found != block.properties.end() && found->second.kind == PropertyValue::Kind::boolean ? found->second.flag : standard;
    };
    const WorldBlock* neutral = nullptr;
    for (const auto& block : blocks) {
        if (block.kind != "spawn" || !flag(block, "Enabled", true)) continue;
        if (flag(block, "Neutral", true)) {
            if (!neutral) neutral = &block;
            continue;
        }
        auto color = block.properties.find("TeamColor");
        if (teamColor && color != block.properties.end() && color->second.text == *teamColor) return &block;
    }
    return neutral;
}

CharacterState arrival(const std::vector<WorldBlock>& blocks, const Terrain& terrain, const std::optional<std::string>& teamColor) {
    if (const WorldBlock* location = spawnLocation(blocks, teamColor)) {
        CharacterState s = spawn(blocks, terrain, location->x, location->z);
        s.y = std::max(s.y, location->y + location->halfExtents().y);
        return s;
    }
    return spawn(blocks, terrain);
}

}

void CharacterDriver::reset(const CharacterState& state) {
    last = state;
    now = state;
    accumulator = 0;
    renderedY.reset();
    aimed.reset();
    catching = false;
    recent.clear();
    focus.reset();
}

int CharacterDriver::advance(double delta, const std::function<std::optional<CharacterState>(const CharacterState&, double)>& simulate) {
    accumulator += std::min(maximumFrame, std::max(0.0, delta));
    int steps = 0;
    while (accumulator >= step) {
        accumulator -= step;
        ++steps;
        auto next = simulate(now, step);
        if (!next) {
            accumulator = 0;
            return steps;
        }
        double jump = std::sqrt((next->x - now.x) * (next->x - now.x) + (next->y - now.y) * (next->y - now.y) + (next->z - now.z) * (next->z - now.z));
        if (jump > teleport) reset(*next);
        else {
            last = now;
            now = *next;
        }
    }
    return steps;
}

CharacterPose CharacterDriver::pose(double delta) {
    double t = alpha();
    double target = last.y + (now.y - last.y) * t;
    double shown = renderedY.value_or(target);
    double rise = motion::speed * motion::slope() * 1.25;
    double decay = std::exp(-stepResponse * std::max(0.0, delta));
    bool behind = target > shown + rise * std::max(delta, step);
    bool supported = now.grounded || (last.grounded && now.vertical <= 0);
    if (behind && supported) {
        shown += (target - shown) * (1 - decay);
        catching = true;
    } else if (behind && catching) shown = target - std::max(0.0, aimed.value_or(target) - shown) * decay;
    else {
        shown = target;
        catching = false;
    }
    aimed = target;
    if (now.grounded && last.grounded) {
        recent.push_back(shown);
        if (recent.size() > 3) recent.erase(recent.begin(), recent.begin() + static_cast<long>(recent.size() - 3));
        shown = std::accumulate(recent.begin(), recent.end(), 0.0) / static_cast<double>(recent.size());
    } else recent = {shown};
    renderedY = shown;
    return {last.x + (now.x - last.x) * t, shown, last.z + (now.z - last.z) * t, last.yaw + motion::shortestAngle(last.yaw, now.yaw) * t};
}

Vec3 CharacterDriver::follow(Vec3 head, double delta) {
    if (!focus) {
        focus = head;
        return head;
    }
    double horizontal = 1 - std::exp(-horizontalResponse * std::max(0.0, delta)), vertical = 1 - std::exp(-verticalResponse * std::max(0.0, delta));
    Vec3 next{focus->x + (head.x - focus->x) * horizontal, focus->y + (head.y - focus->y) * vertical, focus->z + (head.z - focus->z) * horizontal};
    focus = next;
    return next;
}

double FramePacer::next(double delta, double refresh) {
    if (!(refresh > 0)) return delta;
    double owed = delta + debt;
    double paced = std::max(1.0, std::round(owed / refresh)) * refresh;
    debt = std::max(-refresh, std::min(refresh, owed - paced));
    return paced;
}

namespace camera_input {

Vec3 forward(double yaw, double pitch) { return world_space::forward(yaw, pitch); }
Vec3 right(double yaw) { return world_space::right(yaw); }
Vec3 up(double yaw, double pitch) {
    Vec3 f = forward(yaw, pitch), r = right(yaw);
    return {r.y * f.z - r.z * f.y, r.z * f.x - r.x * f.z, r.x * f.y - r.y * f.x};
}

Vec2 look(double yaw, double pitch, double dx, double dy, double sensitivity, double low, double high) {
    return {yaw - dx * lookSpeed * sensitivity, std::min(high, std::max(low, pitch - dy * lookSpeed * sensitivity))};
}

Vec3 pan(Vec3 point, double yaw, double pitch, double dx, double dy, double scale) {
    Vec3 r = right(yaw), u = up(yaw, pitch);
    return {point.x - (r.x * dx + u.x * dy) * scale, point.y - (r.y * dx + u.y * dy) * scale, point.z - (r.z * dx + u.z * dy) * scale};
}

Vec3 eye(Vec3 targetPoint, double yaw, double pitch, double distance) {
    Vec3 f = forward(yaw, pitch);
    return {targetPoint.x - f.x * distance, targetPoint.y - f.y * distance, targetPoint.z - f.z * distance};
}

Vec3 target(Vec3 eyePoint, double yaw, double pitch, double distance) {
    Vec3 f = forward(yaw, pitch);
    return {eyePoint.x + f.x * distance, eyePoint.y + f.y * distance, eyePoint.z + f.z * distance};
}

std::optional<Vec3> fly(double move, double strafe, double rise, double yaw, double pitch) {
    Vec3 f = forward(yaw, pitch), r = right(yaw);
    Vec3 d{f.x * move + r.x * strafe, f.y * move + r.y * strafe + rise, f.z * move + r.z * strafe};
    double length = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    if (length <= 1e-9) return std::nullopt;
    return Vec3{d.x / length, d.y / length, d.z / length};
}

}

namespace camera_rig {

double zoom(double distance, double amount) {
    if (amount > 0 && distance <= firstPersonDistance) return minimumDistance;
    double base = distance <= firstPersonDistance ? firstPersonDistance : distance;
    return std::max(minimumDistance, std::min(maximumDistance, base * std::exp(-amount)));
}

bool firstPerson(double distance) { return distance <= firstPersonDistance; }

std::optional<double> hit(const WorldBlock& block, Vec3 origin, Vec3 direction, double length) {
    Vec3 half = block.halfExtents();
    double lowers[3] = {block.x - half.x - skin, block.y - half.y - skin, block.z - half.z - skin};
    double uppers[3] = {block.x + half.x + skin, block.y + half.y + skin, block.z + half.z + skin};
    double origins[3] = {origin.x, origin.y, origin.z};
    double directions[3] = {direction.x, direction.y, direction.z};
    double near = 0, far = length;
    for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(directions[axis]) < 1e-9) {
            if (origins[axis] < lowers[axis] || origins[axis] > uppers[axis]) return std::nullopt;
            continue;
        }
        double entry = (lowers[axis] - origins[axis]) / directions[axis];
        double exit = (uppers[axis] - origins[axis]) / directions[axis];
        if (entry > exit) std::swap(entry, exit);
        near = std::max(near, entry);
        far = std::min(far, exit);
        if (near > far) return std::nullopt;
    }
    return near;
}

double clearance(Vec3 head, double yaw, double pitch, double distance, const std::vector<WorldBlock>& blocks, const Terrain& terrain) {
    if (distance <= firstPersonDistance) return distance;
    Vec3 direction = world_space::orbit(yaw, pitch, 1);
    double allowed = distance;
    if (auto h = terrain.raycast(head, direction, distance)) allowed = std::min(allowed, std::max(0.0, *h - skin));
    for (const auto& block : blocks) {
        if (!block.canCollide) continue;
        if (auto travel = hit(block, head, direction, distance)) allowed = std::min(allowed, *travel);
    }
    return std::max(0.0, std::min(distance, allowed));
}

Placement place(Vec3 head, double yaw, double pitch, double distance, const std::vector<WorldBlock>& blocks, const Terrain& terrain) {
    double allowed = clearance(head, yaw, pitch, distance, blocks, terrain);
    if (firstPerson(allowed)) return {head, true};
    Vec3 shift = world_space::orbit(yaw, pitch, allowed);
    return {head + shift, false};
}

double recover(double current, double targetDistance, double delta) {
    if (targetDistance <= current) return targetDistance;
    return std::min(targetDistance, current + (targetDistance - current) * std::min(1.0, recoverySpeed * delta));
}

}

}
