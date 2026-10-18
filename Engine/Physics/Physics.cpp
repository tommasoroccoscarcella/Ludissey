#include "Engine/Physics/Physics.hpp"

#include "Engine/Character/Character.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ld::physics {

double restingHeight(const WorldBlock& block, const std::vector<const WorldBlock*>& solids, const Terrain& terrain) {
    Vec3 half = block.halfExtents();
    double floor = -std::numeric_limits<double>::max();
    if (auto ground = terrain.surface(block.x, block.z, block.y - half.y + 0.05)) floor = std::max(floor, *ground + half.y);
    for (const WorldBlock* candidate : solids) {
        if (candidate->id == block.id || !candidate->canCollide) continue;
        Vec3 reach = candidate->halfExtents();
        if (!(std::fabs(block.x - candidate->x) < (half.x + reach.x) * 0.98 && std::fabs(block.z - candidate->z) < (half.z + reach.z) * 0.98)) continue;
        double top = candidate->y + reach.y;
        if (top > block.y + 0.01) continue;
        floor = std::max(floor, top + half.y);
    }
    return floor;
}

Body integrate(const Body& body, const std::vector<const WorldBlock*>& solids, const Terrain& terrain, double delta) {
    Body next = body;
    if (body.block.anchored) return next;
    double rest = restingHeight(body.block, solids, terrain);
    next.velocity.y -= motion::gravity * delta;
    next.block.x += next.velocity.x * delta;
    next.block.z += next.velocity.z * delta;
    next.block.y += next.velocity.y * delta;
    if (next.block.y <= rest) {
        next.block.y = rest;
        next.velocity.y = 0;
        double friction = std::max(0.0, 1 - 8 * delta);
        next.velocity.x *= friction;
        next.velocity.z *= friction;
    }
    next.block.y = std::min(static_cast<double>(rules::world::vertical), next.block.y);
    return next;
}

bool fallen(const WorldBlock& block) { return block.y < motion::voidHeight; }

std::optional<Contact> touching(const WorldBlock& block, const Capsule& character) {
    Vec3 half = block.halfExtents();
    double low = character.feet.y, high = character.feet.y + character.height;
    if (!(std::fabs(character.feet.x - block.x) < half.x + character.radius + 0.05 && std::fabs(character.feet.z - block.z) < half.z + character.radius + 0.05 && low < block.y + half.y + 0.05 &&
          high > block.y - half.y - 0.05))
        return std::nullopt;
    return Contact{block.y + half.y < low + 0.6};
}

}
