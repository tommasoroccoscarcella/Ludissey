#include "Engine/Rendering/Renderer.hpp"
#include "Engine/Rendering/World.hpp"
#include "Engine/Studio/StudioDocument.hpp"
#include "Golden.hpp"
#include "Interface/Theme.hpp"
#include "Pixels.hpp"
#include "Studio/StudioStage.hpp"

#include <Catch2/catch_amalgamated.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <set>
#include <sstream>

using namespace ld;
using pixels::Pixels;

namespace {

struct View {
    std::string name;
    double yaw, pitch;
};

const std::vector<View> quadrants = {{"davanti", 0.15, 0.3}, {"destra", ld::pi / 2 + 0.2, 0.35}, {"dietro", ld::pi + 0.25, 0.3}, {"sinistra", -ld::pi / 2 - 0.2, 0.4}, {"sopra", 0.6, 1.2}, {"sotto", 0.5, -0.45}};

ui::PointerEvent event(ui::PointerPhase phase, double x, double y) {
    ui::PointerEvent e;
    e.phase = phase;
    e.location = {x, y};
    return e;
}

struct Viewport {
    const ClassRegistry& registry = golden::registry();
    StudioDocument document;
    std::unique_ptr<studio::StudioStage> stage;
    ui::Size size{960, 600};

    explicit Viewport(const Experience& project = Experience::blank()) {
        REQUIRE(document.open(project));
        stage = std::make_unique<studio::StudioStage>(document);
        stage->size = size;
    }

    bool send(ui::PointerPhase phase, double x, double y) { return stage->pointer(event(phase, x, y), size); }
    bool send(ui::PointerPhase phase, Vec3 p) { return send(phase, p.x, p.y); }
    Vec3 screen(Vec3 world) const { return render::project(*stage->camera(), {size.width, size.height}, world); }

    Pixels shot() {
        stage->refresh();
        auto image = stage->snapshot(static_cast<int>(size.width), static_cast<int>(size.height));
        return {image.width, image.height, image.rgba};
    }

    const WorldBlock& block(const std::string& id) const {
        for (const auto& b : document.world.blocks)
            if (b.id == id) return b;
        FAIL("parte assente " << id);
        return document.world.blocks.front();
    }

    std::string plate() const { return Experience::baseplate(document.world.id).id; }

    render::NodePtr child(const std::string& name) const { return stage->scene()->root->child(name); }
};

void keep(const Pixels& image, const std::string& name) {
    if (const char* folder = std::getenv("LUDISSEY_SHOTS")) {
        std::filesystem::create_directories(folder);
        pixels::save(image, std::filesystem::path(folder) / ("studio " + name + ".png"));
    }
}

double structure(const Pixels& a, const Pixels& b, int columns, int rows) {
    REQUIRE(a.width == b.width);
    REQUIRE(a.height == b.height);
    double total = 0;
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < columns; ++c) {
            double sum[2][3] = {};
            int count = 0;
            for (int y = r * a.height / rows; y < (r + 1) * a.height / rows; ++y)
                for (int x = c * a.width / columns; x < (c + 1) * a.width / columns; ++x) {
                    std::size_t i = static_cast<std::size_t>((y * a.width + x) * 4);
                    for (int k = 0; k < 3; ++k) {
                        sum[0][k] += a.rgba[i + k];
                        sum[1][k] += b.rgba[i + k];
                    }
                    ++count;
                }
            for (int k = 0; k < 3; ++k) total += std::abs(sum[0][k] - sum[1][k]) / count;
        }
    return total / (columns * rows * 3);
}

void compare(const Pixels& actual, const std::string& name) {
    keep(actual, name);
    auto path = golden::root() / "Tests/Golden/Studio" / (name + ".png");
    const char* mode = std::getenv("LUDISSEY_GOLDEN");
    if (mode && std::string(mode) == "write") {
        std::filesystem::create_directories(path.parent_path());
        pixels::save(actual, path);
        return;
    }
    REQUIRE(std::filesystem::exists(path));
    double difference = structure(actual, pixels::load(path), 32, 20);
    INFO(name << ": differenza strutturale " << difference);
    CHECK(difference < 2.0);
}

std::array<int, 3> rgb(const Pixels& p, int x, int y) {
    std::size_t i = static_cast<std::size_t>((y * p.width + x) * 4);
    return {p.rgba[i], p.rgba[i + 1], p.rgba[i + 2]};
}

std::array<double, 4> box(const std::vector<Vec3>& points) {
    std::array<double, 4> b{1e9, 1e9, -1e9, -1e9};
    for (const auto& p : points) {
        b[0] = std::min(b[0], p.x);
        b[1] = std::min(b[1], p.y);
        b[2] = std::max(b[2], p.x);
        b[3] = std::max(b[3], p.y);
    }
    return b;
}

std::vector<Vec3> corners(const Mat4& m, Vec3 low, Vec3 high) {
    std::vector<Vec3> out;
    for (double x : {low.x, high.x})
        for (double y : {low.y, high.y})
            for (double z : {low.z, high.z}) out.push_back(m.transformPoint({x, y, z}));
    return out;
}

struct Sample {
    std::string where;
    Vec3 world;
};

std::vector<Sample> samples(const render::Node& holder, bool ring) {
    std::vector<Sample> out;
    Mat4 frame = holder.world();
    if (ring) {
        const auto& torus = holder.children().front();
        Bounds b = torus->geometry->bounds();
        double radius = b.high.x - b.high.y;
        Mat4 local = frame * torus->local();
        for (int degrees = 0; degrees < 360; degrees += 30) {
            double a = degrees * ld::pi / 180;
            out.push_back({"anello " + std::to_string(degrees) + "°", local.transformPoint({radius * std::sin(a), 0, radius * std::cos(a)})});
        }
        return out;
    }
    const auto& shaft = holder.children()[0];
    const auto& head = holder.children()[1];
    Bounds s = shaft->geometry->bounds();
    Mat4 local = frame * shaft->local();
    out.push_back({"inizio asta", local.transformPoint({0, s.low.y + (s.high.y - s.low.y) * 0.08, 0})});
    out.push_back({"centro asta", local.transformPoint({0, (s.low.y + s.high.y) / 2, 0})});
    out.push_back({"fine asta", local.transformPoint({0, s.low.y + (s.high.y - s.low.y) * 0.9, 0})});
    out.push_back({"punta", head->worldPosition()});
    return out;
}

std::optional<std::pair<int, int>> handleOf(const std::string& name) {
    if (name.rfind("gizmo:", 0) != 0) return std::nullopt;
    auto colon = name.find(':', 6);
    return std::make_pair(std::stoi(name.substr(6, colon - 6)), std::stoi(name.substr(colon + 1)));
}

double segmentDistance(Vec3 p, Vec3 a, Vec3 b) {
    double dx = b.x - a.x, dy = b.y - a.y, length = dx * dx + dy * dy;
    double t = length > 1e-9 ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / length, 0.0, 1.0) : 0;
    return std::hypot(p.x - (a.x + dx * t), p.y - (a.y + dy * t));
}

bool crowded(Viewport& v, const render::Node& gizmo, std::pair<int, int> handle, Vec3 at, bool ring) {
    for (const auto& holder : gizmo.children()) {
        auto other = handleOf(holder->name);
        if (!other || *other == handle) continue;
        auto points = samples(*holder, ring);
        if (ring) {
            for (std::size_t i = 0; i < points.size(); ++i)
                if (segmentDistance(at, v.screen(points[i].world), v.screen(points[(i + 1) % points.size()].world)) < 14) return true;
            continue;
        }
        Mat4 frame = holder->world();
        Vec3 a = v.screen(frame.transformPoint({0, 0, 0})), b = v.screen(points.back().world);
        if (segmentDistance(at, a, b) < 14) return true;
    }
    return false;
}

Vec3 component(Vec3 v, int axis) { return {axis == 0 ? v.x : 0, axis == 1 ? v.y : 0, axis == 2 ? v.z : 0}; }

}

TEST_CASE("Studio: colore standard di Part e Baseplate", "[studio][part][baseplate]") {
    Viewport v;
    std::string standard(rules::part::color);
    REQUIRE_FALSE(v.document.world.blocks.empty());
    const WorldBlock& plate = v.block(v.plate());
    CHECK(plate.color == standard);
    CHECK_FALSE(plate.locked);
    CHECK_FALSE(v.document.isLocked(plate.id));
    CHECK(WorldBlock().color == standard);
    REQUIRE(v.document.addPart("cube"));
    std::string ribbon = v.document.selectedIds().front();
    auto inserted = v.document.insert("Part", std::string("Workspace"));
    REQUIRE(inserted);
    for (const auto& id : {ribbon, *inserted}) CHECK(v.block(id).color == standard);
    v.document.select({ribbon});
    v.document.duplicateSelection();
    std::string copy = v.document.selectedIds().front();
    CHECK(copy != ribbon);
    CHECK(v.block(copy).color == standard);
    v.document.select({ribbon});
    v.document.run(StudioCommand::copy);
    v.document.run(StudioCommand::paste);
    std::string pasted = v.document.selectedIds().front();
    CHECK(pasted != ribbon);
    CHECK(pasted != copy);
    CHECK(v.block(pasted).color == standard);
    auto reloaded = Experience::parse(v.document.world.json().dump());
    REQUIRE(reloaded);
    for (const auto& b : reloaded->blocks) {
        CHECK(b.color == standard);
        if (b.id == v.plate()) CHECK_FALSE(b.locked);
    }
    StudioDocument again;
    REQUIRE(again.open(*reloaded));
    v.document.play();
    REQUIRE(v.document.playing);
    double clock = 0;
    for (int i = 0; i < 30; ++i) v.document.tick(clock += 1.0 / 60);
    v.document.stop();
    for (const auto& id : {ribbon, *inserted, copy, pasted, v.plate()}) CHECK(v.block(id).color == standard);
    CHECK_FALSE(v.block(v.plate()).locked);
    v.document.tool = StudioTool::select;
    v.document.setSelection(std::nullopt);
    v.stage->orient(0.3, 0.6, 30, {0, 0, 0});
    v.stage->refresh();
    Vec3 surface = v.screen({-20, 0, -18});
    v.send(ui::PointerPhase::down, surface);
    v.send(ui::PointerPhase::up, surface);
    REQUIRE_FALSE(v.document.selectedIds().empty());
    CHECK(v.document.selectedIds().front() == v.plate());
    double before = v.block(v.plate()).x;
    v.send(ui::PointerPhase::down, surface);
    for (int i = 1; i <= 8; ++i) v.send(ui::PointerPhase::dragged, surface.x + i * 10, surface.y);
    v.send(ui::PointerPhase::up, surface.x + 80, surface.y);
    CHECK(std::abs(v.block(v.plate()).x - before) > 0.5);
}

TEST_CASE("Studio: spigoli a raggio fisso", "[studio][part][bevel]") {
    struct Case {
        std::string name;
        Vec3 size;
    };
    std::vector<Case> cases = {{"Baseplate", {60, 1, 60}}, {"Part 1×1×1", {1, 1, 1}}, {"Part standard", {4, 1.2, 2}}, {"Part allungata", {12, 1, 1}}, {"Part piatta", {4, 0.2, 4}}, {"Part scalata", {8, 8, 8}}};
    auto radius = [](const render::Geometry& g, Vec3 size) {
        double top = size.y / 2, flat = 0;
        for (std::size_t i = 0; i + 2 < g.positions.size(); i += 3)
            if (std::abs(g.positions[i + 1] - top) < 1e-4) flat = std::max(flat, static_cast<double>(std::abs(g.positions[i])));
        return size.x / 2 - flat;
    };
    for (const auto& c : cases) {
        WorldBlock block;
        block.kind = "cube";
        block.sizeX = c.size.x;
        block.sizeY = c.size.y;
        block.sizeZ = c.size.z;
        block.rotationY = c.name == "Part scalata" ? 35 : 0;
        auto node = render::scene_factory::block(block);
        render::scene_factory::apply(block, *node);
        INFO(c.name);
        REQUIRE(node->geometry);
        CHECK(node->scale.x == 1);
        CHECK(node->scale.y == 1);
        CHECK(node->scale.z == 1);
        Bounds b = node->geometry->bounds();
        CHECK(std::abs(b.high.x - b.low.x - c.size.x) < 1e-3);
        CHECK(std::abs(b.high.y - b.low.y - c.size.y) < 1e-3);
        CHECK(std::abs(b.high.z - b.low.z - c.size.z) < 1e-3);
        double expected = render::scene_factory::edge(c.size);
        CHECK(std::abs(radius(*node->geometry, c.size) - expected) < 1e-3);
        if (std::min({c.size.x, c.size.y, c.size.z}) >= 0.2) CHECK(std::abs(expected - render::scene_factory::edgeRadius) < 1e-9);
    }
    CHECK(std::abs(render::scene_factory::edge({1, 0.08, 1}) - 0.02) < 1e-9);
    Viewport v;
    v.document.addPart("cube");
    v.stage->refresh();
    auto plateNode = v.child(v.plate());
    auto partNode = v.child(v.document.selectedIds().front());
    REQUIRE(plateNode);
    REQUIRE(partNode);
    CHECK(std::abs(radius(*plateNode->geometry, {60, 1, 60}) - radius(*partNode->geometry, {1, 1, 1})) < 1e-3);
    CHECK(plateNode->material->diffuse == partNode->material->diffuse);
    CHECK(plateNode->material->lighting == partNode->material->lighting);
}

TEST_CASE("Studio: griglia del Baseplate", "[studio][grid]") {
    Viewport v;
    v.stage->refresh();
    auto grid = v.child("studio.grid");
    REQUIRE(grid);
    auto expectFollows = [&](const std::string& step) {
        INFO(step);
        const WorldBlock& plate = v.block(v.plate());
        CHECK_FALSE(grid->hidden);
        Vec3 position = grid->worldPosition();
        CHECK(std::abs(position.x - plate.x) < 1e-9);
        CHECK(std::abs(position.y - plate.y) < 1e-9);
        CHECK(std::abs(position.z - plate.z) < 1e-9);
        Mat3 expected = plate.rotationMatrix();
        Mat4 world = grid->world();
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c) CHECK(std::abs(world.at(r, c) - expected[r][c]) < 1e-9);
        double reachX = 0, reachZ = 0, top = -1e9;
        for (const auto& line : grid->children()) {
            Bounds b = line->geometry->bounds();
            reachX = std::max(reachX, std::abs(line->position.x) + std::abs(b.high.x * line->scale.x));
            reachZ = std::max(reachZ, std::abs(line->position.z) + std::abs(b.high.z * line->scale.z));
            top = std::max(top, line->position.y + b.low.y * line->scale.y);
        }
        CHECK(reachX <= plate.sizeX / 2 + 0.031);
        CHECK(reachZ <= plate.sizeZ / 2 + 0.031);
        CHECK(reachX >= plate.sizeX / 2 - 2.1);
        CHECK(top >= plate.sizeY / 2);
        CHECK(top - plate.sizeY / 2 < 0.01);
    };
    expectFollows("iniziale");
    Vec3 start = grid->worldPosition();
    v.document.update(v.plate(), [](WorldBlock& b) {
        b.x += 7;
        b.y += 2;
        b.z -= 5;
    }, std::string("Sposta"));
    v.stage->refresh();
    expectFollows("spostato");
    Vec3 moved = grid->worldPosition();
    CHECK(std::abs(moved.x - start.x - 7) < 1e-9);
    CHECK(std::abs(moved.y - start.y - 2) < 1e-9);
    CHECK(std::abs(moved.z - start.z + 5) < 1e-9);
    v.document.undo();
    v.stage->refresh();
    expectFollows("annulla");
    CHECK((grid->worldPosition() - start).length() < 1e-9);
    v.document.redo();
    v.stage->refresh();
    expectFollows("ripeti");
    CHECK((grid->worldPosition() - moved).length() < 1e-9);
    v.document.update(v.plate(), [](WorldBlock& b) { b.rotationY = 30; }, std::string("Ruota"));
    v.stage->refresh();
    expectFollows("ruotato");
    v.document.update(v.plate(), [](WorldBlock& b) {
        b.sizeX = 24;
        b.sizeZ = 16;
        b.sizeY = 2;
    }, std::string("Ridimensiona"));
    v.stage->refresh();
    expectFollows("ridimensionato");
    auto saved = Experience::parse(v.document.world.json().dump());
    REQUIRE(saved);
    Viewport reopened(*saved);
    reopened.stage->refresh();
    auto reopenedGrid = reopened.child("studio.grid");
    CHECK((reopenedGrid->worldPosition() - grid->worldPosition()).length() < 1e-9);
    CHECK(reopenedGrid->children().size() == grid->children().size());
    v.document.play();
    v.stage->refresh();
    CHECK(grid->hidden);
    v.document.stop();
    v.stage->refresh();
    expectFollows("dopo Stop");
    std::size_t lines = grid->children().size();
    for (int i = 0; i < 20; ++i) {
        v.document.update(v.plate(), [i](WorldBlock& b) { b.x = i; }, std::string("Sposta"), std::string("trascina"));
        v.stage->refresh();
    }
    CHECK(grid->children().size() == lines);
    CHECK(std::count_if(v.stage->scene()->root->children().begin(), v.stage->scene()->root->children().end(), [](const render::NodePtr& n) { return n->name == "studio.grid"; }) == 1);
    v.document.select({v.plate()});
    v.document.deleteSelection();
    v.stage->refresh();
    CHECK(grid->hidden);
}

TEST_CASE("Studio: contorno di selezione", "[studio][selection]") {
    Viewport v;
    v.document.tool = StudioTool::select;
    struct Case {
        std::string name;
        Vec3 size, rotation;
        std::string kind = "cube";
    };
    std::vector<Case> cases = {{"cubo", {1, 1, 1}, {}}, {"rettangolare", {4, 1.2, 2}, {}}, {"sottile", {6, 0.1, 3}, {}}, {"grande", {30, 6, 20}, {}}, {"scalata", {8, 8, 8}, {}},
                               {"ruotata", {4, 1.2, 2}, {20, 35, 10}}, {"sfera", {3, 3, 3}, {}, "sphere"}};
    for (const auto& c : cases)
        for (double distance : {8.0, 25.0, 80.0}) {
            INFO(c.name << " a distanza " << distance);
            WorldBlock part;
            part.id = makeUuid();
            part.kind = c.kind;
            part.sizeX = c.size.x;
            part.sizeY = c.size.y;
            part.sizeZ = c.size.z;
            part.rotationX = c.rotation.x;
            part.rotationY = c.rotation.y;
            part.rotationZ = c.rotation.z;
            part.y = 6;
            v.document.world.blocks = {part};
            v.document.select({part.id});
            v.stage->orient(0.6, 0.35, distance + std::max({c.size.x, c.size.y, c.size.z}), {0, 6, 0});
            v.stage->refresh();
            auto node = v.child(part.id);
            REQUIRE(node);
            Bounds visual = node->geometry->bounds();
            auto object = box([&] {
                std::vector<Vec3> out;
                for (auto p : corners(node->world(), visual.low, visual.high)) out.push_back(v.screen(p));
                return out;
            }());
            auto highlights = v.child("studio.highlights");
            REQUIRE(highlights);
            REQUIRE_FALSE(highlights->children().empty());
            const auto& holder = highlights->children().front();
            CHECK_FALSE(holder->hidden);
            const auto& line = holder->children().front();
            Bounds edge = line->geometry->bounds();
            auto outline = box([&] {
                std::vector<Vec3> out;
                for (auto p : corners(line->world(), edge.low, edge.high)) out.push_back(v.screen(p));
                return out;
            }());
            for (int k = 0; k < 4; ++k) CHECK(std::abs(outline[static_cast<std::size_t>(k)] - object[static_cast<std::size_t>(k)]) < 0.5);
        }
    WorldBlock floating;
    floating.id = makeUuid();
    floating.sizeX = 4;
    floating.sizeY = 2;
    floating.sizeZ = 3;
    floating.y = 20;
    v.document.world.blocks = {floating};
    v.document.showGrid = false;
    v.document.setSelection(std::nullopt);
    v.stage->orient(0.5, -0.3, 14, {0, 20, 0});
    Pixels plain = v.shot();
    v.document.select({floating.id});
    Pixels selected = v.shot();
    v.document.world.blocks.clear();
    v.document.setSelection(std::nullopt);
    Pixels empty = v.shot();
    std::array<int, 4> silhouette{100000, 100000, -1, -1}, ring{100000, 100000, -1, -1};
    auto grow = [](std::array<int, 4>& b, int x, int y) {
        b[0] = std::min(b[0], x);
        b[1] = std::min(b[1], y);
        b[2] = std::max(b[2], x);
        b[3] = std::max(b[3], y);
    };
    for (int y = 0; y < plain.height; ++y)
        for (int x = 0; x < plain.width; ++x) {
            auto a = rgb(plain, x, y), b = rgb(empty, x, y), c = rgb(selected, x, y);
            if (std::abs(a[0] - b[0]) + std::abs(a[1] - b[1]) + std::abs(a[2] - b[2]) > 30) grow(silhouette, x, y);
            if (std::abs(a[0] - c[0]) + std::abs(a[1] - c[1]) + std::abs(a[2] - c[2]) > 60) grow(ring, x, y);
        }
    INFO("sagoma " << silhouette[0] << "," << silhouette[1] << " → " << silhouette[2] << "," << silhouette[3] << "; contorno " << ring[0] << "," << ring[1] << " → " << ring[2] << "," << ring[3]);
    REQUIRE(silhouette[2] > silhouette[0]);
    REQUIRE(ring[2] > ring[0]);
    for (int k = 0; k < 4; ++k) CHECK(std::abs(ring[static_cast<std::size_t>(k)] - silhouette[static_cast<std::size_t>(k)]) <= 2);
}

TEST_CASE("Gizmo: hover, hit test e cattura", "[studio][gizmo][hover]") {
    std::vector<std::string> failures;
    int verified = 0, ambiguous = 0;
    std::set<std::string> covered;
    for (auto tool : {StudioTool::move, StudioTool::scale, StudioTool::rotate}) {
        bool ring = tool == StudioTool::rotate;
        for (const auto& view : quadrants) {
            Viewport v;
            WorldBlock part;
            part.id = makeUuid();
            part.name = "Bersaglio";
            part.sizeX = 4;
            part.sizeY = 2;
            part.sizeZ = 3;
            part.y = 3;
            v.document.world.blocks.push_back(part);
            v.document.select({part.id});
            v.document.tool = tool;
            v.document.moveSnap = 0;
            v.document.angleSnap = 0;
            v.stage->orient(view.yaw, view.pitch, 22, {0, 3, 0});
            v.stage->refresh();
            auto gizmo = v.child("studio.gizmo");
            REQUIRE(gizmo);
            REQUIRE_FALSE(gizmo->hidden);
            std::vector<std::pair<std::pair<int, int>, std::vector<Sample>>> targets;
            for (const auto& holder : gizmo->children())
                if (auto handle = handleOf(holder->name)) targets.push_back({*handle, samples(*holder, ring)});
            for (const auto& [handle, points] : targets)
                for (const auto& sample : points) {
                    v.document.world.blocks.back() = part;
                    v.document.select({part.id});
                    v.stage->refresh();
                    std::ostringstream context;
                    context << (tool == StudioTool::move ? "Sposta" : tool == StudioTool::scale ? "Scala" : "Ruota") << " · " << view.name << " · " << "XYZ"[handle.first] << (handle.second > 0 ? "+" : "−") << " · " << sample.where;
                    Vec3 at = v.screen(sample.world);
                    if (at.x < 4 || at.y < 4 || at.x > v.size.width - 4 || at.y > v.size.height - 4) continue;
                    if (crowded(v, *gizmo, handle, at, ring)) {
                        ++ambiguous;
                        continue;
                    }
                    auto fail = [&](const std::string& what) { failures.push_back(context.str() + ": " + what); };
                    v.send(ui::PointerPhase::moved, 2, 2);
                    if (v.stage->gizmoState() != studio::StudioStage::GizmoState::idle) fail("fuori dal gizmo lo stato non è Idle");
                    v.send(ui::PointerPhase::moved, at);
                    if (v.stage->gizmoState() != studio::StudioStage::GizmoState::hover || v.stage->highlightedHandle() != handle) {
                        fail("nessun hover sulla maniglia");
                        continue;
                    }
                    Color lit = v.stage->handleColor(handle);
                    bool brighter = false;
                    for (const auto& holder : gizmo->children())
                        if (handleOf(holder->name) == handle) brighter = holder->children().front()->material->diffuse == lit;
                    ui::Color tone = handle.first == 0 ? ui::theme::stage::axisX : handle.first == 1 ? ui::theme::stage::axisY : ui::theme::stage::axisZ;
                    Color normal{tone.r, tone.g, tone.b, tone.a};
                    if (!brighter || lit.r + lit.g + lit.b <= normal.r + normal.g + normal.b + 0.2) fail("la maniglia non si evidenzia");
                    v.send(ui::PointerPhase::down, at);
                    if (v.stage->gizmoState() != studio::StudioStage::GizmoState::pressed) {
                        fail("il clic non cattura la maniglia");
                        v.send(ui::PointerPhase::up, at);
                        continue;
                    }
                    Vec3 centre = v.screen(gizmo->worldPosition());
                    Vec3 direction{at.x - centre.x, at.y - centre.y, 0};
                    if (ring) direction = {-(at.y - centre.y), at.x - centre.x, 0};
                    double length = std::hypot(direction.x, direction.y);
                    if (length < 1e-6) continue;
                    direction = direction / length;
                    Vec3 aside{-direction.y * 90, direction.x * 90, 0};
                    v.send(ui::PointerPhase::dragged, at.x + aside.x, at.y + aside.y);
                    if (v.stage->gizmoState() != studio::StudioStage::GizmoState::dragging || v.stage->highlightedHandle() != handle) fail("il trascinamento perde la maniglia uscendo dalla sua area");
                    for (int step = 1; step <= 6; ++step) v.send(ui::PointerPhase::dragged, at.x + direction.x * 10 * step + aside.x * (step % 2), at.y + direction.y * 10 * step + aside.y * (step % 2));
                    Vec3 end{at.x + direction.x * 60, at.y + direction.y * 60, 0};
                    v.send(ui::PointerPhase::dragged, end);
                    if (v.stage->highlightedHandle() != handle) fail("la maniglia attiva cambia durante il trascinamento");
                    v.send(ui::PointerPhase::up, end);
                    if (v.stage->gizmoState() == studio::StudioStage::GizmoState::pressed || v.stage->gizmoState() == studio::StudioStage::GizmoState::dragging) fail("il rilascio non chiude il trascinamento");
                    const WorldBlock& after = v.block(part.id);
                    int axis = handle.first;
                    if (tool == StudioTool::move) {
                        Vec3 delta = after.position() - part.position();
                        Vec3 along = component(delta, axis);
                        if (along.length() < 0.2) fail("la parte non si sposta");
                        if ((delta - along).length() > 1e-6) fail("spostamento fuori asse");
                        double sign = axis == 0 ? delta.x : axis == 1 ? delta.y : delta.z;
                        if (sign * handle.second <= 0) fail("verso dello spostamento sbagliato");
                    } else if (tool == StudioTool::scale) {
                        double before = axis == 0 ? part.sizeX : axis == 1 ? part.sizeY : part.sizeZ;
                        double now = axis == 0 ? after.sizeX : axis == 1 ? after.sizeY : after.sizeZ;
                        if (now <= before + 0.1) fail("la dimensione non cresce trascinando verso l’esterno");
                        for (int other = 0; other < 3; ++other)
                            if (other != axis && std::abs((other == 0 ? after.sizeX : other == 1 ? after.sizeY : after.sizeZ) - (other == 0 ? part.sizeX : other == 1 ? part.sizeY : part.sizeZ)) > 1e-6) fail("cambia anche un altro asse");
                    } else {
                        Mat3 turned = after.rotationMatrix();
                        Vec3 fixed{axis == 0 ? 1.0 : 0.0, axis == 1 ? 1.0 : 0.0, axis == 2 ? 1.0 : 0.0};
                        Vec3 image{turned[0][0] * fixed.x + turned[0][1] * fixed.y + turned[0][2] * fixed.z, turned[1][0] * fixed.x + turned[1][1] * fixed.y + turned[1][2] * fixed.z,
                                   turned[2][0] * fixed.x + turned[2][1] * fixed.y + turned[2][2] * fixed.z};
                        if ((image - fixed).length() > 1e-6) fail("la rotazione non è attorno all’asse dell’anello");
                        if (std::abs(after.rotationX - part.rotationX) + std::abs(after.rotationY - part.rotationY) + std::abs(after.rotationZ - part.rotationZ) < 2) fail("la parte non ruota");
                    }
                    if (!v.document.undoLabel()) fail("nessuna voce di Annulla");
                    ++verified;
                    covered.insert(std::string(tool == StudioTool::move ? "M" : tool == StudioTool::scale ? "S" : "R") + "XYZ"[axis] + (handle.second > 0 ? "+" : "-") + " " + sample.where.substr(0, 5));
                }
        }
    }
    for (const auto& failure : failures) UNSCOPED_INFO(failure);
    INFO("verificati " << verified << ", ambigui " << ambiguous << ", combinazioni " << covered.size());
    CHECK(failures.empty());
    CHECK(verified > 300);
    for (std::string tool : {"M", "S"})
        for (std::string axis : {"X", "Y", "Z"})
            for (std::string sign : {"+", "-"})
                for (std::string where : {"inizi", "centr", "fine ", "punta"}) {
                    INFO(tool << axis << sign << " " << where);
                    CHECK(covered.count(tool + axis + sign + " " + where) == 1);
                }
    for (std::string axis : {"X", "Y", "Z"}) CHECK(std::any_of(covered.begin(), covered.end(), [&](const std::string& c) { return c.rfind("R" + axis, 0) == 0; }));
}

TEST_CASE("Gizmo: priorità e assi paralleli alla camera", "[studio][gizmo][priority]") {
    Viewport v;
    WorldBlock target;
    target.id = makeUuid();
    target.sizeX = target.sizeY = target.sizeZ = 2;
    target.y = 3;
    WorldBlock blocker;
    blocker.id = makeUuid();
    blocker.sizeX = 1;
    blocker.sizeY = 6;
    blocker.sizeZ = 6;
    v.document.world.blocks.push_back(target);
    v.document.world.blocks.push_back(blocker);
    v.document.select({target.id});
    v.document.tool = StudioTool::move;
    v.document.moveSnap = 0;
    v.stage->orient(ld::pi / 2, 0.15, 22, {0, 3, 0});
    v.stage->refresh();
    auto gizmo = v.child("studio.gizmo");
    render::NodePtr xPlus;
    for (const auto& holder : gizmo->children())
        if (holder->name == "gizmo:0:1") xPlus = holder;
    REQUIRE(xPlus);
    Vec3 head = xPlus->children()[1]->worldPosition();
    v.document.world.blocks.back().x = head.x + 2;
    v.document.world.blocks.back().y = head.y;
    v.document.world.blocks.back().z = head.z;
    v.stage->refresh();
    Vec3 at = v.screen(xPlus->children()[1]->worldPosition());
    v.send(ui::PointerPhase::moved, at);
    CHECK(v.stage->highlightedHandle() == std::make_pair(0, 1));
    v.send(ui::PointerPhase::down, at);
    CHECK(v.stage->gizmoState() == studio::StudioStage::GizmoState::pressed);
    v.send(ui::PointerPhase::up, at);
    CHECK(v.document.selectedIds() == std::vector<std::string>{target.id});
    v.document.world.blocks.pop_back();
    v.stage->orient(0, 0.15, 22, {0, 3, 0});
    v.stage->refresh();
    Vec3 base = v.screen(xPlus->worldPosition());
    Vec3 tip = v.screen(xPlus->children()[1]->worldPosition());
    Vec3 axis{tip.x - base.x, tip.y - base.y, 0};
    Vec3 normal = Vec3{-axis.y, axis.x, 0}.normalized();
    Vec3 middle{(base.x + tip.x) / 2, (base.y + tip.y) / 2, 0};
    double shaft = 0;
    for (double offset = 0; offset < 40; offset += 0.5) {
        auto found = v.stage->handleAt({middle.x + normal.x * offset, middle.y + normal.y * offset});
        if (found == std::make_pair(0, 1)) shaft = offset;
    }
    CHECK(shaft >= 1);
    CHECK(shaft <= 1 + studio::StudioStage::handleTolerance + 2);
    v.stage->orient(ld::pi / 2 - 0.04, 0.03, 22, {0, 3, 0});
    v.stage->refresh();
    for (const auto& holder : gizmo->children()) {
        auto handle = handleOf(holder->name);
        if (!handle || handle->first != 0) continue;
        Vec3 p = v.screen(holder->children()[1]->worldPosition());
        auto found = v.stage->handleAt({p.x, p.y});
        INFO(holder->name);
        REQUIRE(found);
        CHECK(found->first == 0);
    }
}

TEST_CASE("Golden: viewport di Studio", "[studio][visual]") {
    Viewport v;
    v.document.tool = StudioTool::select;
    REQUIRE(v.document.addPart("cube"));
    std::string part = v.document.selectedIds().front();
    v.document.setSelection(std::nullopt);
    v.stage->orient(0.7, 0.45, 14, {0, 0.5, 0});
    Pixels scene = v.shot();
    compare(scene, "part e baseplate");
    Vec3 top = v.screen({0, 1, 0});
    Vec3 floor = v.screen({3.3, 0, -3.3});
    auto partTop = rgb(scene, static_cast<int>(top.x), static_cast<int>(top.y) + 2);
    auto plateTop = rgb(scene, static_cast<int>(floor.x), static_cast<int>(floor.y));
    INFO("parte " << partTop[0] << "," << partTop[1] << "," << partTop[2] << "; baseplate " << plateTop[0] << "," << plateTop[1] << "," << plateTop[2]);
    for (int k = 0; k < 3; ++k) CHECK(std::abs(partTop[static_cast<std::size_t>(k)] - plateTop[static_cast<std::size_t>(k)]) < 24);
    CHECK(partTop[2] - partTop[0] < 30);
    const WorldBlock& placed = v.block(part);
    v.stage->orient(0.75, 0.35, 2.5, {placed.x + 0.5, placed.y + 0.4, placed.z + 0.5});
    compare(v.shot(), "spigolo della part");
    v.stage->orient(0.75, 0.35, 2.5, {30, -0.1, 30});
    compare(v.shot(), "spigolo del baseplate");
    v.document.update(part, [](WorldBlock& b) {
        b.sizeX = 6;
        b.sizeY = 0.4;
        b.sizeZ = 3;
        b.rotationY = 25;
        b.y = 0.2;
    }, std::string("Ridimensiona"));
    v.document.select({part});
    v.stage->orient(0.9, 0.5, 12, {0, 0, 0});
    compare(v.shot(), "part ruotata e scalata con contorno");
    v.document.update(v.plate(), [](WorldBlock& b) {
        b.x = 10;
        b.z = 6;
    }, std::string("Sposta"));
    v.document.setSelection(std::nullopt);
    v.stage->orient(0.7, 0.6, 50, {10, 0, 6});
    compare(v.shot(), "griglia dopo lo spostamento del baseplate");
    v.document.update(v.plate(), [](WorldBlock& b) {
        b.x = 0;
        b.z = 0;
    }, std::string("Sposta"));
    for (auto tool : {StudioTool::move, StudioTool::scale}) {
        std::string name = tool == StudioTool::move ? "sposta" : "scala";
        v.document.tool = tool;
        v.document.select({part});
        v.stage->orient(0.7, 0.45, 14, {0, 0.5, 0});
        v.send(ui::PointerPhase::moved, 2, 2);
        compare(v.shot(), "gizmo " + name);
        auto gizmo = v.child("studio.gizmo");
        for (int axis = 0; axis < 3; ++axis) {
            if (tool == StudioTool::scale && axis != 0) continue;
            for (const auto& holder : gizmo->children())
                if (holder->name == "gizmo:" + std::to_string(axis) + ":1") {
                    Vec3 p = v.screen(holder->children()[1]->worldPosition());
                    v.send(ui::PointerPhase::moved, p);
                }
            CHECK(v.stage->highlightedHandle() == std::make_pair(axis, 1));
            compare(v.shot(), "gizmo " + name + " hover " + std::string(1, "XYZ"[axis]));
        }
    }
}
