#include "Engine/Core/Rotation.hpp"
#include "Golden.hpp"
#include "Studio/StudioStage.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <set>
#include <sstream>

using namespace ld;

namespace {

struct Camera {
    std::string name;
    double yaw, pitch;
};

const std::vector<Camera> cameras = {{"davanti", 0, 0.25}, {"dietro", pi, 0.25}, {"sinistra", -pi / 2, 0.25}, {"destra", pi / 2, 0.25}, {"sopra", 0.3, 1.35}, {"sotto", 0.4, -0.7}};

struct Handle {
    int axis = 0, sign = 1;
    Vec3 tip, base, direction;
};

WorldBlock block(const std::string& name, double x, double y, double z, Vec3 size, Vec3 rotation = {}) {
    WorldBlock b;
    b.id = makeUuid();
    b.name = name;
    b.x = x; b.y = y; b.z = z;
    b.sizeX = size.x; b.sizeY = size.y; b.sizeZ = size.z;
    b.rotationX = rotation.x; b.rotationY = rotation.y; b.rotationZ = rotation.z;
    return b;
}

Vec3 axisOf(const WorldBlock& b, int index) {
    Mat3 m = b.rotationMatrix();
    return {m[0][index], m[1][index], m[2][index]};
}

double component(Vec3 v, int i) { return i == 0 ? v.x : i == 1 ? v.y : v.z; }

std::vector<Handle> handles(const studio::StudioStage& stage) {
    std::vector<Handle> out;
    auto gizmo = stage.scene()->root->child("studio.gizmo");
    if (!gizmo || gizmo->hidden) return out;
    for (const auto& holder : gizmo->children()) {
        const std::string& name = holder->name;
        if (name.rfind("gizmo:", 0) != 0 || holder->children().size() < 2) continue;
        auto colon = name.find(':', 6);
        Handle h;
        h.axis = std::stoi(name.substr(6, colon - 6));
        h.sign = std::stoi(name.substr(colon + 1));
        h.tip = holder->children()[1]->worldPosition();
        h.base = holder->worldPosition();
        h.direction = (h.tip - holder->worldPosition()).normalized();
        out.push_back(h);
    }
    return out;
}

struct Fixture {
    StudioDocument document;
    studio::StudioStage stage;
    Fixture() : stage(document) {}
};

Vec3 screen(const studio::StudioStage& stage, Vec3 world) { return render::project(*stage.camera(), {stage.size.width, stage.size.height}, world); }

bool drag(studio::StudioStage& stage, Vec3 from, Vec3 to) {
    Vec3 a = screen(stage, from), b = screen(stage, to);
    auto visible = [&](Vec3 p) { return p.z > -1 && p.z < 1 && p.x >= 0 && p.y >= 0 && p.x <= stage.size.width && p.y <= stage.size.height; };
    if (std::hypot(b.x - a.x, b.y - a.y) < 12 || !visible(a) || !visible(b)) return false;
    ui::Size size{stage.size.width, stage.size.height};
    auto event = [](ui::PointerPhase phase, double x, double y) {
        ui::PointerEvent e;
        e.phase = phase;
        e.location = {x, y};
        return e;
    };
    stage.pointer(event(ui::PointerPhase::down, a.x, a.y), size);
    for (int step = 1; step <= 6; ++step) {
        double t = step / 6.0;
        stage.pointer(event(ui::PointerPhase::dragged, a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t), size);
    }
    stage.pointer(event(ui::PointerPhase::up, b.x, b.y), size);
    return true;
}

}

TEST_CASE("Gizmo: Scala e Sposta da ogni camera", "[studio][gizmo][transform]") {
    golden::registry();
    Fixture f;
    Experience project;
    project.id = makeUuid();
    REQUIRE(f.document.open(project));
    f.stage.size = {900, 700};
    f.document.moveSnap = 0.25;
    std::vector<std::pair<std::string, std::vector<WorldBlock>>> fixtures = {
        {"parte", {block("A", 0, 3, 0, {4, 2, 3})}},
        {"parte ruotata", {block("A", 0, 3, 0, {4, 2, 3}, {0, 90, 30})}},
        {"selezione multipla", {block("A", -2, 3, 0, {2, 2, 2}), block("B", 3, 4, 1, {2, 4, 2})}}};
    std::vector<std::string> failures;
    int checked = 0, skipped = 0, ambiguous = 0;
    for (auto tool : {StudioTool::scale, StudioTool::move})
        for (const auto& [label, parts] : fixtures)
            for (const auto& camera : cameras)
                for (auto [amount, verse] : std::vector<std::pair<double, std::string>>{{1.0, "verso l’esterno"}, {-0.5, "verso l’interno"}}) {
                    f.document.world.blocks = parts;
                    f.document.world.instances.clear();
                    f.document.tool = tool;
                    std::vector<std::string> ids;
                    for (const auto& p : parts) ids.push_back(p.id);
                    f.document.select(ids);
                    Experience before = f.document.world;
                    f.stage.orient(camera.yaw, camera.pitch, 22, {0, 3, 0});
                    f.stage.refresh();
                    for (const auto& handle : handles(f.stage)) {
                        f.document.world = before;
                        f.document.select(ids);
                        f.stage.refresh();
                        auto all = handles(f.stage);
                        auto current = std::find_if(all.begin(), all.end(), [&](const Handle& h) { return h.axis == handle.axis && h.sign == handle.sign; });
                        if (current == all.end()) continue;
                        Vec3 direction = current->direction;
                        Vec3 target = current->tip + direction * amount;
                        auto undoBefore = f.document.undoLabel();
                        Vec3 at = screen(f.stage, current->tip);
                        bool crowded = std::any_of(all.begin(), all.end(), [&](const Handle& other) {
                            if (other.axis == current->axis && other.sign == current->sign) return false;
                            Vec3 p = screen(f.stage, other.base), q = screen(f.stage, other.tip);
                            double dx = q.x - p.x, dy = q.y - p.y, length = dx * dx + dy * dy;
                            double t = length > 1e-9 ? std::clamp(((at.x - p.x) * dx + (at.y - p.y) * dy) / length, 0.0, 1.0) : 0;
                            return std::hypot(at.x - (p.x + dx * t), at.y - (p.y + dy * t)) < 16;
                        });
                        if (crowded) {
                            ++ambiguous;
                            continue;
                        }
                        if (!drag(f.stage, current->tip, target)) {
                            ++skipped;
                            continue;
                        }
                        ++checked;
                        std::ostringstream context;
                        context << (tool == StudioTool::scale ? "Scala" : "Sposta") << " · " << label << " · camera " << camera.name << " · maniglia " << "XYZ"[handle.axis] << (handle.sign > 0 ? "+" : "−") << " " << verse;
                        const auto& after = f.document.world.blocks;
                        auto fail = [&](const std::string& what) { failures.push_back(context.str() + ": " + what); };
                        if (tool == StudioTool::scale) {
                            if (parts.size() == 1) {
                                const WorldBlock &old = parts[0], &now = after[0];
                                int index = handle.axis;
                                double oldSize = component({old.sizeX, old.sizeY, old.sizeZ}, index), newSize = component({now.sizeX, now.sizeY, now.sizeZ}, index);
                                if (std::abs(std::abs(dot(axisOf(old, index), direction)) - 1) > 0.001) fail("la maniglia non segue l’asse locale della parte");
                                if (std::abs((newSize - oldSize) - amount) > 0.13) fail("dimensione " + std::to_string(oldSize) + " → " + std::to_string(newSize));
                                double oppositeOld = dot(old.position(), direction) - oldSize / 2, oppositeNew = dot(now.position(), direction) - newSize / 2;
                                if (std::abs(oppositeNew - oppositeOld) > 0.02) fail("la faccia opposta si sposta di " + std::to_string(oppositeNew - oppositeOld));
                            } else {
                                auto oldBox = StudioDocument::bounds(parts), newBox = StudioDocument::bounds(after);
                                REQUIRE(oldBox);
                                REQUIRE(newBox);
                                auto span = [&](const Bounds& box) {
                                    double low = component(box.low, handle.axis), high = component(box.high, handle.axis);
                                    return handle.sign > 0 ? std::make_pair(low, high) : std::make_pair(-high, -low);
                                };
                                auto [oldAnchor, oldFace] = span(*oldBox);
                                auto [newAnchor, newFace] = span(*newBox);
                                if (std::abs((newFace - newAnchor) - (oldFace - oldAnchor) - amount) > 0.13) fail("estensione del riquadro errata");
                                if (std::abs(newAnchor - oldAnchor) > 0.02) fail("il lato opposto del riquadro si sposta");
                                double oldGap = std::abs(component(parts[1].position() - parts[0].position(), handle.axis));
                                double newGap = std::abs(component(after[1].position() - after[0].position(), handle.axis));
                                if ((amount > 0) != (newGap > oldGap)) fail("la distanza fra gli oggetti non segue la scala");
                            }
                        } else {
                            for (std::size_t i = 0; i < parts.size(); ++i) {
                                Vec3 moved = after[i].position() - parts[i].position();
                                double along = dot(moved, direction);
                                Vec3 across = moved - direction * along;
                                if (std::abs(along - amount) > 0.13 || std::abs(across.x) + std::abs(across.y) + std::abs(across.z) > 0.02) fail("spostamento errato");
                                if (after[i].sizeX != parts[i].sizeX || after[i].sizeY != parts[i].sizeY || after[i].sizeZ != parts[i].sizeZ) fail("Sposta cambia la dimensione");
                            }
                        }
                        if (f.document.undoLabel() == undoBefore || !f.document.undoLabel()) fail("nessuna voce di Undo");
                        f.document.undo();
                        if (!(f.document.world.blocks == before.blocks)) fail("l’Undo non ripristina con una sola operazione");
                    }
                }
    for (const auto& failure : failures) UNSCOPED_INFO(failure);
    INFO(checked << " trascinamenti verificati, " << skipped << " esclusi per asse quasi parallelo alla vista, " << ambiguous << " con maniglia sovrapposta");
    CHECK(failures.empty());
    CHECK(checked > 200);

    f.document.moveSpace = TransformSpace::local;
    WorldBlock turned = block("A", 0, 3, 0, {4, 2, 3}, {20, 90, 30});
    std::vector<std::string> local;
    for (const auto& camera : cameras) {
        f.document.world.blocks = {turned};
        f.document.tool = StudioTool::move;
        f.document.select({turned.id});
        f.stage.orient(camera.yaw, camera.pitch, 22, {0, 3, 0});
        f.stage.refresh();
        for (const auto& handle : handles(f.stage)) {
            f.document.world.blocks = {turned};
            f.document.select({turned.id});
            f.stage.refresh();
            auto all = handles(f.stage);
            auto current = std::find_if(all.begin(), all.end(), [&](const Handle& h) { return h.axis == handle.axis && h.sign == handle.sign; });
            if (current == all.end()) continue;
            std::string context = "Sposta locale · camera " + camera.name + " · maniglia " + std::string(1, "XYZ"[handle.axis]) + (handle.sign > 0 ? "+" : "−");
            if (std::abs(std::abs(dot(axisOf(turned, handle.axis), current->direction)) - 1) > 0.001) local.push_back(context + ": la maniglia non segue l’asse locale");
            if (!drag(f.stage, current->tip, current->tip + current->direction)) continue;
            Vec3 delta = f.document.world.blocks[0].position() - turned.position();
            double along = dot(delta, current->direction);
            if (std::abs(along - 1) > 0.13 || std::abs(dot(delta, delta) - along * along) > 0.001) local.push_back(context + ": spostamento fuori asse");
            f.document.undo();
        }
    }
    for (const auto& failure : local) UNSCOPED_INFO(failure);
    CHECK(local.empty());
    f.document.moveSpace = TransformSpace::world;

    WorldBlock rendered = block("A", 0, 3, 0, {4, 2, 3}, {0, 90, 30});
    auto node = render::scene_factory::block(rendered);
    render::scene_factory::apply(rendered, *node);
    for (int index = 0; index < 3; ++index) {
        Vec3 unit{index == 0 ? 1.0 : 0.0, index == 1 ? 1.0 : 0.0, index == 2 ? 1.0 : 0.0};
        Vec3 tip = node->orientation.rotate(unit);
        CHECK(std::abs(dot(tip.normalized(), axisOf(rendered, index)) - 1) < 0.001);
    }
}

TEST_CASE("Gizmo: Ruota da ogni camera", "[studio][gizmo][transform][rotate]") {
    golden::registry();
    Fixture f;
    Experience project;
    project.id = makeUuid();
    REQUIRE(f.document.open(project));
    f.stage.size = {900, 700};
    f.document.angleSnap = 15;
    f.document.tool = StudioTool::rotate;
    std::vector<std::pair<std::string, std::vector<WorldBlock>>> fixtures = {
        {"parte", {block("A", 0, 3, 0, {4, 2, 3})}},
        {"parte ruotata", {block("A", 0, 3, 0, {4, 2, 3}, {0, 30, 0})}},
        {"parte inclinata", {block("A", 0, 3, 0, {4, 2, 3}, {20, 40, 10})}},
        {"selezione multipla", {block("A", -2, 3, 0, {2, 2, 2}), block("B", 3, 3, 1, {2, 2, 2})}}};
    auto ringPoint = [](int axis, Vec3 centre, double radius, double degrees) {
        double a = degrees * pi / 180;
        Vec3 local = axis == 0 ? Vec3{0, std::cos(a), std::sin(a)} : axis == 1 ? Vec3{std::sin(a), 0, std::cos(a)} : Vec3{std::cos(a), std::sin(a), 0};
        return centre + local * radius;
    };
    auto planeAngle = [](int axis, Vec3 v) {
        if (axis == 0) return std::atan2(v.z, v.y) * 180 / pi;
        if (axis == 1) return std::atan2(v.x, v.z) * 180 / pi;
        return std::atan2(v.y, v.x) * 180 / pi;
    };
    auto wrap = [](double degrees) { return std::remainder(degrees, 360.0); };
    std::vector<std::string> failures;
    int checked = 0;
    std::set<std::string> covered;
    for (const auto& [label, parts] : fixtures)
        for (const auto& camera : cameras)
            for (int axis = 0; axis < 3; ++axis)
                for (double amount : {30.0, -30.0}) {
                    f.document.world.blocks = parts;
                    f.document.world.instances.clear();
                    std::vector<std::string> ids;
                    for (const auto& p : parts) ids.push_back(p.id);
                    f.document.select(ids);
                    Experience before = f.document.world;
                    f.stage.orient(camera.yaw, camera.pitch, 22, {0, 3, 0});
                    f.stage.refresh();
                    auto gizmo = f.stage.scene()->root->child("studio.gizmo");
                    REQUIRE(gizmo);
                    REQUIRE_FALSE(gizmo->hidden);
                    Vec3 centre = gizmo->worldPosition();
                    double radius = gizmo->scale.x;
                    Vec3 eye = f.stage.camera()->worldPosition();
                    Vec3 normal = axis == 0 ? Vec3{1, 0, 0} : axis == 1 ? Vec3{0, 1, 0} : Vec3{0, 0, 1};
                    if (std::abs(dot(normal, (centre - eye).normalized())) < 0.3) continue;
                    std::optional<double> start;
                    for (double candidate = 0; candidate < 360 && !start; candidate += 15) {
                        Vec3 a = screen(f.stage, ringPoint(axis, centre, radius, candidate)), b = screen(f.stage, ringPoint(axis, centre, radius, candidate + amount));
                        bool clear = true;
                        for (int other = 0; other < 3 && clear; ++other) {
                            if (other == axis) continue;
                            for (double probe = 0; probe < 360 && clear; probe += 5) {
                                Vec3 p = screen(f.stage, ringPoint(other, centre, radius, probe));
                                if (std::hypot(p.x - a.x, p.y - a.y) < 14) clear = false;
                            }
                        }
                        if (clear && std::hypot(b.x - a.x, b.y - a.y) > 20) start = candidate;
                    }
                    if (!start) continue;
                    auto undoBefore = f.document.undoLabel();
                    if (!drag(f.stage, ringPoint(axis, centre, radius, *start), ringPoint(axis, centre, radius, *start + amount))) continue;
                    ++checked;
                    covered.insert(std::string(1, "XYZ"[axis]) + (amount > 0 ? "+" : "-") + " " + camera.name);
                    std::ostringstream context;
                    context << "Ruota · " << label << " · camera " << camera.name << " · asse " << "XYZ"[axis] << (amount > 0 ? " +" : " ") << amount << "°";
                    auto fail = [&](const std::string& what) { failures.push_back(context.str() + ": " + what); };
                    const auto& after = f.document.world.blocks;
                    if (parts.size() == 1) {
                        Mat3 expected = rotation::axis(axis, amount) * parts[0].rotationMatrix(), actual = after[0].rotationMatrix();
                        double error = 0;
                        for (int row = 0; row < 3; ++row)
                            for (int column = 0; column < 3; ++column) error = std::max(error, std::abs(expected[row][column] - actual[row][column]));
                        if (error > 0.0005) fail("la parte non ruota attorno all’asse del mondo mostrato dall’anello (errore " + std::to_string(error) + ")");
                        if (!(after[0].position() == parts[0].position())) fail("la rotazione sposta la parte");
                    } else {
                        double turned = wrap(planeAngle(axis, after[1].position() - after[0].position()) - planeAngle(axis, parts[1].position() - parts[0].position()));
                        if (std::abs(turned - amount) > 0.01) fail("il gruppo ruota di " + std::to_string(turned));
                        double oldGap = (parts[1].position() - parts[0].position()).length(), newGap = (after[1].position() - after[0].position()).length();
                        if (std::abs(oldGap - newGap) > 0.001) fail("la distanza fra gli oggetti cambia");
                    }
                    if (f.document.undoLabel() == undoBefore || !f.document.undoLabel()) fail("nessuna voce di Undo");
                    f.document.undo();
                    if (!(f.document.world.blocks == before.blocks)) fail("l’Undo non ripristina con una sola operazione");
                    f.document.redo();
                    if (!(f.document.world.blocks == after)) fail("il Redo non ripete la rotazione");
                    f.document.undo();
                }
    for (const auto& failure : failures) UNSCOPED_INFO(failure);
    INFO(checked << " rotazioni verificate");
    CHECK(failures.empty());
    CHECK(checked >= 55);
    for (const char* sign : {"+", "-"})
        for (char axis : std::string("XYZ")) CHECK(std::any_of(covered.begin(), covered.end(), [&](const std::string& c) { return c[0] == axis && c.substr(1, 1) == sign; }));
}
