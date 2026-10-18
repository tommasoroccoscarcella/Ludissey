#include "Mesher.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace ld {

namespace {

Vec3 paletteColor(TerrainMaterial material) {
    Color c = Color::hex(materialColor(material), Color{0.5, 0.5, 0.5, 1});
    return {c.r, c.g, c.b};
}

TerrainMesh meshBox(const Terrain& terrain, int startI, int startJ, int startK, int endI, int endJ, int endK, bool water) {
    const double cs = Terrain::cellSize;
    const int lowI = startI - 1, lowJ = startJ - 1, lowK = startK - 1;
    const int spanI = endI - lowI + 2, spanJ = endJ - lowJ + 2, spanK = endK - lowK + 2;
    std::vector<float> field(static_cast<std::size_t>(spanI * spanJ * spanK), 0);
    std::vector<float> wets(field.size(), 0);
    std::vector<float> solids(field.size(), 0);
    std::vector<std::uint8_t> kinds(field.size(), 0);
    for (int a = 0; a < spanI; ++a)
        for (int b = 0; b < spanJ; ++b)
            for (int c = 0; c < spanK; ++c) {
                int ci = lowI + a, cj = lowJ + b, ck = lowK + c;
                bool outside = ci < 0 || ck < 0 || ci >= Terrain::cellsX || ck >= Terrain::cellsZ;
                std::uint16_t value = terrain.cell(ci, cj, ck);
                TerrainMaterial m = Terrain::material(value);
                auto index = static_cast<std::size_t>((a * spanJ + b) * spanK + c);
                kinds[index] = static_cast<std::uint8_t>(m);
                float solid = isSolid(m) ? static_cast<float>(Terrain::occupancy(value)) / static_cast<float>(Terrain::levels) : 0.0f;
                float wet = static_cast<float>(Terrain::waterLevel(value)) / static_cast<float>(Terrain::levels);
                if (water && outside) {
                    std::uint16_t edge = terrain.cell(std::clamp(ci, 0, Terrain::cellsX - 1), cj, std::clamp(ck, 0, Terrain::cellsZ - 1));
                    solid = 0;
                    wet = static_cast<float>(Terrain::waterLevel(edge)) / static_cast<float>(Terrain::levels);
                }
                wets[index] = wet;
                solids[index] = solid;
                field[index] = water ? std::max(solid, wet) : solid;
            }
    auto at = [&](int i, int j, int k) { return static_cast<std::size_t>(((i - lowI) * spanJ + (j - lowJ)) * spanK + (k - lowK)); };
    auto density = [&](int i, int j, int k) { return field[at(i, j, k)]; };
    auto liquidEdge = [&](std::size_t inner, std::size_t outer) {
        float t = (field[inner] - 0.5f) / (field[inner] - field[outer]);
        float below = std::max(0.0f, t - 0.01f), above = std::min(1.0f, t + 0.15f);
        float wet = wets[inner] + (wets[outer] - wets[inner]) * below;
        float solid = solids[inner] + (solids[outer] - solids[inner]) * above;
        return wet >= 0.5f && solid < 0.5f;
    };
    std::unordered_map<std::size_t, long> cache;
    TerrainMesh mesh;
    static constexpr int edges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    auto vertex = [&](int i, int j, int k) -> long {
        std::size_t id = at(i, j, k);
        if (auto found = cache.find(id); found != cache.end()) return found->second;
        float corners[8];
        for (int c = 0; c < 8; ++c) corners[c] = density(i + (c & 1), j + ((c >> 1) & 1), k + ((c >> 2) & 1));
        if (!water && (i == -1 || k == -1 || i == Terrain::cellsX - 1 || k == Terrain::cellsZ - 1)) {
            float flat[8];
            bool crossed = false;
            for (int c = 0; c < 8; ++c)
                flat[c] = density(std::clamp(i + (c & 1), 0, Terrain::cellsX - 1), j + ((c >> 1) & 1), std::clamp(k + ((c >> 2) & 1), 0, Terrain::cellsZ - 1));
            for (const auto& edge : edges) crossed = crossed || (flat[edge[0]] >= 0.5f) != (flat[edge[1]] >= 0.5f);
            if (crossed) std::copy(std::begin(flat), std::end(flat), std::begin(corners));
        }
        std::size_t cell[8];
        for (int c = 0; c < 8; ++c) cell[c] = at(i + (c & 1), j + ((c >> 1) & 1), k + ((c >> 2) & 1));
        auto wetEdge = [&](int a, int b) { return water && (corners[a] >= 0.5f ? liquidEdge(cell[a], cell[b]) : liquidEdge(cell[b], cell[a])); };
        bool surface = false;
        for (const auto& edge : edges)
            if ((corners[edge[0]] >= 0.5f) != (corners[edge[1]] >= 0.5f) && wetEdge(edge[0], edge[1])) surface = true;
        float sumX = 0, sumY = 0, sumZ = 0, count = 0, level = 0, levelCount = 0;
        for (const auto& edge : edges) {
            int a = edge[0], b = edge[1];
            if ((corners[a] >= 0.5f) == (corners[b] >= 0.5f)) continue;
            float t = (0.5f - corners[a]) / (corners[b] - corners[a]);
            float ax = static_cast<float>(a & 1), ay = static_cast<float>((a >> 1) & 1), az = static_cast<float>((a >> 2) & 1);
            float y = ay + (static_cast<float>((b >> 1) & 1) - ay) * t;
            sumX += ax + (static_cast<float>(b & 1) - ax) * t;
            sumY += y;
            sumZ += az + (static_cast<float>((b >> 2) & 1) - az) * t;
            count += 1;
            if (surface && wetEdge(a, b)) {
                level += y;
                levelCount += 1;
            }
        }
        if (surface) {
            float height = level / levelCount;
            sumY = height * count;
            float ground[4];
            for (int c = 0; c < 4; ++c) {
                int dx = c & 1, dz = c >> 1;
                float low = solids[at(i + dx, j, k + dz)], high = solids[at(i + dx, j + 1, k + dz)];
                ground[c] = low + (high - low) * height;
            }
            static constexpr int sides[4][2] = {{0, 1}, {2, 3}, {0, 2}, {1, 3}};
            float shoreX = 0, shoreZ = 0, shores = 0;
            for (const auto& side : sides) {
                float a = ground[side[0]], b = ground[side[1]];
                if ((a >= 0.6f) == (b >= 0.6f)) continue;
                float t = (0.6f - a) / (b - a);
                float ax = static_cast<float>(side[0] & 1), az = static_cast<float>(side[0] >> 1);
                shoreX += ax + (static_cast<float>(side[1] & 1) - ax) * t;
                shoreZ += az + (static_cast<float>(side[1] >> 1) - az) * t;
                shores += 1;
            }
            if (shores > 0) {
                sumX = shoreX / shores * count;
                sumZ = shoreZ / shores * count;
            }
        }
        if (count <= 0) { cache[id] = -1; return -1; }
        Vec3 base = Terrain::center(i, j, k);
        Vec3 position{base.x + static_cast<double>(sumX / count) * cs, base.y + static_cast<double>(sumY / count) * cs, base.z + static_cast<double>(sumZ / count) * cs};
        if (i == -1) position.x = Terrain::origin.x;
        if (i == Terrain::cellsX - 1) position.x = -Terrain::origin.x;
        if (k == -1) position.z = Terrain::origin.z;
        if (k == Terrain::cellsZ - 1) position.z = -Terrain::origin.z;
        float gx = (corners[1] + corners[3] + corners[5] + corners[7]) - (corners[0] + corners[2] + corners[4] + corners[6]);
        float gy = (corners[2] + corners[3] + corners[6] + corners[7]) - (corners[0] + corners[1] + corners[4] + corners[5]);
        float gz = (corners[4] + corners[5] + corners[6] + corners[7]) - (corners[0] + corners[1] + corners[2] + corners[3]);
        float length = std::sqrt(gx * gx + gy * gy + gz * gz);
        Vec3 normal = length > 1e-6f ? Vec3{static_cast<double>(-gx / length), static_cast<double>(-gy / length), static_cast<double>(-gz / length)} : Vec3{0, 1, 0};
        if (water && std::abs(normal.y) > 0.5) normal = {0, normal.y > 0 ? 1.0 : -1.0, 0};
        auto best = static_cast<std::uint8_t>(TerrainMaterial::water);
        float strongest = -1;
        if (!water) {
            for (int c = 0; c < 8; ++c) {
                std::uint8_t kind = kinds[at(i + (c & 1), j + ((c >> 1) & 1), k + ((c >> 2) & 1))];
                if (kind != 0 && corners[c] > strongest) {
                    strongest = corners[c];
                    best = kind;
                }
            }
        }
        Vec3 tint = water ? Vec3{1, 1, 1} : paletteColor(static_cast<TerrainMaterial>(best));
        double shade = water ? 1.0 : 0.92 + 0.08 * std::max(0.0, normal.y);
        mesh.positions.push_back(position);
        mesh.kinds.push_back(best);
        mesh.normals.push_back(normal);
        mesh.colors.push_back({tint.x * shade, tint.y * shade, tint.z * shade});
        long created = static_cast<long>(mesh.positions.size()) - 1;
        cache[id] = created;
        return created;
    };
    auto quad = [&](int ai, int aj, int ak, int bi, int bj, int bk, int ci, int cj, int ck, int di, int dj, int dk, bool flip) {
        long v0 = vertex(ai, aj, ak);
        if (v0 < 0) return;
        long v1 = vertex(bi, bj, bk);
        if (v1 < 0) return;
        long v2 = vertex(ci, cj, ck);
        if (v2 < 0) return;
        long v3 = vertex(di, dj, dk);
        if (v3 < 0) return;
        long order[6] = {v0, v1, v2, v0, v2, v3};
        if (flip) { order[1] = v3; order[2] = v2; order[4] = v2; order[5] = v1; }
        for (int t = 0; t < 6; t += 3) {
            const Vec3 &a = mesh.positions[static_cast<std::size_t>(order[t])], &b = mesh.positions[static_cast<std::size_t>(order[t + 1])], &c = mesh.positions[static_cast<std::size_t>(order[t + 2])];
            Vec3 facing = cross(b - a, c - a);
            if (facing.length() < 1e-9) continue;
            if (water && dot(facing, mesh.normals[static_cast<std::size_t>(order[t])] + mesh.normals[static_cast<std::size_t>(order[t + 1])] + mesh.normals[static_cast<std::size_t>(order[t + 2])]) <= 0) continue;
            for (int v = t; v < t + 3; ++v) mesh.indices.push_back(static_cast<std::uint32_t>(order[v]));
        }
    };
    auto crossing = [&](int i, int j, int k, int ni, int nj, int nk, bool& inside) {
        bool here = density(i, j, k) >= 0.5f, there = density(ni, nj, nk) >= 0.5f;
        if (here == there) return false;
        inside = here;
        if (!water) return true;
        return here ? liquidEdge(at(i, j, k), at(ni, nj, nk)) : liquidEdge(at(ni, nj, nk), at(i, j, k));
    };
    for (int i = startI; i < endI; ++i)
        for (int j = startJ; j < endJ; ++j)
            for (int k = startK; k < endK; ++k) {
                if (water && (i < 0 || k < 0 || i >= Terrain::cellsX || k >= Terrain::cellsZ)) continue;
                bool inside = false;
                if (crossing(i, j, k, i + 1, j, k, inside)) quad(i, j - 1, k - 1, i, j, k - 1, i, j, k, i, j - 1, k, !inside);
                if (crossing(i, j, k, i, j + 1, k, inside)) quad(i - 1, j, k - 1, i - 1, j, k, i, j, k, i, j, k - 1, !inside);
                if (crossing(i, j, k, i, j, k + 1, inside)) quad(i - 1, j - 1, k, i, j - 1, k, i, j, k, i - 1, j, k, !inside);
            }
    return mesh;
}

}

TerrainMesh meshChunk(const Terrain& terrain, const ChunkKey& key, bool water) {
    const int size = Terrain::chunk;
    return meshBox(terrain, key.x * size - (key.x == 0 ? 1 : 0), key.y * size - (key.y == 0 ? 1 : 0), key.z * size - (key.z == 0 ? 1 : 0), key.x * size + size, key.y * size + size, key.z * size + size, water);
}

TerrainMesh meshWater(const Terrain& terrain) { return meshBox(terrain, -1, -1, -1, Terrain::cellsX, Terrain::cellsY, Terrain::cellsZ, true); }

}
