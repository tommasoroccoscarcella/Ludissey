#pragma once

#include "Engine/Core/Json.hpp"
#include "Engine/Core/Math.hpp"
#include "Engine/Core/Result.hpp"
#include "Engine/Core/Rules.hpp"

#include <array>
#include <compare>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace ld {

struct ChunkKey {
    int x = 0, y = 0, z = 0;
    auto operator<=>(const ChunkKey&) const = default;
    std::string text() const;
    static std::optional<ChunkKey> parse(std::string_view text);
};

enum class TerrainMaterial : std::uint8_t { air = 0, grass, rock, sand, mud, water };

std::string_view materialName(TerrainMaterial material);
std::string materialTitle(TerrainMaterial material);
std::string_view materialColor(TerrainMaterial material);
bool isSolid(TerrainMaterial material);

struct TerrainSettings {
    int seed = 1;
    double amplitude = rules::terrain::generation::amplitude;
    double baseHeight = rules::terrain::generation::baseHeight;
    double waterLevel = rules::terrain::generation::waterLevel;
    bool operator==(const TerrainSettings&) const = default;
    Json json() const;
    static TerrainSettings fromJson(const Json& value);
};

struct TerrainWater {
    std::string color{rules::terrain::water::color};
    double transparency = rules::terrain::water::transparency::standard;
    double reflectance = rules::terrain::water::reflectance::standard;
    double waveSize = rules::terrain::water::waveSize::standard;
    double waveSpeed = rules::terrain::water::waveSpeed::standard;
    bool operator==(const TerrainWater&) const = default;
    Json json() const;
    static TerrainWater fromJson(const Json& value);
};

struct TerrainRegion {
    Vec3 minimum, maximum;
    bool operator==(const TerrainRegion&) const = default;
    bool valid() const;
    static TerrainRegion whole(double top);
    static TerrainRegion snapped(Vec3 minimum, Vec3 maximum);
};

enum class TerrainTool { add, subtract, paint, smooth, flatten, water, raise };
enum class TerrainPlacement { surface, free };

std::string_view toolName(TerrainTool tool);
std::string toolTitle(TerrainTool tool);

struct TerrainBrush {
    double radius = rules::terrain::brush::standard;
    double strength = 0.5;
    TerrainMaterial material = TerrainMaterial::grass;
    TerrainPlacement placement = TerrainPlacement::surface;
    bool operator==(const TerrainBrush&) const = default;
};

class Terrain {
public:
    static constexpr double cellSize = rules::terrain::cellSize;
    static constexpr int cellsX = rules::terrain::cellsX, cellsY = rules::terrain::cellsY, cellsZ = rules::terrain::cellsZ;
    static constexpr int chunk = rules::terrain::chunk;
    static constexpr int levels = rules::terrain::levels;
    static constexpr int volume = chunk * chunk * chunk;
    static constexpr Vec3 origin{-cellsX * cellSize / 2, static_cast<double>(rules::terrain::bottom), -cellsZ * cellSize / 2};
    static constexpr int chunksX = cellsX / chunk, chunksY = cellsY / chunk, chunksZ = cellsZ / chunk;

    std::optional<TerrainSettings> settings;
    TerrainWater waterProperties;

    bool empty() const { return chunks.empty(); }
    bool present() const { return !chunks.empty() || settings || waterProperties != TerrainWater{}; }
    std::vector<ChunkKey> keys() const;
    const std::map<ChunkKey, std::vector<std::uint16_t>>& data() const { return chunks; }
    bool operator==(const Terrain& other) const { return settings == other.settings && waterProperties == other.waterProperties && chunks == other.chunks; }

    static std::uint16_t compose(TerrainMaterial solid, int solidLevel, int waterLevel);
    static TerrainMaterial material(std::uint16_t cell);
    static int occupancy(std::uint16_t cell) { return cell & 15; }
    static int waterLevel(std::uint16_t cell) { return (cell >> 8) & 15; }
    static std::uint16_t legacy(std::uint8_t byte);
    static bool inside(int i, int j, int k) { return i >= 0 && j >= 0 && k >= 0 && i < cellsX && j < cellsY && k < cellsZ; }
    static ChunkKey key(int i, int j, int k) { return {i / chunk, j / chunk, k / chunk}; }
    static int offset(int i, int j, int k) { return ((i % chunk) * chunk + j % chunk) * chunk + k % chunk; }
    static int index(double value, double start) { return static_cast<int>(std::floor((value - start) / cellSize)); }
    static Vec3 center(int i, int j, int k) { return {origin.x + (i + 0.5) * cellSize, origin.y + (j + 0.5) * cellSize, origin.z + (k + 0.5) * cellSize}; }

    std::uint16_t cell(int i, int j, int k) const;
    void set(int i, int j, int k, std::uint16_t value);
    void clear();
    double solidDensity(int i, int j, int k) const;
    double waterDensity(int i, int j, int k) const;
    double sample(double x, double y, double z, bool water = false) const;
    bool solid(double x, double y, double z) const { return !empty() && sample(x, y, z) >= 0.5; }
    bool water(double x, double y, double z) const { return !empty() && sample(x, y, z, true) >= 0.5; }

    std::optional<double> surface(double x, double z, double top) const;
    std::optional<double> exit(double x, double z, double bottom) const;
    std::optional<double> raycast(Vec3 from, Vec3 direction, double length) const;

    static double hash(long long a, long long b, long long seed);
    static double noise(double x, double z, long long seed, int octaves);
    void generate(const TerrainSettings& settings);
    int fillWater(Vec3 minimum, Vec3 maximum);
    int clearWater(Vec3 minimum, Vec3 maximum);
    int fillRegion(Vec3 minimum, Vec3 maximum, TerrainMaterial material);
    int clearRegion(Vec3 minimum, Vec3 maximum);
    int apply(TerrainTool tool, Vec3 point, const TerrainBrush& brush);
    static Vec3 snapped(Vec3 point);
    std::optional<double> surfaceHeight(int i, int k) const;
    Result<int> fillSphere(Vec3 point, double radius, TerrainMaterial material);
    static std::optional<TerrainMaterial> dominant(const Terrain& terrain, int i, int j, int k);

    static std::string encodeChunk(const std::vector<std::uint16_t>& values);
    static Result<std::vector<std::uint16_t>> decodeChunk(std::string_view text, int format = rules::terrain::format);
    Json json() const;
    static Result<Terrain> fromJson(const Json& value);

    std::uint64_t stamp(const ChunkKey& key) const;
    std::uint64_t neighbourhood(const ChunkKey& key) const;

private:
    std::map<ChunkKey, std::vector<std::uint16_t>> chunks;
    std::map<ChunkKey, std::uint64_t> stamps;
    static std::uint64_t nextStamp();
    int columns(TerrainTool tool, Vec3 point, const TerrainBrush& brush);
};

}
