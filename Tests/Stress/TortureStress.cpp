#include "Engine/Audio/Audio.hpp"
#include "Engine/Backend/Assets.hpp"
#include "Engine/Character/Character.hpp"
#include "Engine/Core/Png.hpp"
#include "Engine/Rendering/World.hpp"
#include "Engine/Scripting/WorldSession.hpp"
#include "Engine/Terrain/Terrain.hpp"
#include "Interface/Canvas.hpp"
#include "Interface/Surface.hpp"
#include "Lim/Compiler/Compiler.hpp"
#include "Lim/Parser/Parser.hpp"
#include "Lim/Tooling/Tooling.hpp"
#include "Rig.hpp"
#include "Studio/StudioStage.hpp"
#include "Studio/StudioView.hpp"

#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>

#include <cmath>
#include <fstream>
#include <iterator>
#include <map>
#include <random>
#include <set>

namespace {

using studio::StudioStage;

struct Bench {
    std::filesystem::path folder = std::filesystem::temp_directory_path() / ("Ludissey Tortura " + ld::makeUuid());
    std::shared_ptr<ld::Store> store;
    std::unique_ptr<studio::StudioShell> shell;
    std::unique_ptr<ui::Surface> surface;

    Bench() {
        stress::prepare();
        store = stress::account(folder);
        shell = std::make_unique<studio::StudioShell>(store);
        ld::Experience world = ld::Experience::blank();
        world.id = ld::makeUuid();
        REQUIRE(shell->document.open(world));
        surface = std::make_unique<ui::Surface>([this] { return shell->build(); });
        surface->resize({1180, 760}, 1);
        draw();
        REQUIRE(shell->stage);
        stage().size = {900, 640};
        stage().refresh();
    }

    ~Bench() {
        shell->invalidated = nullptr;
        surface.reset();
        shell.reset();
        store.reset();
        std::error_code ignored;
        std::filesystem::remove_all(folder, ignored);
    }

    ld::StudioDocument& document() { return shell->document; }
    StudioStage& stage() { return *shell->stage; }

    void draw() {
        ui::Canvas canvas(1180, 760, 1);
        surface->render(canvas);
    }

    void pointer(ui::PointerPhase phase, ui::Point at) {
        ui::PointerEvent event;
        event.phase = phase;
        event.location = at;
        stage().pointer(event, stage().size);
    }

    const ld::WorldBlock& block(const std::string& id) {
        for (const auto& b : document().world.blocks)
            if (b.id == id) return b;
        FAIL("Part assente: " << id);
        return document().world.blocks.front();
    }

    std::string part(const char* kind, double x, double y, double z, double sx, double sy, double sz) {
        REQUIRE(document().addPart(kind));
        std::string id = document().selectedIds().front();
        document().update(id, [=](ld::WorldBlock& b) {
            b.x = x;
            b.y = y;
            b.z = z;
            b.sizeX = sx;
            b.sizeY = sy;
            b.sizeZ = sz;
        });
        return id;
    }

    ui::Point screen(ld::Vec3 world) {
        ld::Vec3 p = ld::render::project(*stage().camera(), {stage().size.width, stage().size.height}, world);
        return {p.x, p.y};
    }

    std::map<std::pair<int, int>, ui::Point> handles(stress::Watchdog& watchdog) {
        std::map<std::pair<int, int>, ui::Point> found;
        auto gizmo = stage().scene()->root->child("studio.gizmo");
        if (!gizmo || gizmo->hidden) return found;
        ui::Point centre = screen(gizmo->worldPosition());
        const std::size_t expected = document().tool == ld::StudioTool::rotate ? 3 : 6;
        for (double radius = 6; radius < 220; radius += 3) {
            for (int k = 0; k < 96; ++k) {
                double angle = k * 2 * ld::pi / 96;
                ui::Point at{centre.x + std::cos(angle) * radius, centre.y + std::sin(angle) * radius};
                if (auto handle = stage().handleAt(at); handle && !found.count(*handle)) found[*handle] = at;
                if (found.size() == expected) return found;
            }
            watchdog.beat();
        }
        return found;
    }

    ui::Point gizmoCentre() { return screen(stage().scene()->root->child("studio.gizmo")->worldPosition()); }
};

bool onlyAxis(const ld::WorldBlock& a, const ld::WorldBlock& b, int axis) {
    const double pa[3] = {a.x, a.y, a.z}, pb[3] = {b.x, b.y, b.z};
    const double sa[3] = {a.sizeX, a.sizeY, a.sizeZ}, sb[3] = {b.sizeX, b.sizeY, b.sizeZ};
    const double ra[3] = {a.rotationX, a.rotationY, a.rotationZ}, rb[3] = {b.rotationX, b.rotationY, b.rotationZ};
    for (int i = 0; i < 3; ++i)
        if (i != axis && (std::abs(pa[i] - pb[i]) > 1e-9 || std::abs(sa[i] - sb[i]) > 1e-9 || std::abs(ra[i] - rb[i]) > 1e-9)) return false;
    return true;
}

std::string png(int width, int height, std::uint8_t shade) {
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4, shade);
    for (std::size_t i = 3; i < rgba.size(); i += 4) rgba[i] = 255;
    auto encoded = ld::encodePng(rgba, width, height);
    REQUIRE(encoded);
    return *encoded;
}

void jpeg(const std::filesystem::path& file, int width, int height) {
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4, 140);
    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef context = CGBitmapContextCreate(rgba.data(), static_cast<size_t>(width), static_cast<size_t>(height), 8, static_cast<size_t>(width) * 4, space, static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedLast));
    CGImageRef image = CGBitmapContextCreateImage(context);
    std::string path = file.string();
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(nullptr, reinterpret_cast<const UInt8*>(path.c_str()), static_cast<CFIndex>(path.size()), false);
    CGImageDestinationRef destination = CGImageDestinationCreateWithURL(url, CFSTR("public.jpeg"), 1, nullptr);
    CGImageDestinationAddImage(destination, image, nullptr);
    REQUIRE(CGImageDestinationFinalize(destination));
    CFRelease(destination);
    CFRelease(url);
    CGImageRelease(image);
    CGContextRelease(context);
    CGColorSpaceRelease(space);
}

void write(const std::filesystem::path& file, const std::string& data) { std::ofstream(file, std::ios::binary) << data; }

std::size_t files(const std::filesystem::path& folder) {
    std::size_t count = 0;
    std::error_code ignored;
    for (auto it = std::filesystem::recursive_directory_iterator(folder, ignored); it != std::filesystem::recursive_directory_iterator(); it.increment(ignored))
        if (it->is_regular_file()) ++count;
    return count;
}

class Speakers final : public ld::audio::Output {
public:
    std::map<std::string, ld::audio::Playback> active;
    int started = 0, stopped = 0;
    bool play(const std::string& id, const ld::audio::Playback& playback) override {
        active[id] = playback;
        ++started;
        return true;
    }
    void update(const std::string& id, const ld::audio::Playback& playback) override { active[id] = playback; }
    bool playing(const std::string& id) override { return active.count(id) > 0; }
    void stop(const std::string& id) override {
        if (active.erase(id)) ++stopped;
    }
};

}

TEST_CASE("Tortura: gizmo", "[stress][torture][gizmo]") {
    Bench bench;
    stress::Watchdog watchdog("Tortura del gizmo", 30);
    auto& document = bench.document();
    std::string straight = bench.part("cube", 0, 2, 0, 2, 2, 2);
    std::string turned = bench.part("cube", 0, 2, 0, 3, 1, 2);
    document.update(turned, [](ld::WorldBlock& b) {
        b.rotationY = 30;
        b.rotationX = 12;
    });
    int target = stress::scaled(240, 1020, 2000);
    int manipulations = 0, lostCapture = 0, wrongAxis = 0, wrongHighlight = 0, unchanged = 0, undoMismatch = 0, redoMismatch = 0, selectionLost = 0, snapped = 0, misSnapped = 0;
    std::map<std::string, int> perTool;
    stress::Distribution events;
    std::mt19937 random(20261004);
    const ld::StudioTool tools[3] = {ld::StudioTool::move, ld::StudioTool::scale, ld::StudioTool::rotate};
    for (int round = 0; manipulations < target; ++round) {
        REQUIRE(round < target * 4);
        std::string id = round % 4 == 3 ? turned : straight;
        document.update(id, [&](ld::WorldBlock& b) {
            b.x = 0;
            b.y = 2;
            b.z = 0;
            b.sizeX = id == straight ? 2 : 3;
            b.sizeY = id == straight ? 2 : 1;
            b.sizeZ = 2;
            if (id == straight) b.rotationX = b.rotationY = b.rotationZ = 0;
        });
        document.select({id});
        ld::StudioTool tool = tools[round % 3];
        document.tool = tool;
        double snap = round % 5 == 4 ? 0.5 : 0;
        document.moveSnap = snap;
        document.angleSnap = 0;
        bench.stage().orient(0.35 + (random() % 628) / 100.0, 0.25 + (random() % 80) / 100.0, 16 + random() % 10, {0, 2, 0});
        bench.stage().refresh();
        auto points = bench.handles(watchdog);
        if (points.empty()) continue;
        ui::Point centre = bench.gizmoCentre();
        for (const auto& [handle, at] : points) {
            if (manipulations >= target) break;
            ld::WorldBlock before = bench.block(id);
            double begun = stress::now();
            bench.pointer(ui::PointerPhase::moved, at);
            events.add(stress::now() - begun);
            if (bench.stage().highlightedHandle() != std::optional<std::pair<int, int>>(handle)) ++wrongHighlight;
            bench.pointer(ui::PointerPhase::down, at);
            double rx = at.x - centre.x, ry = at.y - centre.y, length = std::max(1e-6, std::hypot(rx, ry));
            double dx = rx / length, dy = ry / length;
            if (tool == ld::StudioTool::rotate) std::swap(dx, dy), dx = -dx;
            int steps = 4 + static_cast<int>(random() % 10);
            bool leave = manipulations % 3 == 0, rapid = manipulations % 4 == 1;
            double reach = (rapid ? 140 : 60) * (manipulations % 2 ? 1 : -1);
            for (int s = 1; s <= steps; ++s) {
                double t = static_cast<double>(s) / steps;
                double far = leave && s == steps ? 3 : 1;
                ui::Point to{at.x + dx * reach * t * far + (rapid ? std::sin(s * 1.3) * 6 : 0), at.y + dy * reach * t * far};
                begun = stress::now();
                bench.pointer(ui::PointerPhase::dragged, to);
                events.add(stress::now() - begun);
                auto state = bench.stage().gizmoState();
                if (state != StudioStage::GizmoState::dragging && state != StudioStage::GizmoState::pressed) ++lostCapture;
                if (bench.stage().highlightedHandle() != std::optional<std::pair<int, int>>(handle)) ++wrongHighlight;
            }
            bench.pointer(ui::PointerPhase::up, {at.x + dx * reach, at.y + dy * reach});
            watchdog.beat();
            ++manipulations;
            ++perTool[std::string(ld::toolTitle(tool)) + " asse " + std::to_string(handle.first) + (handle.second > 0 ? "+" : "-")];
            ld::WorldBlock after = bench.block(id);
            if (document.selectedIds() != std::vector<std::string>{id}) ++selectionLost;
            if (after == before) {
                ++unchanged;
                continue;
            }
            if (id == straight) {
                bool ok = onlyAxis(before, after, handle.first);
                if (tool == ld::StudioTool::rotate) ok = ok && before.x == after.x && before.y == after.y && before.z == after.z && before.sizeX == after.sizeX && before.sizeY == after.sizeY && before.sizeZ == after.sizeZ;
                if (!ok) ++wrongAxis;
                if (tool == ld::StudioTool::move && snap > 0) {
                    const double pb[3] = {before.x, before.y, before.z}, pa[3] = {after.x, after.y, after.z};
                    double moved = (pa[handle.first] - pb[handle.first]) / snap;
                    ++snapped;
                    if (std::abs(moved - std::round(moved)) > 1e-6) ++misSnapped;
                }
            }
            document.undo();
            if (!(bench.block(id) == before)) ++undoMismatch;
            document.redo();
            if (!(bench.block(id) == after)) ++redoMismatch;
            document.undo();
        }
    }
    ld::Json coverage = ld::Json::object();
    for (const auto& [name, count] : perTool) coverage.set(name, count);
    stress::record("tortura gizmo", "manipolazioni", manipulations);
    stress::record("tortura gizmo", "per strumento e asse", coverage);
    stress::record("tortura gizmo", "evento puntatore", events);
    stress::record("tortura gizmo", "anomalie", ld::Json::object({{"cattura persa", lostCapture}, {"asse sbagliato", wrongAxis}, {"evidenziazione sbagliata", wrongHighlight}, {"nessuna modifica", unchanged}, {"annulla diverso", undoMismatch}, {"ripeti diverso", redoMismatch}, {"selezione persa", selectionLost}, {"spostamenti con snap", snapped}, {"snap errati", misSnapped}}));
    INFO(coverage.dump(true));
    INFO("cattura persa " << lostCapture << ", asse sbagliato " << wrongAxis << ", evidenziazione " << wrongHighlight << ", invariati " << unchanged << ", annulla " << undoMismatch << ", ripeti " << redoMismatch << ", selezione " << selectionLost << ", snap errati " << misSnapped << " su " << snapped << "; evento p99 " << events.percentile(0.99) * 1000 << " ms, peggiore " << events.worst() * 1000);
    CHECK(manipulations >= target);
    CHECK(perTool.size() == 15);
    CHECK(lostCapture == 0);
    CHECK(wrongAxis == 0);
    CHECK(wrongHighlight == 0);
    CHECK(unchanged <= manipulations / 50);
    CHECK(undoMismatch == 0);
    CHECK(redoMismatch == 0);
    CHECK(selectionLost == 0);
    CHECK(misSnapped == 0);
    TIMED_CHECK(events.percentile(0.99) < 0.008);
}

TEST_CASE("Tortura: selezione", "[stress][torture][selection]") {
    Bench bench;
    stress::Watchdog watchdog("Tortura della selezione", 30);
    auto& document = bench.document();
    std::string baseplate;
    for (const auto& b : document.world.blocks)
        if (b.name == ld::rules::baseplate::name) baseplate = b.id;
    REQUIRE_FALSE(baseplate.empty());
    std::vector<std::string> targets{bench.part("cube", -9, 0.6, -6, 0.4, 0.4, 0.4), bench.part("cube", 9, 2, -6, 6, 3, 4), bench.part("cube", -9, 2, 6, 0.1, 3, 3), bench.part("cube", 9, 1.5, 6, 2.5, 2.5, 2.5), bench.part("sphere", 0, 1.5, -10, 2, 2, 2)};
    document.update(targets[3], [](ld::WorldBlock& b) {
        b.rotationY = 37;
        b.rotationX = 20;
    });
    std::string locked = targets[4];
    document.update(locked, [](ld::WorldBlock& b) { b.locked = true; });
    targets.push_back(baseplate);
    bench.stage().orient(0.2, 1.15, 34, {0, 0, 0});
    bench.stage().refresh();
    std::map<std::string, ui::Point> aim;
    for (const auto& id : targets) {
        const auto& b = bench.block(id);
        aim[id] = id == baseplate ? bench.screen({0, b.y + b.sizeY / 2, 4}) : bench.screen({b.x, b.y, b.z});
    }
    int clicks = stress::scaled(240, 1020, 3000), wrong = 0, outline = 0, gizmo = 0, properties = 0, explorer = 0, emptied = 0, empties = 0;
    std::map<std::string, int> perTarget;
    std::set<std::string> missingRows;
    stress::Distribution clicksTime;
    for (int i = 0; i < clicks; ++i) {
        bool empty = i % 10 == 9;
        document.tool = i % 5 == 0 ? ld::StudioTool::move : ld::StudioTool::select;
        bench.stage().refresh();
        std::string id = targets[static_cast<std::size_t>(i) % targets.size()];
        ui::Point at = empty ? ui::Point{12, 12} : aim[id];
        if (!empty && document.tool != ld::StudioTool::select && bench.stage().handleAt(at)) document.tool = ld::StudioTool::select, bench.stage().refresh();
        double begun = stress::now();
        bench.pointer(ui::PointerPhase::moved, at);
        bench.pointer(ui::PointerPhase::down, at);
        bench.pointer(ui::PointerPhase::up, at);
        clicksTime.add(stress::now() - begun);
        watchdog.beat();
        if (!empty && id == locked) {
            if (document.selectedIds() != std::vector<std::string>{baseplate}) ++wrong;
            document.select({locked});
            bench.stage().refresh();
        }
        if (i % 4 == 0) bench.draw();
        auto highlights = bench.stage().scene()->root->child("studio.highlights");
        int visible = 0;
        ld::Vec3 shown;
        if (highlights)
            for (const auto& holder : highlights->children())
                if (!holder->hidden) {
                    ++visible;
                    shown = holder->position;
                }
        if (empty) {
            ++empties;
            if (!document.selectedIds().empty() || visible != 0) ++emptied;
            continue;
        }
        ++perTarget[id == baseplate ? "Baseplate" : id == locked ? "bloccata" : id == targets[0] ? "piccola" : id == targets[1] ? "grande" : id == targets[2] ? "sottile" : "ruotata"];
        if (document.selectedIds() != std::vector<std::string>{id}) {
            ++wrong;
            continue;
        }
        const auto& b = bench.block(id);
        if (visible != 1 || std::abs(shown.x - b.x) > 1e-9 || std::abs(shown.y - b.y) > 1e-9 || std::abs(shown.z - b.z) > 1e-9) ++outline;
        if (document.tool != ld::StudioTool::select) {
            bool disabled = bench.stage().gizmoState() == StudioStage::GizmoState::disabled;
            if (disabled != (id == locked)) ++gizmo;
        }
        if (i % 4 == 0) {
            if (bench.surface->matching("vector." + id).empty()) ++properties;
            if (bench.surface->matching("explorer.part." + document.displayName(id)).empty()) {
                ++explorer;
                missingRows.insert(document.displayName(id));
            }
        }
    }
    ld::Json coverage = ld::Json::object();
    for (const auto& [name, count] : perTarget) coverage.set(name, count);
    stress::record("tortura selezione", "clic", clicks);
    stress::record("tortura selezione", "per bersaglio", coverage);
    stress::record("tortura selezione", "clic completo", clicksTime);
    stress::record("tortura selezione", "anomalie", ld::Json::object({{"selezione sbagliata", wrong}, {"contorno", outline}, {"gizmo", gizmo}, {"Proprietà", properties}, {"Explorer", explorer}, {"clic nel vuoto non deselezionati", emptied}}));
    INFO(coverage.dump(true) << " sbagliate " << wrong << ", contorno " << outline << ", gizmo " << gizmo << ", Proprietà " << properties << ", Explorer " << explorer << ", vuoto " << emptied << " su " << empties << "; righe assenti: " << [&] { std::string t; for (const auto& n : missingRows) t += n + " "; return t; }());
    CHECK(perTarget.size() == 6);
    CHECK(wrong == 0);
    CHECK(outline == 0);
    CHECK(gizmo == 0);
    CHECK(properties == 0);
    CHECK(explorer == 0);
    CHECK(emptied == 0);
    TIMED_CHECK(clicksTime.percentile(0.99) < 0.01);
}

TEST_CASE("Tortura: Annulla e Ripeti", "[stress][torture][undo]") {
    stress::prepare();
    stress::Watchdog watchdog("Tortura Annulla", 30);
    ld::Experience world = ld::Experience::blank();
    world.id = ld::makeUuid();
    {
        ld::StudioDocument seed;
        REQUIRE(seed.open(world));
        for (int i = 0; i < 6; ++i) REQUIRE(seed.addPart(i % 2 ? "sphere" : "cube"));
        world = seed.world;
    }
    ld::StudioDocument document;
    REQUIRE(document.open(world));
    REQUIRE_FALSE(document.undoLabel());
    std::vector<ld::Experience> history{document.world};
    std::size_t position = 0;
    std::mt19937 random(20261005);
    int operations = stress::scaled(300, 2000, 6000), undos = 0, redos = 0, edits = 0, mismatches = 0, stroke = 1, noop = 0;
    std::map<std::string, int> kinds;
    std::vector<std::string> trail;
    std::string first;
    auto note = [&](const std::string& what) {
        trail.push_back(what + "@" + std::to_string(position) + "/" + std::to_string(history.size()));
        if (trail.size() > 8) trail.erase(trail.begin());
    };
    auto differs = [&](const ld::Experience& expected) {
        if (document.world == expected) return false;
        if (first.empty()) {
            for (const auto& t : trail) first += t + " ";
            first += "| blocchi " + std::to_string(document.world.blocks.size()) + " contro " + std::to_string(expected.blocks.size());
            first += " terreno " + std::string(document.world.terrain == expected.terrain ? "uguale" : "diverso");
            first += " istanze " + std::string(document.world.instances == expected.instances ? "uguali" : "diverse");
            for (std::size_t k = 0; k < std::min(document.world.blocks.size(), expected.blocks.size()); ++k)
                if (!(document.world.blocks[k] == expected.blocks[k])) {
                    first += " blocco " + document.world.blocks[k].name + " / " + expected.blocks[k].name;
                    break;
                }
        }
        return true;
    };
    auto parts = [&] {
        std::vector<std::string> ids;
        for (const auto& b : document.world.blocks)
            if (b.name != ld::rules::baseplate::name) ids.push_back(b.id);
        return ids;
    };
    for (int i = 0; i < operations; ++i) {
        int choice = static_cast<int>(random() % 10);
        if (choice < 4 && position > 0) {
            document.undo();
            --position;
            ++undos;
            note("annulla");
            if (differs(history[position])) ++mismatches;
        } else if (choice < 7 && position + 1 < history.size()) {
            document.redo();
            ++position;
            ++redos;
            note("ripeti");
            if (differs(history[position])) ++mismatches;
        } else {
            auto ids = parts();
            int edit = static_cast<int>(random() % 7);
            std::string label = "Modifica " + std::to_string(i);
            if (ids.empty()) edit = 3;
            std::string id = ids.empty() ? std::string() : ids[random() % ids.size()];
            std::string name;
            switch (edit) {
            case 0: name = "sposta"; document.update(id, [&](ld::WorldBlock& b) { b.x = b.x > 0 ? b.x - 1.5 - static_cast<double>(random() % 5) : b.x + 1.5 + static_cast<double>(random() % 5); }, label); break;
            case 1: name = "scala"; document.update(id, [&](ld::WorldBlock& b) { b.sizeY = b.sizeY >= 2 ? b.sizeY - 0.75 : b.sizeY + 0.5 + (random() % 4) * 0.25; }, label); break;
            case 2: name = "ruota"; document.update(id, [&](ld::WorldBlock& b) { b.rotationY = std::fmod(b.rotationY + 15 + static_cast<double>(random() % 300), 360.0); }, label); break;
            case 3: name = "inserisci"; if (ids.size() < 40) REQUIRE(document.addPart("cylinder")); else document.update(ids.front(), [](ld::WorldBlock& b) { b.y += 1; }, label); break;
            case 4: name = "elimina"; document.select({id}); document.deleteSelection(); break;
            case 5: name = "proprietà"; document.update(id, [&](ld::WorldBlock& b) { b.anchored = !b.anchored; b.transparency = (random() % 5) * 0.2; }, label); break;
            default: {
                name = "Terrain";
                ld::Vec3 point{static_cast<double>(random() % 40) - 20, 1, static_cast<double>(random() % 40) - 20};
                document.perform(ld::StudioEdit::paint(ld::TerrainTool::add, point, stroke++));
                break;
            }
            }
            if (document.world == history[position]) {
                ++noop;
                continue;
            }
            history.resize(position + 1);
            history.push_back(document.world);
            if (history.size() > static_cast<std::size_t>(ld::StudioDocument::history) + 1) history.erase(history.begin());
            position = history.size() - 1;
            ++edits;
            ++kinds[name];
            note(name);
        }
        if (i % 50 == 0) watchdog.beat();
    }
    while (position > 0) {
        document.undo();
        --position;
        ++undos;
        note("svolgi annulla");
        if (differs(history[position])) ++mismatches;
    }
    document.undo();
    note("annulla oltre l'inizio");
    if (differs(history.front())) ++mismatches;
    while (position + 1 < history.size()) {
        document.redo();
        ++position;
        ++redos;
        note("svolgi ripeti");
        if (differs(history[position])) ++mismatches;
    }
    document.redo();
    note("ripeti oltre la fine");
    if (differs(history.back())) ++mismatches;
    ld::Json counts = ld::Json::object();
    for (const auto& [name, count] : kinds) counts.set(name, count);
    stress::record("tortura annulla", "operazioni", ld::Json::object({{"modifiche", edits}, {"annulla", undos}, {"ripeti", redos}, {"per tipo", counts}}));
    stress::record("tortura annulla", "differenze dal modello", mismatches);
    INFO(counts.dump(true) << " modifiche " << edits << ", annulla " << undos << ", ripeti " << redos << ", differenze " << mismatches << ", operazioni senza effetto " << noop << "; prima differenza: " << first);
    CHECK(undos + redos >= stress::scaled(100, 1000, 3000));
    CHECK(kinds.size() == 7);
    CHECK(mismatches == 0);
}

TEST_CASE("Tortura: import di icona e miniatura", "[stress][torture][assets]") {
    Bench bench;
    stress::Watchdog watchdog("Import immagini", 30);
    auto& document = bench.document();
    auto cache = bench.folder / "Cache";
    ld::asset_cache::setFolder(cache);
    auto inputs = bench.folder / "Input";
    std::filesystem::create_directories(inputs);
    write(inputs / "icona.png", png(64, 64, 120));
    write(inputs / "miniatura.png", png(480, 270, 60));
    jpeg(inputs / "foto.jpg", 320, 180);
    write(inputs / "testo.png", "non sono un'immagine");
    write(inputs / "grande.png", std::string(9 * 1024 * 1024, 'x'));
    write(inputs / "icona.gif", png(64, 64, 120));
    std::string whole = png(64, 64, 90);
    write(inputs / "tronca.png", whole.substr(0, whole.size() / 3));
    write(inputs / "minuscola.png", png(8, 8, 30));
    write(inputs / "enorme.png", png(4100, 16, 30));
    struct Case {
        const char* file;
        ld::ExperienceImage kind;
        bool accepted;
    };
    const Case cases[] = {{"icona.png", ld::ExperienceImage::icon, true}, {"miniatura.png", ld::ExperienceImage::thumbnail, true}, {"foto.jpg", ld::ExperienceImage::thumbnail, true}, {"testo.png", ld::ExperienceImage::icon, false}, {"grande.png", ld::ExperienceImage::thumbnail, false}, {"icona.gif", ld::ExperienceImage::icon, false}, {"tronca.png", ld::ExperienceImage::icon, false}, {"minuscola.png", ld::ExperienceImage::icon, false}, {"enorme.png", ld::ExperienceImage::thumbnail, false}};
    int rounds = stress::scaled(20, 120, 400), wrong = 0, kept = 0, overwritten = 0;
    std::map<std::string, std::string> identity;
    double baseline = 0;
    std::size_t cached = 0;
    for (int round = 0; round < rounds; ++round) {
        for (const auto& c : cases) {
            auto previousIcon = document.experienceImage(ld::ExperienceImage::icon), previousThumbnail = document.experienceImage(ld::ExperienceImage::thumbnail);
            auto result = document.setExperienceImage(c.kind, inputs / c.file);
            if (result.ok() != c.accepted) ++wrong;
            auto icon = document.experienceImage(ld::ExperienceImage::icon), thumbnail = document.experienceImage(ld::ExperienceImage::thumbnail);
            if (!c.accepted && (icon != previousIcon || thumbnail != previousThumbnail)) ++kept;
            if (c.accepted) {
                auto& stored = c.kind == ld::ExperienceImage::icon ? icon : thumbnail;
                auto& other = c.kind == ld::ExperienceImage::icon ? thumbnail : icon;
                auto& otherBefore = c.kind == ld::ExperienceImage::icon ? previousThumbnail : previousIcon;
                if (other != otherBefore) ++overwritten;
                if (stored) {
                    auto [it, inserted] = identity.emplace(c.file, *stored);
                    if (!inserted && it->second != *stored) ++wrong;
                }
            }
        }
        if (round % 4 == 3) {
            document.clearExperienceImage(ld::ExperienceImage::icon);
            if (document.experienceImage(ld::ExperienceImage::icon)) ++wrong;
        }
        if (round == 2) {
            baseline = stress::megabytes();
            cached = files(cache);
        }
        watchdog.beat();
    }
    double growth = stress::megabytes() - baseline;
    std::size_t finalFiles = files(cache);
    stress::record("tortura asset", "import", rounds * static_cast<int>(std::size(cases)));
    stress::record("tortura asset", "file in cache", ld::Json::object({{"dopo 3 giri", static_cast<double>(cached)}, {"finale", static_cast<double>(finalFiles)}}));
    stress::record("tortura asset", "memoria", ld::Json::object({{"riferimento", baseline}, {"crescita", growth}}));
    INFO("esiti sbagliati " << wrong << ", stato alterato da import rifiutati " << kept << ", immagine dell'altro tipo sovrascritta " << overwritten << ", file in cache " << cached << " → " << finalFiles << ", crescita " << growth << " MB");
    CHECK(wrong == 0);
    CHECK(kept == 0);
    CHECK(overwritten == 0);
    CHECK(identity.size() == 3);
    CHECK(finalFiles == cached);
    UNINSTRUMENTED_CHECK(growth < 60);
    ld::asset_cache::setFolder(std::nullopt);
}

TEST_CASE("Tortura: ciclo audio", "[stress][torture][audio]") {
    stress::prepare();
    stress::Watchdog watchdog("Ciclo audio", 30);
    auto folder = std::filesystem::temp_directory_path() / ("Ludissey Ciclo Audio " + ld::makeUuid());
    ld::asset_cache::setFolder(folder);
    std::string data = "RIFF audio di prova";
    std::string asset = ld::asset_cache::identifier(data, "wav");
    ld::asset_cache::store(data, asset);
    auto speakers = std::make_shared<Speakers>();
    ld::audio::setOutput(speakers);
    ld::Experience world = ld::Experience::blank();
    for (int i = 0; i < 4; ++i) {
        ld::InstanceRecord sound;
        sound.id = ld::makeUuid();
        sound.className = "Sound";
        sound.name = "Suono" + std::to_string(i);
        sound.parent = "SoundService";
        sound.properties["SoundId"] = ld::PropertyValue::content(ld::asset_cache::reference(asset));
        sound.properties["Looped"] = ld::PropertyValue::boolean(i == 0);
        sound.properties["Playing"] = ld::PropertyValue::boolean(i == 0);
        world.instances.push_back(sound);
    }
    ld::InstanceRecord script;
    script.id = ld::makeUuid();
    script.className = "Script";
    script.name = "Regia";
    script.parent = "ServerScriptService";
    script.properties["Source"] = ld::PropertyValue::string("for i in 1...40 {\n    let s = Audio.find(\"Suono\\(i % 3 + 1)\")\n    if s != none {\n        s.play()\n        await Task.nextFrame()\n        s.stop()\n    }\n}\nprint(\"ciclo audio finito\")\n");
    world.instances.push_back(script);
    int sessions = stress::scaled(10, 60, 240), leftovers = 0, finished = 0;
    double baseline = 0;
    for (int round = 0; round < sessions; ++round) {
        std::vector<std::string> lines;
        {
            ld::WorldSession session(world, ld::WorldSession::Mode::play, "Audio", 1, false, [&](const ld::OutputLine& l) { lines.push_back(l.text); });
            session.physics = false;
            session.start({0, 1, 0});
            for (int frame = 0; frame < 120; ++frame) session.advance(1.0 / 60);
            session.stop();
        }
        if (!speakers->active.empty()) ++leftovers;
        if (std::find(lines.begin(), lines.end(), "ciclo audio finito") != lines.end()) ++finished;
        if (round == 2) baseline = stress::megabytes();
        watchdog.beat();
    }
    double growth = stress::megabytes() - baseline;
    stress::record("tortura audio", "sessioni", sessions);
    stress::record("tortura audio", "riproduzioni", ld::Json::object({{"avviate", speakers->started}, {"fermate", speakers->stopped}}));
    INFO("sessioni " << sessions << ", finite " << finished << ", riproduzioni avviate " << speakers->started << ", fermate " << speakers->stopped << ", rimaste attive " << leftovers << ", crescita " << growth << " MB");
    CHECK(leftovers == 0);
    CHECK(finished == sessions);
    CHECK(speakers->started >= sessions * 41);
    CHECK(speakers->started == speakers->stopped);
    UNINSTRUMENTED_CHECK(growth < 40);
    ld::audio::setOutput(nullptr);
    ld::asset_cache::setFolder(std::nullopt);
    std::error_code ignored;
    std::filesystem::remove_all(folder, ignored);
}

TEST_CASE("Tortura: editor Lim", "[stress][torture][editor]") {
    stress::Watchdog watchdog("Tortura editor", 20);
    lim::TypeRegistry registry = lim::standardRegistry();
    lim::tooling::Context context{registry, {}};
    const std::vector<std::string> programs{"x", "x =", "x = (", "x = foo(", "match", "match x", "class", "enum", "let x = ", "var y: [Integer] = [1, 2", "fn f(a: Number, b", "class A { var v = 1\n fn m() -> Number { return v", "enum E { a, b", "match e { .a { print(1) }", "if x > 1 && y", "for i in 1...", "while true {", "on Players.joined(p) { print(p.", "let t = \"testo \\(x", "x?.y ?? ", "await Task.wait(", "let f = fn(x) { return x *"};
    int analysed = 0;
    int repeats = stress::scaled(2, 12, 40);
    for (int repeat = 0; repeat < repeats; ++repeat)
        for (const auto& program : programs) {
            std::string typed;
            for (char c : program) {
                typed.push_back(c);
                for (std::uint32_t offset : {0u, static_cast<std::uint32_t>(typed.size() / 2), static_cast<std::uint32_t>(typed.size())}) {
                    auto parsed = lim::parse(typed, "editor");
                    lim::analyze(parsed.program, registry, {});
                    lim::tooling::hover(typed, offset, context);
                    lim::tooling::complete(typed, offset, context);
                    lim::tooling::definition(typed, offset, context);
                    lim::tooling::references(typed, offset, context);
                    lim::tooling::highlight(typed);
                    ++analysed;
                }
            }
            while (!typed.empty()) {
                typed.pop_back();
                lim::tooling::hover(typed, static_cast<std::uint32_t>(typed.size()), context);
                lim::tooling::complete(typed, static_cast<std::uint32_t>(typed.size()), context);
                lim::compileSource(typed, "Editor", registry, {});
                ++analysed;
            }
            watchdog.beat();
        }
    stress::record("tortura editor", "stati analizzati", analysed);
    stress::record("tortura editor", "costrutti", static_cast<double>(programs.size()));
    CHECK(analysed > 1000);
}

TEST_CASE("Tortura: terreno", "[stress][torture][terrain]") {
    stress::prepare();
    stress::Watchdog watchdog("Tortura Terrain", 40);
    ld::Terrain terrain;
    ld::render::TerrainView view;
    std::mt19937 random(20261006);
    int strokes = stress::scaled(200, 1500, 6000);
    stress::Distribution edits, meshes;
    const ld::TerrainTool tools[] = {ld::TerrainTool::add, ld::TerrainTool::subtract, ld::TerrainTool::raise};
    std::map<std::string, int> used;
    double baseline = stress::megabytes();
    for (int i = 0; i < strokes; ++i) {
        ld::TerrainBrush brush;
        brush.radius = 2 + static_cast<double>(i % 5);
        brush.strength = 0.4 + (i % 3) * 0.2;
        brush.placement = i % 2 ? ld::TerrainPlacement::free : ld::TerrainPlacement::surface;
        ld::TerrainTool tool = tools[(i / 2) % 3];
        ld::Vec3 point{static_cast<double>(static_cast<int>(random() % 96)) - 48, static_cast<double>(random() % 6), static_cast<double>(static_cast<int>(random() % 96)) - 48};
        double started = stress::now();
        terrain.apply(tool, point, brush);
        edits.add(stress::now() - started);
        started = stress::now();
        view.sync(terrain);
        meshes.add(stress::now() - started);
        ++used[std::string(ld::toolTitle(tool)) + (brush.placement == ld::TerrainPlacement::surface ? " su superficie" : " libera")];
        if (i % 20 == 0) watchdog.beat();
    }
    auto decoded = ld::Terrain::fromJson(terrain.json());
    REQUIRE(decoded.ok());
    CHECK(*decoded == terrain);
    ld::CharacterDriver driver;
    std::vector<ld::WorldBlock> blocks{ld::Experience::baseplate(ld::makeUuid())};
    driver.reset(ld::motion::spawn(blocks, terrain, -40, -40));
    std::vector<const ld::WorldBlock*> solids{&blocks.front()};
    ld::CharacterInput input;
    input.forward = 1;
    int steps = stress::scaled(2400, 12000, 40000), jumps = 0, falls = 0, chunks = 0;
    double previous = driver.state().y;
    int lastChunk = static_cast<int>(std::floor(driver.state().x / (ld::Terrain::chunk * ld::Terrain::cellSize)));
    stress::Distribution step;
    for (int i = 0; i < steps; ++i) {
        double yaw = std::sin(i * 0.0015) * 3.1;
        double started = stress::now();
        driver.advance(1.0 / 60, [&](const ld::CharacterState& state, double dt) -> std::optional<ld::CharacterState> { return ld::motion::integrate(state, input, yaw, solids, terrain, dt); });
        step.add(stress::now() - started);
        const auto& state = driver.state();
        REQUIRE(std::isfinite(state.x + state.y + state.z));
        if (state.y - previous > ld::motion::stepUp + 0.05) ++jumps;
        previous = state.y;
        int chunk = static_cast<int>(std::floor(state.x / (ld::Terrain::chunk * ld::Terrain::cellSize)));
        if (chunk != lastChunk) ++chunks, lastChunk = chunk;
        if (state.y < ld::motion::voidHeight + 5) {
            driver.reset(ld::motion::spawn(blocks, terrain, 0, 0));
            previous = driver.state().y;
            ++falls;
        }
        if (i % 500 == 0) watchdog.beat();
    }
    std::size_t half = edits.samples.size() / 2;
    stress::Distribution early, late;
    for (std::size_t i = 0; i < edits.samples.size(); ++i) (i < half ? early : late).add(edits.samples[i] + meshes.samples[i]);
    double growth = stress::megabytes() - baseline;
    ld::Json coverage = ld::Json::object();
    for (const auto& [name, count] : used) coverage.set(name, count);
    stress::record("tortura terrain", "pennellate per modalità", coverage);
    stress::record("tortura terrain", "pennellata", edits);
    stress::record("tortura terrain", "aggiornamento mesh", meshes);
    stress::record("tortura terrain", "personaggio", ld::Json::object({{"passi", steps}, {"salti oltre il gradino", jumps}, {"cadute nel vuoto", falls}, {"confini di chunk attraversati", chunks}}));
    stress::record("tortura terrain", "passo del personaggio", step);
    INFO(coverage.dump(true) << " salti " << jumps << ", cadute " << falls << ", chunk attraversati " << chunks << ", modifica+mesh p50 prima metà " << early.percentile(0.5) * 1000 << " ms, seconda metà " << late.percentile(0.5) * 1000);
    CHECK(used.size() == 6);
    CHECK(jumps == 0);
    CHECK(chunks > 2);
    TIMED_CHECK(late.percentile(0.5) < early.percentile(0.5) * 2 + 0.002);
    TIMED_CHECK(meshes.percentile(0.95) < 1.0 / 30);
    UNINSTRUMENTED_CHECK(growth < 200);
}
