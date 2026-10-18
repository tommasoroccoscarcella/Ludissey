#include "Engine/Avatar/Avatar.hpp"
#include "Engine/Character/Character.hpp"
#include "Engine/Core/Rotation.hpp"
#include "Engine/Experience/Experience.hpp"
#include "Engine/Studio/TransformMath.hpp"
#include "Engine/Terrain/Mesher.hpp"
#include "Golden.hpp"

#include <algorithm>

using namespace ld;

namespace {

Json strings(const std::vector<std::string>& values) {
    Json out = Json::array();
    for (const auto& v : values) out.push(v);
    return out;
}

template <typename Set>
Json sortedStrings(const Set& values) {
    std::vector<std::string> list(values.begin(), values.end());
    std::sort(list.begin(), list.end());
    return strings(list);
}

WorldBlock blockFrom(const Json& value) {
    auto block = WorldBlock::fromJson(value);
    REQUIRE(block.ok());
    return *block;
}

Json blockList(const BlockSet& set) {
    Json out = Json::array();
    for (const auto& [id, block] : set) out.push(block.json());
    return out;
}

BlockSet blockSet(const Json& list) {
    BlockSet set;
    for (const auto& item : list.items()) {
        WorldBlock block = blockFrom(item);
        set[block.id] = block;
    }
    return set;
}

std::vector<WorldBlock> blankBlocks() {
    Experience flat = Experience::blank();
    return flat.blocks;
}

struct GoldenWorld {
    std::string name;
    std::vector<WorldBlock> blocks;
    Terrain terrain;
};

std::vector<GoldenWorld> worlds() {
    std::vector<WorldBlock> flat = blankBlocks();
    flat[0].sizeZ = 120;
    std::vector<WorldBlock> stairs = flat;
    for (int step = 0; step < 6; ++step) {
        WorldBlock b;
        b.id = "step" + std::to_string(step);
        b.sizeX = 6;
        b.sizeY = 0.3 * (step + 1);
        b.sizeZ = 1;
        b.z = -static_cast<double>(step) - 3;
        b.y = b.sizeY / 2;
        stairs.push_back(b);
    }
    WorldBlock wall;
    wall.id = "wall";
    wall.sizeX = 10;
    wall.sizeY = 4;
    wall.sizeZ = 1;
    wall.z = -14;
    wall.y = 2;
    stairs.push_back(wall);
    WorldBlock tilted;
    tilted.id = "tilted";
    tilted.sizeX = 3;
    tilted.sizeY = 1;
    tilted.sizeZ = 3;
    tilted.x = 4;
    tilted.y = 0.5;
    tilted.z = -2;
    tilted.rotationY = 45;
    stairs.push_back(tilted);
    Terrain hills;
    hills.generate({9, 3, 2, -20});
    Terrain lake;
    lake.generate({5, 2.5, 0, 0.5});
    return {{"piano", flat, {}}, {"ostacoli", stairs, {}}, {"colline", {}, hills}, {"lago", {}, lake}};
}

Json terrainState(const Terrain& terrain) {
    Json raw = Json::object();
    for (const auto& key : terrain.keys()) {
        std::vector<std::uint16_t> values(Terrain::volume);
        for (int index = 0; index < Terrain::volume; ++index) {
            int i = index / (Terrain::chunk * Terrain::chunk), j = (index / Terrain::chunk) % Terrain::chunk, k = index % Terrain::chunk;
            values[static_cast<std::size_t>(index)] = terrain.cell(key.x * Terrain::chunk + i, key.y * Terrain::chunk + j, key.z * Terrain::chunk + k);
        }
        raw.set(key.text(), Terrain::encodeChunk(values));
    }
    return raw;
}

Json optionalNumber(std::optional<double> value) { return value ? Json(*value) : Json(); }

std::string refusalName(AvatarRefusal refusal) {
    switch (refusal) {
    case AvatarRefusal::none: return "ok";
    case AvatarRefusal::notOwned: return "notOwned";
    case AvatarRefusal::categoryFull: return "categoryFull";
    case AvatarRefusal::accessoriesFull: return "accessoriesFull";
    }
    return "";
}

const std::vector<double> driverDeltas = {1.0 / 60, 1.0 / 120, 1.0 / 30, 0.019, 0.3};
const std::vector<double> pacerDeltas = {0.0166, 0.0171, 0.033, 0.008, 0.0167};

AvatarItem item(const std::string& id, const std::string& category) {
    AvatarItem result;
    result.id = id;
    result.category.name = category;
    return result;
}

Json stateRow(const CharacterState& s) { return Json::array({s.x, s.y, s.z, s.yaw, s.vertical, s.grounded ? 1 : 0, s.moving ? 1 : 0, s.swimming ? 1 : 0}); }

}

TEST_CASE("Parità: rotazioni", "[parity][rotation]") {
    Json g = golden::load("rotation");
    for (const auto& c : g["cases"].items()) {
        Mat3 m = rotation::matrix(c["angles"][0].number(), c["angles"][1].number(), c["angles"][2].number());
        Json matrix = Json::array();
        for (int r = 0; r < 3; ++r) matrix.push(Json::array({m[r][0], m[r][1], m[r][2]}));
        Vec3 back = rotation::euler(m);
        INFO(c["angles"].dump());
        CHECK(golden::same(matrix, c["matrix"], 1e-12) == "");
        CHECK(golden::same(golden::array(back), c["euler"], 1e-9) == "");
    }
}

TEST_CASE("Parità: ClassRegistry", "[parity][registry]") {
    Json g = golden::load("registry");
    const ClassRegistry& registry = golden::registry();
    Json classes = Json::object();
    for (const auto& [name, d] : registry.descriptors()) {
        Json properties = Json::object();
        for (const auto& [p, descriptor] : d.properties)
            properties.set(p, Json::object({{"category", descriptor.category}, {"type", descriptor.typeName}, {"readOnly", descriptor.readOnly}, {"deprecated", descriptor.deprecated}, {"owner", descriptor.owner}}));
        classes.set(name, Json::object({{"superclass", d.superclass ? Json(*d.superclass) : Json()}, {"tags", strings(d.tags)}, {"category", d.category}, {"explorerOrder", d.explorerOrder},
                                        {"preferredParent", d.preferredParent}, {"deprecated", d.deprecated}, {"implemented", d.implemented}, {"runtime", d.runtime ? Json(*d.runtime) : Json()},
                                        {"icon", d.icon}, {"parents", strings(d.parents)}, {"supported", strings(d.supported)}, {"kind", d.kind ? Json(*d.kind) : Json()},
                                        {"creatable", d.creatable()}, {"canCreate", d.canCreate()}, {"insertable", d.insertable()}, {"defaults", propertiesJson(d.defaults)},
                                        {"properties", properties}, {"functions", sortedStrings(d.functions)}, {"events", sortedStrings(d.events)}}));
    }
    CHECK(golden::same(classes, g["classes"], golden::Options{1e-9, true}) == "");
    Json services = Json::array();
    for (const auto& s : registry.services()) services.push(Json::object({{"name", s.name}, {"icon", s.icon}}));
    CHECK(golden::same(services, g["services"]) == "");
    Json serviceChildren = Json::object();
    for (const auto& [k, v] : registry.serviceChildren()) serviceChildren.set(k, strings(v));
    CHECK(golden::same(serviceChildren, g["serviceChildren"]) == "");
    Json defaults = Json::array();
    for (const auto& [className, parent] : registry.defaultChildren()) defaults.push(Json::object({{"className", className}, {"parent", parent}}));
    CHECK(golden::same(defaults, g["defaultChildren"]) == "");
    Json enums = Json::object();
    for (const auto& [k, v] : registry.enums()) enums.set(k, strings(v));
    CHECK(golden::same(enums, g["enums"]) == "");
    CHECK(registry.dataset() == g["dataset"].integer());
    std::vector<std::string> implemented, parents;
    for (const auto& [name, d] : registry.descriptors())
        if (d.implemented) implemented.push_back(name);
    for (const auto& s : registry.services()) parents.push_back(s.name);
    parents.insert(parents.end(), implemented.begin(), implemented.end());
    parents.push_back("DataModel");
    std::sort(parents.begin(), parents.end());
    Json matrix = Json::object();
    for (const auto& child : implemented) {
        std::vector<std::string> allowed;
        for (const auto& parent : parents)
            if (registry.canParent(child, parent)) allowed.push_back(parent);
        matrix.set(child, strings(allowed));
    }
    CHECK(golden::same(matrix, g["canParent"]) == "");
    Json insertable = Json::object();
    for (const auto& parent : parents) {
        std::vector<std::string> names;
        for (const auto* d : registry.insertable(parent)) names.push_back(d->className);
        insertable.set(parent, strings(names));
    }
    CHECK(golden::same(insertable, g["insertable"]) == "");
}

TEST_CASE("Parità: codec esperienza e proprietà", "[parity][codec]") {
    Json g = golden::load("codec");
    auto matched = [](const Json& actual, Json& expected, auto... options) {
        if (golden::writing()) {
            expected = actual;
            return std::string();
        }
        return golden::same(actual, expected, options...);
    };
    golden::registry();
    for (auto& c : g["cases"].items()) {
        auto parsed = Experience::parse(c["input"].string());
        INFO(c["input"].string());
        if (c.contains("error")) {
            CHECK_FALSE(parsed.ok());
            continue;
        }
        REQUIRE(parsed.ok());
        Json encoded = parsed->json();
        CHECK(encoded["languageVersion"].string() == rules::lim::languageVersion);
        CHECK(encoded["engineApiVersion"].integer() == rules::lim::engineApiVersion);
        encoded.erase("languageVersion");
        encoded.erase("engineApiVersion");
        CHECK(matched(encoded, c["output"]) == "");
        auto again = Experience::parse(parsed->json().dump());
        REQUIRE(again.ok());
        CHECK(*again == *parsed);
    }
    std::vector<PropertyValue> values = {PropertyValue::string("a"), PropertyValue::number(1.5), PropertyValue::boolean(true), PropertyValue::vector3(1, 2, 3), PropertyValue::vector2(1, 2),
                                         PropertyValue::color3("AABBCC"), PropertyValue::udim(0.5, 3), PropertyValue::udim2(1, 2, 3, 4), PropertyValue::enumItem("Wood"),
                                         PropertyValue::reference(std::nullopt), PropertyValue::reference("x"), PropertyValue::content("c")};
    for (std::size_t i = 0; i < values.size(); ++i) {
        Json& expected = g["properties"].items()[i];
        CHECK(matched(values[i].json(), expected["value"]) == "");
        CHECK(values[i].typeName() == expected["type"].string());
        CHECK(values[i].display() == expected["text"].string());
        auto back = PropertyValue::fromJson(expected["value"]);
        REQUIRE(back.ok());
        CHECK(*back == values[i]);
    }
    CHECK(matched(Experience::baseplate("5a1f0c2e-7d3b-4c1a-8e2f-0000000000ab").json(), g["baseplate"]) == "");
    CHECK(static_cast<long long>(Experience::blank().blocks.size()) == g["blankBlocks"].integer());
    if (golden::writing()) golden::save("codec", g);
}

Json meshState(const TerrainMesh& mesh, bool detailed) {
    Json out = Json::object({{"vertices", static_cast<long long>(mesh.positions.size())}, {"indices", static_cast<long long>(mesh.indices.size())}});
    if (!detailed) return out;
    Json positions = Json::array(), normals = Json::array(), colors = Json::array(), kinds = Json::array(), triangles = Json::array();
    for (std::size_t i = 0; i < mesh.positions.size(); ++i) {
        for (double v : {mesh.positions[i].x, mesh.positions[i].y, mesh.positions[i].z}) positions.push(v);
        for (double v : {mesh.normals[i].x, mesh.normals[i].y, mesh.normals[i].z}) normals.push(v);
        for (double v : {mesh.colors[i].x, mesh.colors[i].y, mesh.colors[i].z}) colors.push(v);
        kinds.push(static_cast<int>(mesh.kinds[i]));
    }
    for (auto index : mesh.indices) triangles.push(static_cast<long long>(index));
    out.set("positions", positions);
    out.set("normals", normals);
    out.set("colors", colors);
    out.set("kinds", kinds);
    out.set("triangles", triangles);
    return out;
}

void rewriteTerrain(Json g) {
    for (auto& entry : g["generated"].items()) {
        Terrain t;
        t.generate(TerrainSettings::fromJson(entry["settings"]));
        entry.set("chunks", terrainState(t));
    }
    Terrain base;
    base.generate({7, 4, 1, 0});
    g.set("base", base.json());
    Terrain edited = base;
    for (auto& stroke : g["brushes"].items()) {
        auto tool = TerrainTool::add;
        for (auto candidate : {TerrainTool::add, TerrainTool::subtract, TerrainTool::paint, TerrainTool::smooth, TerrainTool::flatten, TerrainTool::water})
            if (toolName(candidate) == stroke["tool"].string()) tool = candidate;
        TerrainBrush brush{stroke["radius"].number(), stroke["strength"].number(), static_cast<TerrainMaterial>(stroke["material"].integer()), TerrainPlacement::free};
        stroke.set("changed", edited.apply(tool, golden::vec3(stroke["point"]), brush));
        stroke.set("chunks", terrainState(edited));
    }
    for (auto& s : g["samples"].items()) {
        Vec3 p = golden::vec3(s["point"]);
        s.set("solid", base.sample(p.x, p.y, p.z));
        s.set("water", base.sample(p.x, p.y, p.z, true));
        s.set("isSolid", base.solid(p.x, p.y, p.z));
        s.set("isWater", base.water(p.x, p.y, p.z));
        s.set("surface", optionalNumber(base.surface(p.x, p.z, p.y)));
        s.set("exit", optionalNumber(base.exit(p.x, p.z, p.y)));
        s.set("raycast", optionalNumber(base.raycast(p, {0.3, -0.9, 0.2}, 20)));
    }
    for (auto& m : g["meshes"].items()) {
        auto key = ChunkKey::parse(m["key"].string());
        REQUIRE(key);
        Json state = meshState(meshChunk(base, *key, m["water"].boolean()), m.contains("positions"));
        for (const auto& [name, value] : state.members()) m.set(name, value);
    }
    golden::save("terrain", g);
}

TEST_CASE("Parità: terreno, pennelli, campionamento e mesh", "[parity][terrain]") {
    Json g = golden::load("terrain");
    if (golden::writing()) rewriteTerrain(g);
    g = golden::load("terrain");
    for (const auto& entry : g["generated"].items()) {
        Terrain t;
        t.generate(TerrainSettings::fromJson(entry["settings"]));
        CHECK(golden::same(terrainState(t), entry["chunks"]) == "");
    }
    Terrain base;
    base.generate({7, 4, 1, 0});
    CHECK(golden::same(base.json(), g["base"]) == "");
    auto decoded = Terrain::fromJson(g["base"]);
    REQUIRE(decoded.ok());
    CHECK(*decoded == base);
    Terrain edited = base;
    for (const auto& stroke : g["brushes"].items()) {
        auto tool = TerrainTool::add;
        for (auto candidate : {TerrainTool::add, TerrainTool::subtract, TerrainTool::paint, TerrainTool::smooth, TerrainTool::flatten, TerrainTool::water})
            if (toolName(candidate) == stroke["tool"].string()) tool = candidate;
        TerrainBrush brush{stroke["radius"].number(), stroke["strength"].number(), static_cast<TerrainMaterial>(stroke["material"].integer()), TerrainPlacement::free};
        int changed = edited.apply(tool, golden::vec3(stroke["point"]), brush);
        INFO(stroke["tool"].string());
        CHECK(changed == stroke["changed"].integer());
        CHECK(golden::same(terrainState(edited), stroke["chunks"]) == "");
    }
    for (const auto& s : g["samples"].items()) {
        Vec3 p = golden::vec3(s["point"]);
        Json actual = Json::object({{"point", s["point"]}, {"solid", base.sample(p.x, p.y, p.z)}, {"water", base.sample(p.x, p.y, p.z, true)}, {"isSolid", base.solid(p.x, p.y, p.z)},
                                    {"isWater", base.water(p.x, p.y, p.z)}, {"surface", optionalNumber(base.surface(p.x, p.z, p.y))}, {"exit", optionalNumber(base.exit(p.x, p.z, p.y))},
                                    {"raycast", optionalNumber(base.raycast(p, {0.3, -0.9, 0.2}, 20))}});
        CHECK(golden::same(actual, s, 1e-12) == "");
    }
    for (const auto& m : g["meshes"].items()) {
        auto key = ChunkKey::parse(m["key"].string());
        REQUIRE(key);
        TerrainMesh mesh = meshChunk(base, *key, m["water"].boolean());
        INFO(m["key"].string() + (m["water"].boolean() ? " acqua" : ""));
        CHECK(static_cast<long long>(mesh.positions.size()) == m["vertices"].integer());
        CHECK(static_cast<long long>(mesh.indices.size()) == m["indices"].integer());
        if (m.contains("positions")) {
            Json positions = Json::array(), normals = Json::array(), colors = Json::array(), kinds = Json::array(), triangles = Json::array();
            for (std::size_t i = 0; i < mesh.positions.size(); ++i) {
                for (double v : {mesh.positions[i].x, mesh.positions[i].y, mesh.positions[i].z}) positions.push(v);
                for (double v : {mesh.normals[i].x, mesh.normals[i].y, mesh.normals[i].z}) normals.push(v);
                for (double v : {mesh.colors[i].x, mesh.colors[i].y, mesh.colors[i].z}) colors.push(v);
                kinds.push(static_cast<int>(mesh.kinds[i]));
            }
            for (auto index : mesh.indices) triangles.push(static_cast<long long>(index));
            CHECK(golden::same(positions, m["positions"], 1e-6) == "");
            CHECK(golden::same(normals, m["normals"], 1e-5) == "");
            CHECK(golden::same(colors, m["colors"], 1e-6) == "");
            CHECK(golden::same(kinds, m["kinds"]) == "");
            CHECK(golden::same(triangles, m["triangles"]) == "");
        }
    }
    for (const auto& n : g["noise"].items()) {
        double x = n[0].number(), z = n[1].number();
        CHECK(golden::close(Terrain::noise(x, z, 7, 3), n[2].number(), 1e-12));
        CHECK(golden::close(Terrain::hash(static_cast<long long>(x), static_cast<long long>(z), 7), n[3].number(), 1e-12));
    }
}

TEST_CASE("Parità: movimento del personaggio, driver e pacer", "[parity][motion]") {
    Json g = golden::load("motion");
    auto matched = [](const Json& actual, Json& expected, auto... options) {
        if (golden::writing()) {
            expected = actual;
            return std::string();
        }
        return golden::same(actual, expected, options...);
    };
    auto list = worlds();
    std::size_t traceIndex = 0;
    for (const auto& world : list) {
        for (const auto& [pattern, yaw] : std::vector<std::pair<std::string, double>>{{"avanti", 0.0}, {"diagonale", 0.7}, {"salto", -1.2}, {"giro", 2.5}}) {
            Json& trace = g["traces"].items()[traceIndex++];
            REQUIRE(trace["world"].string() == world.name);
            REQUIRE(trace["pattern"].string() == pattern);
            CharacterState state = motion::spawn(world.blocks, world.terrain, 0, 5);
            Json steps = Json::array({stateRow(state)});
            for (int index = 0; index < 240; ++index) {
                CharacterInput input;
                if (pattern == "avanti") input.forward = 1;
                else if (pattern == "diagonale") {
                    input.forward = 1;
                    input.strafe = index < 120 ? 1 : -1;
                } else if (pattern == "salto") {
                    input.forward = 1;
                    input.jump = index % 50 == 10;
                    input.ascend = index > 150;
                } else {
                    input.forward = index % 80 < 40 ? 1 : -1;
                    input.strafe = 0.5;
                    input.pace = 1.5;
                    input.leap = 0.8;
                    input.jump = index == 30;
                }
                double delta = index % 7 == 3 ? 1.0 / 30 : 1.0 / 60;
                state = motion::step(state, input, yaw + index * 0.004, world.blocks, world.terrain, delta);
                steps.push(stateRow(state));
            }
            INFO(world.name + " " + pattern);
            CHECK(matched(steps, trace["steps"], 1e-9) == "");
        }
    }
    std::size_t spawnIndex = 0;
    for (const auto& world : list)
        for (auto [x, z] : std::vector<std::pair<double, double>>{{0, 5}, {3, -4}, {40, 40}}) {
            CharacterState s = motion::spawn(world.blocks, world.terrain, x, z);
            CHECK(matched(Json::array({x, z, s.x, s.y, s.z}), g["spawns"].items()[spawnIndex++]) == "");
        }
    CharacterDriver driver;
    driver.reset({});
    Json poses = Json::array();
    double t = 0;
    for (int index = 0; index < 200; ++index) {
        double delta = driverDeltas[static_cast<std::size_t>(index % 5)];
        t += delta;
        int steps = driver.advance(delta, [&](const CharacterState& s, double step) -> std::optional<CharacterState> {
            CharacterState next = s;
            next.x += 3 * step;
            next.y = std::sin(t) * 0.4 + (index > 100 ? 1 : 0);
            next.grounded = index % 9 != 0;
            next.yaw = s.yaw + 0.05;
            if (index == 150) next.x += 10;
            return next;
        });
        CharacterPose pose = driver.pose(delta);
        Vec3 focus = driver.follow({pose.x, pose.y + 0.9, pose.z}, delta);
        poses.push(Json::array({delta, steps, pose.x, pose.y, pose.z, pose.yaw, focus.x, focus.y, focus.z}));
    }
    CHECK(matched(poses, g["driver"], 1e-9) == "");
    FramePacer pacer;
    Json paced = Json::array();
    for (int index = 0; index < 60; ++index) {
        double raw = pacerDeltas[static_cast<std::size_t>(index % 5)];
        paced.push(Json::array({raw, pacer.next(raw, 1.0 / 60)}));
    }
    CHECK(matched(paced, g["pacer"]) == "");
    CHECK(motion::speed == g["constants"]["speed"].number());
    CHECK(motion::gravity == g["constants"]["gravity"].number());
    CHECK(motion::jumpSpeed == g["constants"]["jumpSpeed"].number());
    CHECK(CharacterDriver::step == g["constants"]["step"].number());
    if (golden::writing()) golden::save("motion", g);
}

TEST_CASE("Parità: camera, orbita, zoom e collisioni", "[parity][camera]") {
    Json g = golden::load("camera");
    auto matched = [](const Json& actual, Json& expected, auto... options) {
        if (golden::writing()) {
            expected = actual;
            return std::string();
        }
        return golden::same(actual, expected, options...);
    };
    for (auto& row : g["looks"].items()) {
        double yaw = row[0].number(), pitch = row[1].number(), dx = row[2].number(), dy = row[3].number(), sensitivity = row[4].number();
        Vec2 look = camera_input::look(yaw, pitch, dx, dy, sensitivity);
        Vec2 character = camera_input::look(yaw, pitch, dx, dy, 1, camera_input::characterPitchLow, camera_input::characterPitchHigh);
        Vec3 f = camera_input::forward(yaw, pitch), u = camera_input::up(yaw, pitch);
        CHECK(matched(Json::array({yaw, pitch, dx, dy, sensitivity, look.x, look.y, character.x, character.y, f.x, f.y, f.z, u.x, u.y, u.z}), row) == "");
    }
    for (auto& row : g["pans"].items()) {
        double yaw = row[0].number(), pitch = row[1].number(), dx = row[2].number(), dy = row[3].number();
        Vec3 p = camera_input::pan({1, 2, 3}, yaw, pitch, dx, dy, 0.05), e = camera_input::eye({1, 2, 3}, yaw, pitch, 10), t = camera_input::target({1, 2, 3}, yaw, pitch, 10);
        CHECK(matched(Json::array({yaw, pitch, dx, dy, p.x, p.y, p.z, e.x, e.y, e.z, t.x, t.y, t.z}), row) == "");
    }
    for (auto& row : g["flies"].items()) {
        auto fly = camera_input::fly(row[2].number(), row[3].number(), row[4].number(), row[0].number(), row[1].number());
        CHECK(matched(fly ? golden::array(*fly) : Json(), row.items()[5]) == "");
    }
    for (const auto& row : g["zooms"].items()) CHECK(golden::close(camera_rig::zoom(row[0].number(), row[1].number()), row[2].number(), 1e-12));
    for (const auto& row : g["recovers"].items()) CHECK(golden::close(camera_rig::recover(row[0].number(), row[0].number() * 1.5, row[2].number()), row[3].number(), 1e-12));
    auto list = worlds();
    for (auto& row : g["places"].items()) {
        const GoldenWorld* world = nullptr;
        for (const auto& w : list)
            if (w.name == row[0].string()) world = &w;
        REQUIRE(world);
        Vec3 head = golden::vec3(row[1]);
        double yaw = row[2].number(), pitch = row[3].number(), distance = row[4].number();
        auto placed = camera_rig::place(head, yaw, pitch, distance, world->blocks, world->terrain);
        double clearance = camera_rig::clearance(head, yaw, pitch, distance, world->blocks, world->terrain);
        INFO(row.dump());
        CHECK(matched(Json::array({row[0], row[1], yaw, pitch, distance, golden::array(placed.position), placed.firstPerson, clearance}), row) == "");
    }
    if (golden::writing()) golden::save("camera", g);
}

TEST_CASE("Parità: trasformazioni di Studio", "[parity][transform]") {
    Json g = golden::load("transform");
    for (const auto& r : g["resizes"].items()) {
        WorldBlock b = blockFrom(r["block"]);
        WorldBlock result = transform_math::resized(b, static_cast<int>(r["axis"].integer()), static_cast<int>(r["sign"].integer()), r["delta"].number(), r["snap"].number());
        CHECK(golden::same(result.json(), r["result"]) == "");
    }
    for (const auto& s : g["scales"].items()) {
        BlockSet group = blockSet(s["blocks"]);
        int anchor = static_cast<int>(s["anchor"].integer());
        BlockSet scaled = transform_math::scaled(group, static_cast<int>(s["axis"].integer()), anchor == 0 ? ScaleAnchor::middle() : ScaleAnchor::face(anchor), s["factor"].number());
        CHECK(golden::same(blockList(scaled), s["result"]) == "");
    }
    for (const auto& m : g["moves"].items()) {
        BlockSet group = blockSet(m["blocks"]);
        BlockSet moved = transform_math::moved(group, golden::vec3(m["direction"]), m["delta"].number(), m["snap"].number(), m["worldAligned"].boolean());
        CHECK(golden::same(blockList(moved), m["result"]) == "");
    }
    for (const auto& c : g["closest"].items()) {
        auto value = transform_math::closestParameter({c[0].number(), c[1].number(), c[2].number()}, {c[3].number(), c[4].number(), c[5].number()}, {c[6].number(), c[7].number(), c[8].number()},
                                                      {c[9].number(), c[10].number(), c[11].number()});
        CHECK(golden::same(optionalNumber(value), c[12]) == "");
    }
    for (const auto& f : g["frames"].items()) {
        std::vector<WorldBlock> blocks;
        for (const auto& b : f["blocks"].items()) blocks.push_back(blockFrom(b));
        auto frame = transform_math::frame(blocks, f["local"].boolean() ? TransformSpace::local : TransformSpace::world, f["single"].boolean());
        REQUIRE(frame);
        Json axes = Json::array();
        for (const auto& a : frame->axes) axes.push(golden::array(a));
        CHECK(golden::same(golden::array(frame->center), f["center"]) == "");
        CHECK(golden::same(axes, f["axes"]) == "");
        CHECK(golden::same(Json::array({frame->half[0], frame->half[1], frame->half[2]}), f["half"]) == "");
    }
}

TEST_CASE("Parità: avatar", "[parity][avatar]") {
    Json g = golden::load("avatar");
    AvatarItem hat = item("h1", "hat"), shirt = item("s1", "shirt");
    std::vector<AvatarItem> sequence = {hat, item("h2", "hat"), item("h3", "hat"), item("h4", "hat"), item("hair", "hair"), shirt, item("s2", "shirt"), item("x", "face")};
    std::set<std::string> owned = {"h1", "h2", "h3", "h4", "hair", "s1", "s2"};
    AvatarConfiguration config = AvatarConfiguration::standard("Prova", "female");
    for (std::size_t i = 0; i < sequence.size(); ++i) {
        const Json& expected = g["equip"][i];
        AvatarRefusal refusal = config.equip(sequence[i], owned);
        CHECK(refusalName(refusal) == expected["result"].string());
        CHECK(golden::same(config.json()["equipped"], expected["equipped"]) == "");
    }
    std::set<std::string> ownedSubset = {"h1", "s1"};
    std::map<std::string, AvatarItem> catalog = {{"h1", hat}, {"s1", shirt}};
    for (const auto& v : g["validate"].items()) {
        auto input = AvatarConfiguration::fromJson(v["input"]);
        REQUIRE(input.ok());
        CHECK(golden::same(input->json(), v["input"]) == "");
        CHECK(golden::same(input->validated("Utente", "male").json(), v["plain"]) == "");
        CHECK(golden::same(input->validated("Utente", "male", &ownedSubset, &catalog).json(), v["owned"]) == "");
    }
    auto legacy = AvatarConfiguration::fromJson(*Json::parse(R"({"displayName":"Vecchio","skinTone":"E0AC86","shirtColor":"2FA84F"})"));
    REQUIRE(legacy.ok());
    CHECK(golden::same(legacy->json(), g["legacy"]) == "");
    CHECK(golden::same(AvatarConfiguration::standard("X", "alien").json(), g["standard"]) == "");
    Json limits = Json::array();
    for (const auto& c : AvatarCategory::all()) limits.push(Json::array({c.name, c.limit(), c.accessory() ? 1 : 0, groupName(c.group())}));
    CHECK(golden::same(limits, g["limits"]) == "");
}
