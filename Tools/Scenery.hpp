#pragma once

#include "Engine/Experience/Experience.hpp"
#include "Engine/Rendering/World.hpp"

#include <memory>
#include <random>
#include <string>
#include <vector>

namespace scenery {

struct Scene {
    std::unique_ptr<ld::render::WorldScene> world;
    std::vector<ld::render::CharacterPtr> people;
    ld::render::NodePtr camera;
    int parts = 0;
};

struct Builder {
    Builder(unsigned seed, std::string prefix, std::string label);
    ld::Experience world;
    std::mt19937 random;
    std::string prefix;
    std::string label;
    int count = 0;

    double uniform(double low, double high);
    int pick(int size);
    ld::WorldBlock& part(ld::Vec3 centre, ld::Vec3 size, const std::string& color, const std::string& kind = "cube");
    ld::WorldBlock& glow(ld::Vec3 centre, ld::Vec3 size, const std::string& color, const std::string& kind = "cube");
};

ld::AvatarConfiguration look(std::mt19937& random, bool female);

}
