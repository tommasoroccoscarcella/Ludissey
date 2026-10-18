#pragma once

#include "Engine/Core/Math.hpp"
#include "Engine/Experience/Experience.hpp"
#include "Engine/Terrain/Terrain.hpp"

#include <optional>
#include <vector>

namespace ld::physics {

struct Body {
    WorldBlock block;
    Vec3 velocity;
};

struct Capsule {
    Vec3 feet;
    double radius = 0;
    double height = 0;
};

struct Contact {
    bool feet = false;
};

double restingHeight(const WorldBlock& block, const std::vector<const WorldBlock*>& solids, const Terrain& terrain);
Body integrate(const Body& body, const std::vector<const WorldBlock*>& solids, const Terrain& terrain, double delta);
bool fallen(const WorldBlock& block);
std::optional<Contact> touching(const WorldBlock& block, const Capsule& character);

}
