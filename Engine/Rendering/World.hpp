#pragma once

#include "Engine/Avatar/Avatar.hpp"
#include "Engine/Experience/Experience.hpp"
#include "Engine/Rendering/Scene.hpp"
#include "Engine/Terrain/Terrain.hpp"

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace ld::render {

struct CharacterFrame {
    std::vector<float> vertices;
    std::vector<float> normals;
    CharacterFrame blended(const CharacterFrame& other, float weight) const;
};

struct CharacterClip {
    double duration = 1;
    std::vector<std::vector<CharacterFrame>> frames;
    std::vector<CharacterFrame> sample(double phase) const;
};

struct CharacterAsset {
    std::vector<std::string> names;
    std::vector<CharacterFrame> rest;
    std::map<std::string, CharacterClip> clips;
    static std::shared_ptr<const CharacterAsset> load(const std::filesystem::path& path);
};

class CharacterModel {
public:
    CharacterModel(std::shared_ptr<const CharacterAsset> asset, const AvatarConfiguration& avatar);
    NodePtr node = Node::make();
    std::optional<AvatarConfiguration> avatar;
    void animate(const std::string& clip, double dt, std::optional<double> phase = std::nullopt);
    void dress(const AvatarConfiguration& avatar);
    const std::vector<CharacterFrame>& frames() const { return current; }
    const std::vector<std::string>& parts() const { return asset->names; }
    const std::map<std::size_t, std::vector<float>>& faces() const { return faceUVs; }

private:
    std::shared_ptr<const CharacterAsset> asset;
    std::vector<NodePtr> meshes;
    std::vector<CharacterFrame> current;
    std::vector<CharacterFrame> transitionFrames;
    std::string state;
    double elapsed = 0;
    double transition = 0;
    NodePtr headAnchor = Node::make();
    NodePtr backAnchor = Node::make();
    std::map<std::size_t, std::vector<float>> faceUVs;
    void placeDecor();
};

namespace face {
inline constexpr double width = 0.95;
inline constexpr double drop = 0.03;
std::filesystem::path chosen(const AvatarConfiguration& avatar);
std::shared_ptr<Texture> texture(const std::filesystem::path& path);
std::vector<float> uvs(const CharacterFrame& head);
}

using CharacterPtr = std::shared_ptr<CharacterModel>;

namespace name_tag {
inline constexpr const char* name = "nametag";
inline constexpr double lift = 2.3;
inline constexpr double low = 0.1;
inline constexpr double high = 0.32;
inline constexpr double perUnit = 0.021;
NodePtr make(const std::string& text);
void fit(const std::vector<NodePtr>& bodies, Vec3 camera);
}

namespace scene_factory {
CharacterPtr avatar(const AvatarConfiguration& avatar, const std::optional<std::filesystem::path>& resource = std::nullopt);
void warm();
void base(Scene& scene);
Background sky(const WorldLighting& lighting);
void illuminate(Scene& scene, const WorldLighting& lighting);
GeometryPtr mesh(const std::filesystem::path& path);
inline constexpr double edgeRadius = 0.05;
double edge(Vec3 size);
GeometryPtr cuboid(Vec3 size);
bool cuboidal(const WorldBlock& block);
Bounds bounds(const WorldBlock& block, const Node& node);
Material material(const WorldBlock& block);
std::string structure(const WorldBlock& block);
void apply(const WorldBlock& block, Node& node);
NodePtr block(const WorldBlock& block);
}

namespace avatar_catalog {
NodePtr accessory(const AvatarItem& item);
}

class TerrainView {
public:
    NodePtr root = Node::make("ludissey.terrain");
    void sync(const Terrain& terrain);
    std::optional<Underwater> underwater(Vec3 point) const;
    int rebuilt = 0;
    int triangles = 0;
    bool water = false;
    std::size_t chunkNodes() const { return nodes.size(); }
    NodePtr waterNode() const { return surface; }

private:
    std::map<ChunkKey, NodePtr> nodes;
    std::map<ChunkKey, std::uint64_t> signatures;
    NodePtr surface;
    std::uint64_t surfaceSignature = 0;
    Terrain current;
};

struct WorldScene {
    Scene scene;
    TerrainView terrain;
    std::map<std::string, NodePtr> parts;
};

std::unique_ptr<WorldScene> worldScene(const Experience& experience);

}
