#pragma once

#include "Terrain.hpp"

#include <cstdint>
#include <vector>

namespace ld {

struct TerrainMesh {
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<Vec3> colors;
    std::vector<std::uint8_t> kinds;
    std::vector<std::uint32_t> indices;
    bool empty() const { return indices.empty(); }
};

TerrainMesh meshChunk(const Terrain& terrain, const ChunkKey& key, bool water);
TerrainMesh meshWater(const Terrain& terrain);

}
