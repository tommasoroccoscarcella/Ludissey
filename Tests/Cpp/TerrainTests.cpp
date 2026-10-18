#include "Engine/Backend/ServerState.hpp"
#include "Engine/Character/Character.hpp"
#include "Engine/Rendering/World.hpp"
#include "Engine/Scripting/WorldSession.hpp"
#include "Engine/Studio/StudioDocument.hpp"
#include "Engine/Terrain/Mesher.hpp"
#include "Golden.hpp"

#include <cmath>
#include <functional>
#include <set>

using namespace ld;

namespace {

const TerrainSettings hills{4242, 5, 1, -1};

std::optional<double> column(const Terrain& t, double x, double z) { return t.surface(x, z, 15); }

TerrainBrush brush(double radius, double strength, TerrainMaterial material) {
    TerrainBrush b;
    b.radius = radius;
    b.strength = strength;
    b.material = material;
    b.placement = TerrainPlacement::free;
    return b;
}

TEST_CASE("Acqua: livello regionale e rive", "[terrain][water]") {
    Terrain terrain;
    int i = Terrain::index(1, Terrain::origin.x), j = Terrain::index(-1, Terrain::origin.y), k = Terrain::index(1, Terrain::origin.z);
    auto shore = Terrain::compose(TerrainMaterial::sand, 6, 0);
    terrain.set(i, j, k, shore);
    REQUIRE(terrain.fillWater({-8, -8, -8}, {8, 4, 8}) > 0);
    CHECK(Terrain::material(terrain.cell(i, j, k)) == TerrainMaterial::sand);
    CHECK(Terrain::occupancy(terrain.cell(i, j, k)) == 6);
    CHECK(Terrain::waterLevel(terrain.cell(i, j, k)) == Terrain::levels);
    CHECK(terrain.water(-4, 2, -4));
    CHECK_FALSE(terrain.water(-4, 5, -4));
    CHECK_FALSE(terrain.water(10, 0, 0));
    CHECK_FALSE(terrain.solid(-4, 0, -4));
    CHECK(terrain.fillWater({-8, -8, -8}, {8, 4, 8}) == 0);
    auto round = Terrain::fromJson(terrain.json());
    REQUIRE(round);
    CHECK(*round == terrain);
    REQUIRE(terrain.clearWater({-8, -8, -8}, {0, 8, 8}) > 0);
    CHECK_FALSE(terrain.water(-4, 0, -4));
    CHECK(terrain.water(4, 0, -4));
    CHECK(Terrain::occupancy(terrain.cell(i, j, k)) == 6);
    CHECK(terrain.clearWater({8, 0, 0}, {-8, 4, 8}) == 0);
    CHECK(terrain.fillWater({0, 0, 0}, {INFINITY, 4, 8}) == 0);
    REQUIRE(terrain.clearWater(Terrain::origin, Terrain::origin + Vec3{64, 32, 64}) > 0);
    CHECK(terrain.cell(i, j, k) == shore);
    CHECK(terrain.clearWater(Terrain::origin, Terrain::origin + Vec3{64, 32, 64}) == 0);
}

TEST_CASE("Acqua: riempire non abbassa e svuotare ripristina", "[terrain][water]") {
    Terrain terrain;
    REQUIRE(terrain.fillWater({-8, -8, -8}, {8, 4, 8}) > 0);
    int i = Terrain::index(0, Terrain::origin.x), k = Terrain::index(0, Terrain::origin.z), top = Terrain::index(3, Terrain::origin.y);
    CHECK(terrain.fillWater({-8, -8, -8}, {8, 1, 8}) == 0);
    CHECK(Terrain::waterLevel(terrain.cell(i, top, k)) == Terrain::levels);
    Terrain hills;
    hills.generate(TerrainSettings{31, 6, 0, -16});
    Terrain original = hills;
    REQUIRE(hills.fillWater({-12, -5, -12}, {12, 1.25, 12}) > 0);
    REQUIRE(hills.clearWater({-12, -5, -12}, {12, 1.25, 12}) > 0);
    CHECK(hills == original);
    Terrain empty;
    REQUIRE(empty.fillRegion({-6, -5, -6}, {6, 1.25, 6}, TerrainMaterial::rock) > 0);
    REQUIRE(empty.clearRegion({-6, -5, -6}, {6, 1.25, 6}) > 0);
    CHECK(empty == Terrain{});
}

TEST_CASE("Acqua: nessun vertice sopra il livello", "[terrain][water][mesh]") {
    Terrain hills;
    hills.generate(TerrainSettings{57, 12, 0, 0});
    TerrainMesh mesh = meshWater(hills);
    REQUIRE_FALSE(mesh.empty());
    double highest = -INFINITY;
    for (const auto& p : mesh.positions) highest = std::max(highest, p.y);
    CHECK(highest <= 0.01);
}

TEST_CASE("Acqua: mare oltre il bordo", "[terrain][water][mesh]") {
    Terrain sea;
    REQUIRE(sea.fillWater(Terrain::origin, {-Terrain::origin.x, 0, -Terrain::origin.z}) > 0);
    Experience world = Experience::blank();
    world.blocks.clear();
    world.terrain = sea;
    auto scene = render::worldScene(world);
    auto node = scene->terrain.waterNode();
    REQUIRE(node);
    REQUIRE(node->geometry);
    const auto& g = *node->geometry;
    REQUIRE(g.elements.size() == 2);
    CHECK(g.elements[1].count == 24);
    CHECK(g.materials[1].deep);
    CHECK_FALSE(g.materials[0].deep);
    CHECK_FALSE(node->castsShadow);
    struct Box {
        double x0, z0, x1, z1;
    };
    std::vector<Box> boxes;
    double reach = rules::terrain::water::seaRing;
    for (std::uint32_t r = 0; r < 4; ++r) {
        Box b{INFINITY, INFINITY, -INFINITY, -INFINITY};
        for (std::uint32_t v = 0; v < 6; ++v) {
            std::uint32_t index = g.indices[g.elements[1].start + r * 6 + v];
            double x = g.positions[index * 3], y = g.positions[index * 3 + 1], z = g.positions[index * 3 + 2];
            CHECK(std::abs(y + 0.02) < 0.01);
            b = {std::min(b.x0, x), std::min(b.z0, z), std::max(b.x1, x), std::max(b.z1, z)};
        }
        boxes.push_back(b);
    }
    double area = 0;
    for (std::size_t a = 0; a < boxes.size(); ++a) {
        area += (boxes[a].x1 - boxes[a].x0) * (boxes[a].z1 - boxes[a].z0);
        for (std::size_t b = a + 1; b < boxes.size(); ++b) {
            double w = std::min(boxes[a].x1, boxes[b].x1) - std::max(boxes[a].x0, boxes[b].x0);
            double h = std::min(boxes[a].z1, boxes[b].z1) - std::max(boxes[a].z0, boxes[b].z0);
            CHECK_FALSE((w > 0.001 && h > 0.001));
        }
    }
    double inner = boxes[2].z1 - boxes[2].z0;
    double span = boxes[0].x1 - boxes[0].x0;
    CHECK(std::abs(span - inner - 2 * reach - 2 * Terrain::cellSize) < 0.01);
    CHECK(std::abs(span - 2 * reach + 2 * Terrain::origin.x) < 0.01);
    CHECK(std::abs(area - (span * span - inner * inner)) < 1);
    Experience dry = Experience::blank();
    dry.blocks.clear();
    dry.terrain.generate(TerrainSettings{9, 1.5, 2, -20});
    auto land = render::worldScene(dry);
    CHECK((!land->terrain.waterNode() || land->terrain.waterNode()->geometry->elements.size() == 1));
}

TEST_CASE("Acqua: livello frazionario a passi di 0,25", "[terrain][water]") {
    Terrain terrain;
    REQUIRE(terrain.fillWater({-8, -16, -8}, {8, 1.25, 8}) > 0);
    int i = Terrain::index(0, Terrain::origin.x), k = Terrain::index(0, Terrain::origin.z);
    int top = Terrain::index(1, Terrain::origin.y);
    CHECK(Terrain::waterLevel(terrain.cell(i, top - 1, k)) == Terrain::levels);
    CHECK(Terrain::waterLevel(terrain.cell(i, top, k)) == static_cast<int>(std::round(1.25 / Terrain::cellSize * Terrain::levels)));
    CHECK(Terrain::waterLevel(terrain.cell(i, top + 1, k)) == 0);
    auto region = TerrainRegion::snapped({-7.3, -15.6, -9.1}, {7.7, 1.13, 8.9});
    CHECK(region.minimum.x == -8);
    CHECK(region.maximum.x == 8);
    CHECK(region.minimum.y == -16);
    CHECK(region.maximum.y == 1.25);
    CHECK(region.minimum.z == -10);
    CHECK(region.maximum.z == 8);
    auto whole = TerrainRegion::whole(0);
    CHECK(whole.minimum == Terrain::origin);
    CHECK(whole.maximum.y == 0);
}

TEST_CASE("Terreno: conversione dal formato 1", "[terrain][codec]") {
    std::string runs;
    auto run = [&](int count, int value) { runs += static_cast<char>(count); runs += static_cast<char>(value); };
    run(200, 0x00);
    run(100, 0x2F);
    run(100, 0x53);
    run(112, 0x36);
    Json legacy = Json::object({{"settings", TerrainSettings{3, 4, 1, 0}.json()}, {"chunks", Json::object({{"1,0,2", base64Encode(runs)}})}});
    auto decoded = Terrain::fromJson(legacy);
    REQUIRE(decoded);
    const auto& cells = decoded->data().at(ChunkKey{1, 0, 2});
    CHECK(cells[0] == 0);
    CHECK(cells[200] == Terrain::compose(TerrainMaterial::rock, 15, 0));
    CHECK(cells[300] == Terrain::compose(TerrainMaterial::air, 0, 3));
    CHECK(Terrain::waterLevel(cells[300]) == 3);
    CHECK(Terrain::occupancy(cells[300]) == 0);
    CHECK(cells[400] == Terrain::compose(TerrainMaterial::sand, 6, 0));
    Json converted = decoded->json();
    CHECK(converted["format"] == Json(2));
    auto again = Terrain::fromJson(converted);
    REQUIRE(again);
    CHECK(*again == *decoded);
    Json invalid = Json::object({{"format", 2}, {"chunks", Json::object({{"0,0,0", base64Encode(std::string("\x01\x0F\x00", 3) + std::string(1, static_cast<char>(255)) + std::string("\x00\x00", 2))}})}});
    CHECK_FALSE(Terrain::fromJson(invalid));
    for (std::uint16_t bad : {std::uint16_t{0x0080}, std::uint16_t{0x1000}, std::uint16_t{0x005F}, std::uint16_t{0x0010}, std::uint16_t{0x0001}}) {
        std::vector<std::uint16_t> values(Terrain::volume, bad);
        INFO(bad);
        CHECK_FALSE(Terrain::decodeChunk(Terrain::encodeChunk(values)));
    }
    CHECK(Terrain::decodeChunk(Terrain::encodeChunk(std::vector<std::uint16_t>(Terrain::volume, Terrain::compose(TerrainMaterial::mud, 9, 12)))));
}

TEST_CASE("Acqua: mesh solo fra acqua e aria", "[terrain][water][mesh]") {
    Terrain island;
    island.generate(TerrainSettings{144, 8, 0, 0});
    TerrainMesh mesh = meshWater(island);
    REQUIRE_FALSE(mesh.empty());
    int checked = 0;
    for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
        Vec3 a = mesh.positions[mesh.indices[t]], b = mesh.positions[mesh.indices[t + 1]], c = mesh.positions[mesh.indices[t + 2]];
        Vec3 centre = (a + b + c) * (1.0 / 3);
        Vec3 normal = cross(b - a, c - a).normalized();
        Vec3 outside = centre + normal * 0.4, inside = centre - normal * 0.4;
        double margin = Terrain::cellSize * 0.75;
        bool wall = centre.x < Terrain::origin.x + margin || centre.z < Terrain::origin.z + margin || centre.x > -Terrain::origin.x - margin || centre.z > -Terrain::origin.z - margin || centre.y < Terrain::origin.y + margin;
        if (wall) continue;
        INFO(centre.x << " " << centre.y << " " << centre.z);
        CHECK_FALSE(island.solid(outside.x, outside.y, outside.z));
        CHECK_FALSE(island.water(outside.x, outside.y, outside.z));
        CHECK(island.sample(inside.x, inside.y, inside.z, true) + island.sample(inside.x, inside.y, inside.z) >= 0.45);
        if (island.water(inside.x, inside.y, inside.z)) ++checked;
    }
    CHECK(checked > 100);
}

TEST_CASE("Acqua: nessun quad fra chunk pieni d'acqua", "[terrain][water][mesh]") {
    Terrain sea;
    REQUIRE(sea.fillWater(Terrain::origin, {-Terrain::origin.x, 0, -Terrain::origin.z}) > 0);
    TerrainMesh mesh = meshWater(sea);
    for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
        Vec3 centre = (mesh.positions[mesh.indices[t]] + mesh.positions[mesh.indices[t + 1]] + mesh.positions[mesh.indices[t + 2]]) * (1.0 / 3);
        bool top = std::abs(centre.y) < 0.05;
        double rim = Terrain::cellSize + 0.01;
        bool floor = centre.y < Terrain::origin.y + rim;
        INFO(centre.x << " " << centre.y << " " << centre.z);
        CHECK((top || floor));
    }
    for (int x = 0; x < Terrain::chunksX; ++x)
        for (int z = 0; z < Terrain::chunksZ; ++z) {
            ChunkKey key{x, 0, z};
            TerrainMesh chunk = meshChunk(sea, key, true);
            for (std::size_t t = 0; t + 2 < chunk.indices.size(); t += 3) {
                Vec3 centre = (chunk.positions[chunk.indices[t]] + chunk.positions[chunk.indices[t + 1]] + chunk.positions[chunk.indices[t + 2]]) * (1.0 / 3);
                double seamX = Terrain::origin.x + (x + 1) * Terrain::chunk * Terrain::cellSize, seamZ = Terrain::origin.z + (z + 1) * Terrain::chunk * Terrain::cellSize;
                Vec3 n = chunk.normals[chunk.indices[t]];
                bool facingX = std::abs(n.x) > 0.5 && x + 1 < Terrain::chunksX && std::abs(centre.x - seamX) < 0.5;
                bool facingZ = std::abs(n.z) > 0.5 && z + 1 < Terrain::chunksZ && std::abs(centre.z - seamZ) < 0.5;
                CHECK_FALSE((facingX || facingZ));
            }
        }
}

TEST_CASE("Acqua: superficie del lago connessa", "[terrain][water][mesh]") {
    Terrain lake;
    for (int i = 0; i < Terrain::cellsX; ++i)
        for (int k = 0; k < Terrain::cellsZ; ++k) {
            Vec3 c = Terrain::center(i, 0, k);
            double radius = std::hypot(c.x, c.z);
            double height = radius < 18 ? -6 + radius * 0.2 : 4;
            for (int j = 0; j < Terrain::cellsY; ++j) {
                double bottom = Terrain::origin.y + j * Terrain::cellSize;
                int level = static_cast<int>(std::round(std::clamp((height - bottom) / Terrain::cellSize, 0.0, 1.0) * Terrain::levels));
                lake.set(i, j, k, Terrain::compose(TerrainMaterial::sand, level, 0));
            }
        }
    REQUIRE(lake.fillWater({-20, -16, -20}, {20, 0, 20}) > 0);
    TerrainMesh mesh = meshWater(lake);
    std::vector<int> parent(mesh.positions.size());
    for (std::size_t i = 0; i < parent.size(); ++i) parent[i] = static_cast<int>(i);
    std::function<int(int)> root = [&](int v) { return parent[static_cast<std::size_t>(v)] == v ? v : parent[static_cast<std::size_t>(v)] = root(parent[static_cast<std::size_t>(v)]); };
    std::set<int> used;
    for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
        bool up = true;
        for (int corner = 0; corner < 3; ++corner) up = up && mesh.normals[mesh.indices[t + static_cast<std::size_t>(corner)]].y > 0.5;
        if (!up) continue;
        int a = root(static_cast<int>(mesh.indices[t]));
        for (int corner = 1; corner < 3; ++corner) parent[static_cast<std::size_t>(root(static_cast<int>(mesh.indices[t + static_cast<std::size_t>(corner)])))] = a;
        for (int corner = 0; corner < 3; ++corner) used.insert(static_cast<int>(mesh.indices[t + static_cast<std::size_t>(corner)]));
    }
    REQUIRE(used.size() > 10);
    std::set<int> components;
    for (int v : used) components.insert(root(v));
    CHECK(components.size() == 1);
}

TEST_CASE("Terreno: generato con il mare entra nel salvataggio", "[terrain][water][codec]") {
    for (int seed : {1, 7, 42}) {
        Terrain terrain;
        terrain.generate(TerrainSettings{seed, 12, 0, 0});
        terrain.fillWater(Terrain::origin, {-Terrain::origin.x, 4, -Terrain::origin.z});
        INFO(seed);
        CHECK(terrain.json().dump().size() <= static_cast<std::size_t>(rules::terrain::keys * rules::terrain::keyCharacters));
    }
}

TEST_CASE("Terreno: generate non tocca il solido sotto l'acqua", "[terrain][water]") {
    TerrainSettings settings{42, 12, 0, 1.5};
    Terrain dry, wet;
    dry.generate({settings.seed, settings.amplitude, settings.baseHeight, -100});
    wet.generate(settings);
    int submerged = 0;
    for (int i = 0; i < Terrain::cellsX; ++i)
        for (int j = 0; j < Terrain::cellsY; ++j)
            for (int k = 0; k < Terrain::cellsZ; ++k) {
                auto a = dry.cell(i, j, k), b = wet.cell(i, j, k);
                CHECK(Terrain::occupancy(a) == Terrain::occupancy(b));
                if (Terrain::waterLevel(b) > 0 && Terrain::occupancy(b) > 0) ++submerged;
            }
    CHECK(submerged > 0);
}

Terrain field(const std::function<double(double, double)>& height) {
    Terrain t;
    for (int i = 0; i < Terrain::cellsX; ++i)
        for (int k = 0; k < Terrain::cellsZ; ++k) {
            Vec3 c = Terrain::center(i, 0, k);
            double top = height(c.x, c.z);
            for (int j = 0; j < Terrain::cellsY; ++j) {
                double bottom = Terrain::origin.y + j * Terrain::cellSize;
                double fill = std::clamp((top - bottom) / Terrain::cellSize, 0.0, 1.0);
                int level = static_cast<int>(std::round(fill * Terrain::levels));
                if (level > 0) t.set(i, j, k, Terrain::compose(TerrainMaterial::grass, level, 0));
            }
        }
    return t;
}

struct Walk {
    std::vector<CharacterState> states;
    int airborne() const {
        int n = 0;
        for (std::size_t i = 1; i < states.size(); ++i) n += !states[i].grounded;
        return n;
    }
    double jitter(double low = -1e18, double high = 1e18) const {
        double worst = 0;
        for (std::size_t i = 2; i < states.size(); ++i) {
            bool within = true;
            for (std::size_t j = i - 2; j <= i; ++j) within = within && states[j].x >= low && states[j].x <= high;
            if (within) worst = std::max(worst, std::abs(states[i].y - 2 * states[i - 1].y + states[i - 2].y));
        }
        return worst;
    }
};

CharacterInput press(double forward, double strafe) {
    CharacterInput i;
    i.forward = forward;
    i.strafe = strafe;
    return i;
}

Walk walk(const Terrain& t, const std::vector<WorldBlock>& blocks, CharacterState start, CharacterInput input, double yaw, double seconds, double hertz) {
    Walk w{{start}};
    for (int i = 0; i < static_cast<int>(std::round(seconds * hertz)); ++i) {
        start = motion::step(start, input, yaw, blocks, t, 1 / hertz);
        w.states.push_back(start);
    }
    return w;
}

}

TEST_CASE("Terreno: generazione, pennelli e codec", "[terrain]") {
    Terrain first, second, other;
    first.generate(hills);
    second.generate(hills);
    TerrainSettings changed = hills;
    changed.seed = 4243;
    other.generate(changed);
    CHECK(first == second);
    CHECK_FALSE(first.empty());
    CHECK(first.json().dump(true) == second.json().dump(true));
    CHECK_FALSE(first == other);
    CHECK(first.settings == std::optional<TerrainSettings>(hills));
    std::set<TerrainMaterial> materials;
    for (int i = 0; i < Terrain::cellsX; ++i)
        for (int j = 0; j < Terrain::cellsY; ++j)
            for (int k = 0; k < Terrain::cellsZ; ++k) materials.insert(Terrain::material(first.cell(i, j, k)));
    CHECK(materials.count(TerrainMaterial::grass));
    CHECK(materials.count(TerrainMaterial::rock));
    std::set<long> heights;
    int found = 0;
    for (auto [x, z] : std::vector<std::pair<double, double>>{{-20, -20}, {0, 0}, {18, -6}, {-9, 14}})
        if (auto h = column(first, x, z)) {
            ++found;
            heights.insert(std::lround(*h * 10));
        }
    CHECK(found == 4);
    CHECK(heights.size() > 1);
    Terrain flooded;
    flooded.generate(TerrainSettings{9, 4, -3, 2});
    bool water = false;
    for (int i = 0; i < Terrain::cellsX && !water; ++i)
        for (int k = 0; k < Terrain::cellsZ && !water; ++k) water = Terrain::waterLevel(flooded.cell(i, Terrain::index(1, Terrain::origin.y), k)) > 0;
    CHECK(water);

    Terrain terrain;
    auto rock = brush(4, 1, TerrainMaterial::rock);
    CHECK(terrain.apply(TerrainTool::add, {0, 0, 0}, rock) > 0);
    CHECK(terrain.solid(0, 0, 0));
    int ci = Terrain::index(0, Terrain::origin.x), cj = Terrain::index(0, Terrain::origin.y), ck = Terrain::index(0, Terrain::origin.z);
    CHECK(Terrain::material(terrain.cell(ci, cj, ck)) == TerrainMaterial::rock);
    Terrain painted = terrain;
    painted.apply(TerrainTool::paint, {0, 0, 0}, brush(4, 1, TerrainMaterial::sand));
    CHECK(Terrain::material(painted.cell(ci, cj, ck)) == TerrainMaterial::sand);
    CHECK(Terrain::occupancy(painted.cell(ci, cj, ck)) == Terrain::occupancy(terrain.cell(ci, cj, ck)));
    CHECK(painted.apply(TerrainTool::paint, {20, 10, 20}, brush(2, 1, TerrainMaterial::sand)) == 0);
    Terrain carved = terrain;
    for (int i = 0; i < 3; ++i) carved.apply(TerrainTool::subtract, {0, 0, 0}, rock);
    CHECK_FALSE(carved.solid(0, 0, 0));
    CHECK(carved.empty());
    Terrain spike;
    Vec3 middle = Terrain::center(ci, cj, ck);
    spike.apply(TerrainTool::add, middle, brush(1, 1, TerrainMaterial::rock));
    double peak = spike.solidDensity(ci, cj, ck);
    spike.apply(TerrainTool::smooth, middle, rock);
    CHECK(spike.solidDensity(ci, cj, ck) < peak);
    CHECK(spike.solidDensity(ci + 1, cj, ck) > 0);
    Terrain bumpy;
    bumpy.generate(hills);
    for (double x = -6; x <= 6; x += 3)
        for (double z = -6; z <= 6; z += 3)
            for (int i = 0; i < 4; ++i) bumpy.apply(TerrainTool::flatten, {x, 2, z}, brush(5, 1, TerrainMaterial::grass));
    for (auto [x, z] : std::vector<std::pair<double, double>>{{-2, -2}, {0, 0}, {2, 1}, {-1, 2}}) {
        auto h = column(bumpy, x, z);
        REQUIRE(h);
        CHECK(std::abs(*h - 2) < 0.6);
    }
    Terrain wet;
    wet.apply(TerrainTool::add, {0, -4, 0}, brush(3, 1, TerrainMaterial::rock));
    auto before = wet.cell(ci, Terrain::index(-4, Terrain::origin.y), ck);
    wet.apply(TerrainTool::water, {0, -1, 0}, brush(5, 1, TerrainMaterial::grass));
    CHECK(wet.water(0, 1, 0));
    CHECK_FALSE(wet.solid(0, 1, 0));
    CHECK(Terrain::occupancy(wet.cell(ci, Terrain::index(-4, Terrain::origin.y), ck)) == Terrain::occupancy(before));
    CHECK(Terrain::material(wet.cell(ci, Terrain::index(-4, Terrain::origin.y), ck)) == Terrain::material(before));
    CHECK(terrain.apply(TerrainTool::add, {500, 0, 0}, rock) == 0);

    Terrain coded;
    coded.generate(hills);
    auto back = Terrain::fromJson(coded.json());
    REQUIRE(back);
    CHECK(*back == coded);
    auto rejects = [](const std::string& text) {
        auto parsed = Json::parse(text);
        return !parsed || !Terrain::fromJson(*parsed);
    };
    CHECK(rejects(R"({"chunks":{"0,0,0":"@@@@"}})"));
    CHECK(rejects(R"({"chunks":{"0,0,0":")" + base64Encode(std::string("\x0a\x11", 2)) + R"("}})"));
    CHECK(rejects(R"({"chunks":{"0,0,0":")" + base64Encode(std::string("\xff\xf1\xff\xf1\x02\xf1", 6)) + R"("}})"));
    CHECK(rejects(R"({"chunks":{"0,0,0":")" + base64Encode(std::string("\xff\x10\xff\x10\x02\x10", 6)) + R"("}})"));
    CHECK(rejects(R"({"format":2,"chunks":{"9,0,0":")" + Terrain::encodeChunk(std::vector<std::uint16_t>(Terrain::volume, Terrain::compose(TerrainMaterial::grass, 15, 0))) + R"("}})"));
    CHECK(rejects(R"({"format":3,"chunks":{}})"));
    CHECK(rejects(R"({"chunks":{"0,0,0":"AAAA"}})"));
    Experience world = Experience::blank();
    world.terrain = coded;
    std::string encoded = world.json().dump();
    auto at = encoded.find("\"chunks\":{\"");
    REQUIRE(at != std::string::npos);
    encoded.insert(at + std::string("\"chunks\":{\"").size(), "X");
    CHECK_FALSE(Experience::parse(encoded));
}

TEST_CASE("Terreno: collisioni e nuoto", "[terrain][motion]") {
    golden::registry();
    Terrain terrain;
    terrain.generate(TerrainSettings{77, 0, 2, -20});
    auto ground = column(terrain, 3, 3);
    REQUIRE(ground);
    CHECK(std::abs(*ground - 2) < 0.3);
    CharacterState state;
    state.x = 3; state.y = 8; state.z = 3;
    for (int i = 0; i < 180; ++i) state = motion::step(state, CharacterInput{}, 0, {}, terrain, 1.0 / 60);
    CHECK(state.grounded);
    CHECK(std::abs(state.y - *ground) < 0.15);
    Terrain ramp = terrain;
    int index = 0;
    for (double x = 0; x <= 14; x += 2, ++index) ramp.apply(TerrainTool::add, {x, 2 + index * 0.35, 0}, brush(2.5, 1, TerrainMaterial::grass));
    CharacterState climber;
    climber.x = -4; climber.y = *ground;
    for (int i = 0; i < 150; ++i) climber = motion::step(climber, press(0, 1), 0, {}, ramp, 1.0 / 60);
    CHECK(climber.x > 4);
    CHECK(climber.y > *ground + 0.8);
    CHECK_FALSE(ramp.solid(climber.x, climber.y + 0.3, climber.z));
    Terrain wall = terrain;
    for (double y = 2; y <= 12; y += 2) wall.apply(TerrainTool::add, {6, y, 0}, brush(2, 1, TerrainMaterial::rock));
    CharacterState runner;
    runner.y = *ground;
    for (int i = 0; i < 180; ++i) runner = motion::step(runner, press(0, 1), 0, {}, wall, 1.0 / 60);
    CHECK(runner.x < 5);
    CHECK_FALSE(wall.solid(runner.x, runner.y + 1, runner.z));
    CharacterState buried;
    buried.x = 3; buried.y = *ground - 1.2; buried.z = 3;
    buried = motion::step(buried, CharacterInput{}, 0, {}, terrain, 1.0 / 60);
    CHECK(buried.y >= *ground - 0.1);
    CHECK(std::abs(motion::spawn({}, terrain, 3, 3).y - *ground) < 0.2);
    CHECK(camera_rig::clearance({3, *ground + 1, 3}, 0, -1.2, 20, {}, terrain) < 3);
    Terrain lake = terrain;
    for (double x = -10; x <= 10; x += 3)
        for (double z = -10; z <= 10; z += 3) lake.apply(TerrainTool::subtract, {x, 1, z}, brush(3, 1, TerrainMaterial::grass));
    for (double x = -10; x <= 10; x += 3)
        for (double z = -10; z <= 10; z += 3) lake.apply(TerrainTool::water, {x, 0, z}, brush(3, 1, TerrainMaterial::grass));
    CharacterState swimmer;
    for (int i = 0; i < 40; ++i) swimmer = motion::step(swimmer, CharacterInput{}, 0, {}, lake, 1.0 / 60);
    CHECK(swimmer.swimming);
    CharacterState floater;
    floater.y = -1.5;
    double low = 1e9, high = -1e9;
    for (int frame = 0; frame < 600; ++frame) {
        floater = motion::step(floater, CharacterInput{}, 0, {}, lake, 1.0 / 60);
        if (frame >= 300) {
            low = std::min(low, floater.y);
            high = std::max(high, floater.y);
        }
    }
    CHECK(floater.swimming);
    CHECK_FALSE(lake.water(floater.x, floater.y + motion::height, floater.z));
    CHECK(high - low < 0.3);
    CHECK_FALSE(lake.solid(0, 0, 0));
    CHECK(lake.water(0, 0, 0));
    WorldBlock loose;
    loose.id = makeUuid();
    loose.x = 3; loose.z = 3; loose.y = 10;
    loose.anchored = false;
    Experience physical;
    physical.id = makeUuid();
    physical.blocks = {loose};
    physical.terrain = terrain;
    WorldSession session(physical, WorldSession::Mode::run, "Player", 0, false, [](const OutputLine&) {});
    session.start({0, 0, 0});
    for (int i = 0; i < 240; ++i) session.advance(1.0 / 60);
    auto parts = session.parts();
    REQUIRE_FALSE(parts.empty());
    CHECK(std::abs(parts.front().y - (*ground + 0.5)) < 0.2);
}

TEST_CASE("Terreno: pendenze e gradini", "[terrain][motion]") {
    auto east = press(0, 1), west = press(0, -1);
    double t30 = std::tan(30 * pi / 180), t40 = std::tan(40 * pi / 180), t72 = std::tan(72 * pi / 180);
    Terrain gentle = field([&](double x, double) { return 1 + std::clamp(x, 0.0, 12.0) * t30; });
    auto foot = motion::spawn({}, gentle, -6, 0);
    auto up = walk(gentle, {}, foot, east, 0, 4, 60);
    auto summit = up.states.back();
    auto down = walk(gentle, {}, summit, west, 0, 4, 60);
    CHECK(summit.x > 6);
    CHECK(summit.y - foot.y > 3);
    CHECK(up.airborne() == 0);
    CHECK(down.airborne() == 0);
    CHECK(std::max(up.jitter(2, 10), down.jitter(2, 10)) < 0.03);
    int rising = 0, falling = 0;
    for (std::size_t i = 1; i < up.states.size(); ++i)
        if (up.states[i].x >= 2 && up.states[i].x <= 10 && up.states[i].y < up.states[i - 1].y - 1e-6) ++rising;
    for (std::size_t i = 1; i < down.states.size(); ++i)
        if (down.states[i].x >= 2 && down.states[i].x <= 10 && down.states[i].y > down.states[i - 1].y + 1e-6) ++falling;
    CHECK(rising == 0);
    CHECK(falling == 0);
    CHECK(summit.y - down.states.back().y > 3);
    for (const auto& s : down.states) CHECK_FALSE(gentle.solid(s.x, s.y + 0.3, s.z));
    Terrain steep = field([&](double x, double) { return 1 + std::clamp(x + 4, 0.0, 12.0) * t40; });
    auto climber = motion::spawn({}, steep, -6, 0);
    double worstGap = 0;
    int airborne = 0;
    for (int i = 0; i < 240; ++i) {
        climber = motion::step(climber, east, 0, {}, steep, 1.0 / 60);
        double reach = motion::radius, lowest = 1e9, highest = -1e9;
        bool any = false;
        for (auto [dx, dz] : std::vector<std::pair<double, double>>{{0, 0}, {reach, 0}, {-reach, 0}, {0, reach}, {0, -reach}})
            if (auto h = steep.surface(climber.x + dx, climber.z + dz, 30)) {
                any = true;
                lowest = std::min(lowest, *h);
                highest = std::max(highest, *h);
            }
        if (!any) lowest = highest = climber.y;
        worstGap = std::max(worstGap, climber.y < lowest ? lowest - climber.y : climber.y > highest ? climber.y - highest : 0.0);
        if (!climber.grounded) ++airborne;
    }
    CHECK(worstGap < 0.02);
    CHECK(airborne == 0);
    Terrain cliff = field([&](double x, double) { return 1 + std::clamp(x, 0.0, 6.0) * t72; });
    auto attempt = walk(cliff, {}, motion::spawn({}, cliff, -4, 0), east, 0, 3, 60);
    auto blocked = attempt.states.back(), settled = attempt.states[attempt.states.size() - 61];
    double face = cliff.surface(1, 0, 30).value_or(0);
    CHECK(blocked.x < 1);
    CHECK(blocked.y < face);
    CHECK(std::abs(blocked.x - settled.x) < 0.01);
    std::vector<CharacterState> finals;
    for (double hertz : {30.0, 60.0, 120.0}) finals.push_back(walk(gentle, {}, foot, press(1, 1), 0.4, 2, hertz).states.back());
    double spread = 0;
    for (const auto& a : finals)
        for (const auto& b : finals) spread = std::max(spread, (a.position() - b.position()).length());
    CHECK(spread < 0.05);
    WorldBlock floor, low, high;
    floor.sizeX = 40; floor.sizeZ = 40; floor.sizeY = 1; floor.y = -0.5;
    low.x = 3; low.sizeX = 2; low.sizeZ = 4; low.sizeY = 0.3; low.y = 0.15;
    high.x = 3; high.sizeX = 2; high.sizeZ = 4; high.sizeY = 0.6; high.y = 0.3;
    Terrain empty;
    auto stepped = walk(empty, {floor, low}, CharacterState{}, east, 0, 1, 60).states.back();
    auto stopped = walk(empty, {floor, high}, CharacterState{}, east, 0, 1, 60).states.back();
    CHECK(stepped.x > 3);
    CHECK(std::abs(stepped.y - 0.3) < 0.01);
    CHECK(stepped.grounded);
    CHECK(stopped.x < 2);
    CHECK(stopped.y < 0.01);
    Terrain generated;
    generated.generate(hills);
    int toggles = 0;
    double worst = 0;
    for (double angle = 0; angle < 2 * pi - 1e-9; angle += pi / 4) {
        auto run = walk(generated, {}, motion::spawn({}, generated, 0, 0), press(1, 0), angle, 3, 60);
        for (std::size_t i = 1; i < run.states.size(); ++i) toggles += run.states[i].grounded != run.states[i - 1].grounded;
        worst = std::max(worst, run.jitter());
    }
    CHECK(toggles <= 2);
    CHECK(worst < 0.03);
}

TEST_CASE("Terreno: mesh incrementale", "[terrain][rendering]") {
    Terrain terrain;
    terrain.generate(hills);
    render::TerrainView view;
    view.sync(terrain);
    int initial = view.rebuilt;
    CHECK(initial > 0);
    CHECK(view.chunkNodes() > 0);
    CHECK(view.triangles > 500);
    view.sync(terrain);
    CHECK(view.rebuilt == initial);
    terrain.apply(TerrainTool::add, {-26, 2, -26}, brush(2, 1, TerrainMaterial::rock));
    view.sync(terrain);
    int delta = view.rebuilt - initial;
    CHECK(delta > 0);
    CHECK(delta <= 8);
    CHECK(delta < initial);
    Experience world = Experience::blank();
    world.terrain = terrain;
    auto scene = render::worldScene(world);
    render::TerrainView studio;
    studio.sync(terrain);
    CHECK(scene->terrain.triangles == studio.triangles);

    Terrain seams;
    seams.generate(hills);
    struct Point {
        long x, y, z;
        auto operator<=>(const Point&) const = default;
    };
    std::map<std::array<Point, 2>, std::pair<int, std::set<ChunkKey>>> edges;
    std::map<std::array<Point, 3>, int> faces;
    double span = Terrain::cellSize * Terrain::cellsX / 2.0 - Terrain::cellSize * 1.5;
    for (int i = 0; i < Terrain::chunksX; ++i)
        for (int j = 0; j < Terrain::chunksY; ++j)
            for (int k = 0; k < Terrain::chunksZ; ++k) {
                ChunkKey key{i, j, k};
                auto mesh = meshChunk(seams, key, false);
                auto point = [&](std::uint32_t index) {
                    Vec3 v = mesh.positions[index];
                    return Point{std::lround(v.x * 1000), std::lround(v.y * 1000), std::lround(v.z * 1000)};
                };
                for (std::size_t s = 0; s + 2 < mesh.indices.size(); s += 3) {
                    std::array<Point, 3> corners{point(mesh.indices[s]), point(mesh.indices[s + 1]), point(mesh.indices[s + 2])};
                    auto sorted = corners;
                    std::sort(sorted.begin(), sorted.end());
                    ++faces[sorted];
                    for (auto [a, b] : std::vector<std::pair<int, int>>{{0, 1}, {1, 2}, {2, 0}}) {
                        std::array<Point, 2> edge{corners[a], corners[b]};
                        std::sort(edge.begin(), edge.end());
                        auto& entry = edges[edge];
                        ++entry.first;
                        entry.second.insert(key);
                    }
                }
            }
    int open = 0, overlapping = 0, crossing = 0;
    for (const auto& [edge, entry] : edges) {
        bool interior = std::all_of(edge.begin(), edge.end(), [&](const Point& p) { return std::abs(p.x / 1000.0) < span && std::abs(p.z / 1000.0) < span; });
        if (!interior) continue;
        if (entry.first == 1) ++open;
        if (entry.second.size() > 1) ++crossing;
    }
    for (const auto& [face, count] : faces) overlapping += count > 1;
    CHECK(open == 0);
    CHECK(overlapping == 0);
    CHECK(crossing > 100);
}

TEST_CASE("Terreno: comandi annullabili in Studio", "[terrain][studio]") {
    golden::registry();
    StudioDocument document;
    REQUIRE(document.open(Experience::blank()));
    document.perform(StudioEdit::generate(hills));
    Terrain generated = document.world.terrain;
    CHECK(document.undoLabel() == std::optional<std::string>("Genera terreno"));
    document.terrainBrush = brush(3, 1, TerrainMaterial::mud);
    int stroke = document.beginStroke();
    for (double x = -6; x <= 6; x += 2) document.perform(StudioEdit::paint(TerrainTool::add, {x, 4, 0}, stroke));
    document.endStroke();
    Terrain added = document.world.terrain;
    CHECK_FALSE(added == generated);
    CHECK(document.undoLabel() == std::optional<std::string>("Terreno · Aggiungi"));
    int paint = document.beginStroke();
    document.terrainBrush.material = TerrainMaterial::sand;
    document.perform(StudioEdit::paint(TerrainTool::paint, {0, 4, 0}, paint));
    document.endStroke();
    int dug = document.beginStroke();
    document.perform(StudioEdit::paint(TerrainTool::subtract, {8, 2, 8}, dug));
    document.endStroke();
    Terrain final = document.world.terrain;
    document.undo();
    document.undo();
    CHECK(document.world.terrain == added);
    document.undo();
    CHECK(document.world.terrain == generated);
    document.redo();
    document.redo();
    document.redo();
    CHECK(document.world.terrain == final);
    document.play();
    CHECK(document.perform(StudioEdit::paint(TerrainTool::add, {0, 4, 0}, document.beginStroke())) == 0);
    document.stop();
}

TEST_CASE("Terreno: validazione nel backend", "[terrain][native-server]") {
    golden::registry();
    ServerState server;
    server.enroll("Pterra", "Geologa", Json::object({{"username", "Geologa"}, {"gender", "female"}, {"createdAt", "2026-09-21T14:13:20.000Z"}}));
    Experience world = Experience::blank();
    world.terrain.generate(TerrainSettings{31, 4, -2, 1});
    Json corrupt = world.json();
    corrupt.set("terrain", Json::object({{"chunks", Json::object({{"0,0,0", "AAAA"}})}}));
    CHECK_THROWS(server.invoke("Pterra", "LudisseySaveWorld", Json::object({{"world", corrupt}})));
    Json oversized = world.json();
    std::string full = Terrain::encodeChunk(std::vector<std::uint16_t>(Terrain::volume, Terrain::compose(TerrainMaterial::rock, 1, 0)));
    oversized.set("terrain", Json::object({{"format", 2}, {"chunks", Json::object({{"0,0,0", full}, {"5,0,0", full}})}}));
    CHECK_THROWS(server.invoke("Pterra", "LudisseySaveWorld", Json::object({{"world", oversized}})));
    std::string legacyId = "cafecafe-cafe-4afe-8afe-cafecafecafe";
    std::string legacy = R"({"id":")" + legacyId + R"(","schemaVersion":3,"title":"Lago","genre":"Altro","serverSize":4,"owner":"Pterra","author":"Geologa","blocks":[{"id":"11111111-1111-4111-8111-111111111111","kind":"cube","color":"3E7FD6","x":0,"y":1,"z":0,"size":8,"sizeY":2,"material":"water","canCollide":false}]})";
    auto& stored = server.state["players"]["Pterra"]["readOnly"];
    stored.set("LudisseyWorlds", Json::array({legacyId}).dump());
    stored.set("World_" + legacyId, legacy);
    Json listed = server.invoke("Pterra", "LudisseyWorlds", Json::object());
    std::optional<Experience> migrated;
    for (const auto& w : listed["worlds"].items())
        if (w["id"] == Json(legacyId)) migrated = *Experience::fromJson(w);
    REQUIRE(migrated);
    CHECK(std::none_of(migrated->blocks.begin(), migrated->blocks.end(), [](const WorldBlock& b) { return b.material == "water"; }));
    CHECK(migrated->terrain.water(0, 1, 0));
}

TEST_CASE("Terreno: progetto schema 5 convertito", "[terrain][native-server][migration]") {
    golden::registry();
    ServerState server;
    server.enroll("Pterra", "Geologa", Json::object({{"username", "Geologa"}, {"gender", "female"}, {"createdAt", "2026-09-21T14:13:20.000Z"}}));
    std::string id = "beadbead-bead-4ead-8ead-beadbeadbead";
    std::string runs;
    auto run = [&](int count, int value) { runs += static_cast<char>(count); runs += static_cast<char>(value); };
    run(255, 0x2F);
    run(1, 0x2F);
    run(255, 0x5F);
    run(1, 0x5F);
    std::string ground = R"({"settings":null,"chunks":{"1,0,1":")" + base64Encode(runs) + R"("}})";
    std::string scripts = R"J([{"id":"a1b2c3d4-e5f6-4a7b-8c9d-0e1f2a3b4c5d","className":"Script","name":"Saluto","parent":"ServerScriptService","properties":{"Disabled":false,"Source":{"String":"print(\"ciao\")"}}}])J";
    std::string legacy = R"({"id":")" + id + R"(","schemaVersion":5,"title":"Lago vecchio","genre":"Altro","serverSize":4,"owner":"Pterra","author":"Geologa","terrainParts":1,"instanceParts":1,"blocks":[]})";
    auto& stored = server.state["players"]["Pterra"]["readOnly"];
    stored.set("LudisseyWorlds", Json::array({id}).dump());
    stored.set("World_" + id, legacy);
    stored.set("Terrain_" + id + "_0", Json(ground).dump());
    stored.set("Instances_" + id + "_0", Json(scripts).dump());
    Json listed = server.invoke("Pterra", "LudisseyWorlds", Json::object());
    std::optional<Experience> migrated;
    for (const auto& w : listed["worlds"].items())
        if (w["id"] == Json(id)) {
            CHECK(w["schemaVersion"] == Json(rules::schemaVersion));
            CHECK(w["terrain"]["format"] == Json(rules::terrain::format));
            migrated = *Experience::fromJson(w);
        }
    REQUIRE(migrated);
    CHECK(migrated->legacyScripts().empty());
    REQUIRE(migrated->record("a1b2c3d4-e5f6-4a7b-8c9d-0e1f2a3b4c5d"));
    CHECK(migrated->record("a1b2c3d4-e5f6-4a7b-8c9d-0e1f2a3b4c5d")->properties["Disabled"] == PropertyValue::boolean(false));
    int i = Terrain::chunk + 1, k = Terrain::chunk + 1;
    CHECK(Terrain::material(migrated->terrain.cell(i, 0, k)) == TerrainMaterial::rock);
    int wet = Terrain::chunk + 5;
    CHECK(Terrain::waterLevel(migrated->terrain.cell(wet, 4, k)) == Terrain::levels);
    CHECK(Terrain::occupancy(migrated->terrain.cell(wet, 4, k)) == 0);
}

TEST_CASE("Terreno: pennello su superficie e nel punto", "[terrain][placement]") {
    TerrainBrush brush;
    brush.radius = 6;
    brush.strength = 0.5;
    brush.placement = TerrainPlacement::surface;
    Vec3 aim{0.3, 0.5, -0.4};
    auto inside = [&](const Terrain& t, double reach) {
        std::vector<double> heights;
        Vec3 c = Terrain::snapped(aim);
        for (int i = 0; i < Terrain::cellsX; ++i)
            for (int k = 0; k < Terrain::cellsZ; ++k) {
                Vec3 p = Terrain::center(i, 0, k);
                if (std::hypot(p.x - c.x, p.z - c.z) > reach) continue;
                auto h = t.surfaceHeight(i, k);
                heights.push_back(h ? *h : -1000);
            }
        return heights;
    };
    Terrain flat;
    REQUIRE(flat.apply(TerrainTool::add, aim, brush) > 0);
    auto layer = inside(flat, brush.radius);
    REQUIRE(layer.size() > 20);
    double top = layer.front();
    for (double h : layer) CHECK(h == Catch::Approx(top));
    CHECK(top == Catch::Approx(aim.y + brush.strength * Terrain::cellSize).margin(Terrain::cellSize / Terrain::levels));
    int outside = 0;
    for (int i = 0; i < Terrain::cellsX; ++i)
        for (int k = 0; k < Terrain::cellsZ; ++k) {
            Vec3 p = Terrain::center(i, 0, k);
            Vec3 c = Terrain::snapped(aim);
            if (std::hypot(p.x - c.x, p.z - c.z) > brush.radius + 0.01 && flat.surfaceHeight(i, k)) ++outside;
        }
    CHECK(outside == 0);
    Terrain shifted;
    shifted.apply(TerrainTool::add, {0.9, 0.5, -0.1}, brush);
    CHECK(shifted == flat);
    Terrain grown = flat;
    grown.apply(TerrainTool::add, {aim.x, top, aim.z}, brush);
    for (double h : inside(grown, brush.radius)) CHECK(h == Catch::Approx(top + brush.strength * Terrain::cellSize).margin(Terrain::cellSize / Terrain::levels));
    Terrain dug = grown;
    double high = inside(grown, brush.radius).front();
    brush.radius = 3;
    dug.apply(TerrainTool::subtract, {aim.x, high, aim.z}, brush);
    auto pit = inside(dug, 3);
    for (double h : pit) CHECK(h == Catch::Approx(high - brush.strength * Terrain::cellSize).margin(Terrain::cellSize / Terrain::levels));
    brush.radius = 6;
    Terrain swollen = flat;
    swollen.apply(TerrainTool::raise, {aim.x, top, aim.z}, brush);
    Vec3 c = Terrain::snapped(aim);
    int ci = Terrain::index(c.x, Terrain::origin.x), ck = Terrain::index(c.z, Terrain::origin.z);
    double middle = *swollen.surfaceHeight(ci, ck), rim = *swollen.surfaceHeight(ci + 2, ck);
    CHECK(middle > rim);
    CHECK(middle > top);
    for (double h : inside(swollen, brush.radius)) CHECK(h >= top - 1e-9);
    brush.placement = TerrainPlacement::free;
    Terrain sphere;
    sphere.apply(TerrainTool::add, aim, brush);
    auto blob = inside(sphere, 4);
    double highest = *std::max_element(blob.begin(), blob.end()), lowest = *std::min_element(blob.begin(), blob.end());
    CHECK(highest - lowest > Terrain::cellSize / 2);
}
