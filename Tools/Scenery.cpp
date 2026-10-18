#include "Scenery.hpp"

#include <array>

namespace scenery {

using namespace ld;

namespace {

const std::array<const char*, 10> skins{"F1C9A5", "E0AC86", "C68A5E", "8D5A3B", "5C3A28", "F5D7B8", "D9A47A", "B0754E", "7A4A2E", "EBC09A"};
const std::array<const char*, 12> clothes{"E0392B", "2E6FF2", "F2C230", "2FA84F", "6730EE", "F2F3F5", "1B1D21", "E46F5E", "2F8F8A", "8C7BB8", "3E4A61", "D98E04"};

}

Builder::Builder(unsigned seed, std::string prefix, std::string label) : random(seed), prefix(std::move(prefix)), label(std::move(label)) {}

double Builder::uniform(double low, double high) { return std::uniform_real_distribution<double>(low, high)(random); }

int Builder::pick(int size) { return std::uniform_int_distribution<int>(0, size - 1)(random); }

WorldBlock& Builder::part(Vec3 centre, Vec3 size, const std::string& color, const std::string& kind) {
    WorldBlock b;
    b.id = prefix + "-" + std::to_string(++count);
    b.name = label;
    b.kind = kind;
    b.x = centre.x;
    b.y = centre.y;
    b.z = centre.z;
    b.sizeX = size.x;
    b.sizeY = size.y;
    b.sizeZ = size.z;
    b.color = color;
    world.blocks.push_back(b);
    return world.blocks.back();
}

WorldBlock& Builder::glow(Vec3 centre, Vec3 size, const std::string& color, const std::string& kind) {
    auto& b = part(centre, size, color, kind);
    b.material = "neon";
    return b;
}

AvatarConfiguration look(std::mt19937& random, bool female) {
    auto config = AvatarConfiguration::standard("", female ? "female" : "male");
    auto choose = [&](const auto& list) { return std::string(list[std::uniform_int_distribution<std::size_t>(0, list.size() - 1)(random)]); };
    std::string skin = choose(skins), top = choose(clothes), legs = choose(clothes);
    config.bodyColors = {{"head", skin}, {"leftArm", std::uniform_int_distribution<int>(0, 2)(random) ? skin : top}, {"rightArm", skin}, {"torso", top}, {"leftLeg", legs}, {"rightLeg", legs}};
    config.bodyColors["rightArm"] = config.bodyColors["leftArm"];
    return config;
}

}
