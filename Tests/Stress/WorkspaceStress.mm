#import <AppKit/AppKit.h>

#include "City.hpp"
#include "Engine/Rendering/Renderer.hpp"
#include "Interface/ClientView.hpp"
#include "Lim/Tooling/Tooling.hpp"
#include "Platform/Mac/Platform.hpp"
#include "Rig.hpp"
#include "Studio/StudioView.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <random>
#include <unistd.h>

namespace {

struct Workspace {
    std::filesystem::path folder = std::filesystem::temp_directory_path() / ("Ludissey Stress " + ld::makeUuid());
    std::shared_ptr<ld::Store> store;
    std::unique_ptr<studio::StudioShell> studio;
    std::unique_ptr<platform::Window> window;
    stress::Distribution total, tick, interface, present, interval;
    int frames = 0, interfaceRenders = 0;
    std::function<void()> perFrame;
    std::function<void()> betweenFrames;

    Workspace() {
        stress::prepare();
        [NSApplication sharedApplication];
        platform::installAdapters();
        store = stress::account(folder);
        studio = std::make_unique<studio::StudioShell>(store);
        REQUIRE(studio->document.open(stress::project()));
        window = std::make_unique<platform::Window>(platform::WindowSpec{"Stress del Workspace", 1180, 760, 860, 680}, [this] { return studio->build(); });
        studio->invalidated = [this] { window->invalidate(); };
        window->views = [this](const auto& slots) { return studio->views(slots); };
        window->rendered = [this](const auto& surface) { studio->rendered(surface); };
        window->tick = [this](double now, double delta) {
            if (perFrame) perFrame();
            studio->tick(now, delta);
        };
        window->sampled = [this](const platform::FrameSample& sample) {
            ++frames;
            total.add(sample.total);
            tick.add(sample.tick);
            interface.add(sample.interface);
            present.add(sample.present);
            interval.add(sample.interval);
            if (sample.interfaceRendered) ++interfaceRenders;
        };
        window->show();
    }

    ~Workspace() {
        studio->invalidated = nullptr;
        window.reset();
        studio.reset();
        store.reset();
        std::error_code problem;
        std::filesystem::remove_all(folder, problem);
    }

    void reset() {
        total = {};
        tick = {};
        interface = {};
        present = {};
        interval = {};
        frames = 0;
        interfaceRenders = 0;
    }

    void run(double seconds, stress::Watchdog& watchdog) {
        double end = stress::now() + seconds;
        while (stress::now() < end) {
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.002, true);
            ld::fallbackQueue().pump(0.0005);
            if (betweenFrames) betweenFrames();
            watchdog.beat();
        }
    }

    void key(int code, bool down) {
        ui::KeyInput input;
        input.keyCode = code;
        input.down = down;
        studio->key(input);
    }

    void pointer(ui::PointerPhase phase, double x, double y, double dx = 0, double dy = 0, double scroll = 0) {
        ui::PointerEvent event;
        event.phase = phase;
        event.location = {x, y};
        event.delta = {dx, dy};
        event.scrollY = scroll;
        studio->pointer("studio.viewport", event);
    }

    void report(const std::string& name) {
        REQUIRE(frames > 0);
        stress::record(name, "frame", total);
        stress::record(name, "tick", tick);
        stress::record(name, "interfaccia", interface);
        stress::record(name, "presentazione", present);
        stress::record(name, "intervallo", interval);
        stress::record(name, "conteggi", ld::Json::object({{"frame", frames}, {"ridisegni interfaccia", interfaceRenders}}));
    }
};

void populate(ld::StudioDocument& document, int parts) {
    for (int i = 0; i < parts; ++i) {
        document.addPart(i % 3 == 0 ? "cube" : i % 3 == 1 ? "sphere" : "cylinder");
        std::string id = document.world.blocks.back().id;
        document.update(id, [i](ld::WorldBlock& b) {
            b.x = (i % 12) * 3 - 18;
            b.z = (i / 12) * 3 - 12;
            b.y = 1 + (i % 4);
        });
    }
    document.setSelection(std::nullopt);
}

}

TEST_CASE("Workspace: volo della camera e prova", "[stress][workspace]") {
    @autoreleasepool {
        Workspace w;
        stress::Watchdog watchdog("Workspace", 20);
        populate(w.studio->document, 60);
        w.run(1.0, watchdog);
        double refresh = 1.0 / 60;
        double baseline = stress::megabytes();

        int step = 0;
        w.perFrame = [&] {
            ++step;
            int phase = (step / 40) % 4;
            static const int codes[4] = {13, 2, 1, 0};
            for (int code : codes) w.key(code, false);
            w.key(codes[phase], true);
            if (step % 3 == 0) w.key(codes[(phase + 1) % 4], true);
            w.pointer(ui::PointerPhase::rightDragged, 300, 300, std::sin(step * 0.05) * 6, std::cos(step * 0.07) * 2);
            if (step % 30 == 0) w.pointer(ui::PointerPhase::scroll, 300, 300, 0, 0, step % 60 == 0 ? 1 : -1);
        };
        w.pointer(ui::PointerPhase::rightDown, 300, 300);
        w.reset();
        w.run(stress::seconds(6, 30, 120), watchdog);
        w.pointer(ui::PointerPhase::rightUp, 300, 300);
        for (int code : {13, 2, 1, 0}) w.key(code, false);
        w.perFrame = nullptr;
        w.report("workspace modifica");
        INFO("modifica: p50 " << w.total.percentile(0.5) * 1000 << " ms, p95 " << w.total.percentile(0.95) * 1000 << ", p99 " << w.total.percentile(0.99) * 1000 << ", peggiore " << w.total.worst() * 1000 << "; ridisegni interfaccia " << w.interfaceRenders << " su " << w.frames);
        TIMED_CHECK(w.frames > 100);
        TIMED_CHECK(w.total.percentile(0.95) < refresh);
        TIMED_CHECK(w.total.percentile(0.99) < refresh * 2);
        TIMED_CHECK(w.interval.percentile(0.95) < refresh * 1.5);
        UNINSTRUMENTED_CHECK(w.interfaceRenders < w.frames / 10);
        TIMED_CHECK(!stress::regressed("workspace modifica, frame p95", w.total.percentile(0.95)));
        TIMED_CHECK(!stress::regressed("workspace modifica, intervallo p95", w.interval.percentile(0.95), 1.25));

        w.studio->document.playMode = ld::StudioPlayMode::character;
        w.studio->document.play();
        w.run(0.5, watchdog);
        step = 0;
        w.perFrame = [&] {
            ++step;
            int phase = (step / 25) % 6;
            static const int codes[4] = {13, 2, 1, 0};
            for (int code : codes) w.key(code, false);
            w.key(codes[phase % 4], true);
            if (phase >= 4) w.key(codes[(phase + 1) % 4], true);
            if (step % 45 == 0) w.key(49, true);
            if (step % 45 == 2) w.key(49, false);
            w.pointer(ui::PointerPhase::rightDragged, 300, 300, std::sin(step * 0.04) * 5, std::cos(step * 0.06) * 1.5);
        };
        w.pointer(ui::PointerPhase::rightDown, 300, 300);
        w.reset();
        w.run(stress::seconds(6, 30, 120), watchdog);
        w.pointer(ui::PointerPhase::rightUp, 300, 300);
        w.perFrame = nullptr;
        w.report("workspace play");
        INFO("play: p50 " << w.total.percentile(0.5) * 1000 << " ms, p95 " << w.total.percentile(0.95) * 1000 << ", p99 " << w.total.percentile(0.99) * 1000 << ", peggiore " << w.total.worst() * 1000 << "; interfaccia p95 " << w.interface.percentile(0.95) * 1000 << "; ridisegni interfaccia " << w.interfaceRenders << " su " << w.frames);
        TIMED_CHECK(w.frames > 100);
        TIMED_CHECK(w.total.percentile(0.95) < refresh);
        TIMED_CHECK(w.total.percentile(0.99) < refresh * 2);
        TIMED_CHECK(w.interval.percentile(0.95) < refresh * 1.5);
        TIMED_CHECK(!stress::regressed("workspace play, tick p95", w.tick.percentile(0.95)));
        TIMED_CHECK(!stress::regressed("workspace play, intervallo p95", w.interval.percentile(0.95), 1.25));
        double skippedBefore = static_cast<double>(ld::render::Renderer::shared().skipped());
        stress::record("workspace play", "frame saltati dal renderer", skippedBefore);
        TIMED_CHECK(skippedBefore < (w.frames + 400) * 0.02);
        int stalls = stress::scaled(6, 20, 60);
        for (int i = 0; i < stalls; ++i) {
            bool stalled = false;
            w.perFrame = [&] {
                if (stalled) return;
                stalled = true;
                usleep(70000);
            };
            w.run(0.3, watchdog);
        }
        w.perFrame = nullptr;
        w.reset();
        w.run(2, watchdog);
        w.report("workspace play dopo stalli");
        stress::record("workspace play dopo stalli", "stalli provocati", stalls);
        stress::record("workspace play dopo stalli", "frame saltati dal renderer", static_cast<double>(ld::render::Renderer::shared().skipped()) - skippedBefore);
        TIMED_CHECK(static_cast<double>(ld::render::Renderer::shared().skipped()) - skippedBefore <= stalls * 3);
        INFO("dopo " << stalls << " stalli: presentazione p50 " << w.present.percentile(0.5) * 1000 << " ms, p95 " << w.present.percentile(0.95) * 1000 << "; intervallo p95 " << w.interval.percentile(0.95) * 1000);
        TIMED_CHECK(w.present.percentile(0.5) < refresh / 3);
        TIMED_CHECK(w.interval.percentile(0.95) < refresh * 1.5);
        w.studio->document.stop();
        w.run(0.5, watchdog);
        stress::record("workspace", "memoria", ld::Json::object({{"prima", baseline}, {"dopo", stress::megabytes()}}));
        UNINSTRUMENTED_CHECK(stress::megabytes() - baseline < 80);
    }
}

TEST_CASE("Workspace: prova senza ricostruire l'interfaccia", "[stress][workspace][animated]") {
    @autoreleasepool {
        Workspace w;
        stress::Watchdog watchdog("Workspace animato", 20);
        auto& document = w.studio->document;
        populate(document, 24);
        for (int i = 0; i < 12; ++i) {
            std::string id = document.world.blocks[static_cast<std::size_t>(i + 1)].id;
            document.update(id, [i](ld::WorldBlock& b) {
                b.anchored = false;
                b.y = 6 + i;
            });
        }
        std::string mover = document.world.blocks.back().id;
        auto script = document.insert("Script", mover);
        REQUIRE(script);
        document.setSource(*script, "let p = script.parent\nif p is BasePart {\n    print(\"animazione avviata\")\n    var y = 3.0\n    while true {\n        y = y + 0.02\n        if y > 6 {\n            y = 3.0\n        }\n        p.position = Vec3(0, y, 0)\n        await Task.nextFrame()\n    }\n}\n");
        document.playMode = ld::StudioPlayMode::camera;
        document.play();
        w.run(0.5, watchdog);
        double refresh = 1.0 / 60;
        int step = 0;
        w.perFrame = [&] {
            ++step;
            w.pointer(ui::PointerPhase::rightDragged, 300, 300, std::sin(step * 0.05) * 4, std::cos(step * 0.07) * 1.5);
        };
        w.pointer(ui::PointerPhase::rightDown, 300, 300);
        w.reset();
        w.run(stress::seconds(5, 20, 60), watchdog);
        w.pointer(ui::PointerPhase::rightUp, 300, 300);
        w.perFrame = nullptr;
        w.report("workspace play animato");
        bool started = std::any_of(document.output.begin(), document.output.end(), [](const ld::OutputLine& l) { return l.text.find("animazione avviata") != std::string::npos; });
        std::string transcript;
        for (const auto& line : document.output) transcript += line.text + "\n";
        INFO(transcript);
        INFO("play animato: frame " << w.frames << ", ridisegni interfaccia " << w.interfaceRenders << "; frame p50 " << w.total.percentile(0.5) * 1000 << " ms, p95 " << w.total.percentile(0.95) * 1000 << ", p99 " << w.total.percentile(0.99) * 1000 << ", peggiore " << w.total.worst() * 1000 << "; intervallo p95 " << w.interval.percentile(0.95) * 1000);
        CHECK(started);
        CHECK(w.frames > 60);
        CHECK(w.interfaceRenders < w.frames / 10);
        TIMED_CHECK(w.total.percentile(0.95) < refresh);
        TIMED_CHECK(w.total.percentile(0.99) < refresh * 2);
        TIMED_CHECK(w.interval.percentile(0.95) < refresh * 1.5);
        document.stop();
        w.run(0.3, watchdog);
    }
}

TEST_CASE("Workspace: trascinamento prolungato del gizmo", "[stress][workspace][gesture]") {
    @autoreleasepool {
        Workspace w;
        stress::Watchdog watchdog("Trascinamento", 20);
        auto& document = w.studio->document;
        populate(document, 30);
        w.run(1.0, watchdog);
        auto stage = w.studio->stage;
        REQUIRE(stage);
        document.tool = ld::StudioTool::move;
        document.moveSnap = 0;
        std::string part = document.world.blocks.back().id;
        document.select({part});
        stage->refresh();
        w.run(0.3, watchdog);
        auto gizmo = stage->scene()->root->child("studio.gizmo");
        REQUIRE(gizmo);
        REQUIRE_FALSE(gizmo->hidden);
        ld::render::Viewport view{stage->size.width, stage->size.height};
        ld::Vec3 centre = ld::render::project(*stage->camera(), view, gizmo->worldPosition());
        std::optional<ld::Vec3> head;
        for (const auto& holder : gizmo->children())
            if (holder->name == "gizmo:0:1" && holder->children().size() >= 2) head = ld::render::project(*stage->camera(), view, holder->children()[1]->worldPosition());
        REQUIRE(head);
        double dx = head->x - centre.x, dy = head->y - centre.y, length = std::max(1e-6, std::hypot(dx, dy));
        dx /= length;
        dy /= length;
        double before = document.world.blocks.back().x;
        w.pointer(ui::PointerPhase::moved, head->x, head->y);
        w.pointer(ui::PointerPhase::down, head->x, head->y);
        REQUIRE(stage->gizmoState() == studio::StudioStage::GizmoState::pressed);
        int step = 0, lost = 0;
        w.perFrame = [&] {
            ++step;
            double reach = std::sin(step * 0.05) * 120;
            w.pointer(ui::PointerPhase::dragged, head->x + dx * reach, head->y + dy * reach);
            if (stage->gizmoState() != studio::StudioStage::GizmoState::dragging) ++lost;
        };
        w.reset();
        w.run(stress::seconds(3, 12, 40), watchdog);
        w.perFrame = nullptr;
        w.pointer(ui::PointerPhase::up, head->x + dx * 60, head->y + dy * 60);
        w.run(0.3, watchdog);
        w.report("trascinamento prolungato");
        double moved = document.world.blocks.back().x - before;
        INFO("trascinamento: frame " << w.frames << ", ridisegni interfaccia " << w.interfaceRenders << "; frame p50 " << w.total.percentile(0.5) * 1000 << " ms, p90 " << w.total.percentile(0.9) * 1000 << ", p95 " << w.total.percentile(0.95) * 1000 << ", p99 " << w.total.percentile(0.99) * 1000 << ", peggiore " << w.total.worst() * 1000 << "; intervallo p95 " << w.interval.percentile(0.95) * 1000 << "; spostamento finale " << moved);
        CHECK(lost == 0);
        CHECK(std::abs(moved) > 0.1);
        CHECK(document.undoLabel());
        CHECK(w.frames > 60);
        CHECK(w.interfaceRenders <= w.frames / 4);
        TIMED_CHECK(w.total.percentile(0.5) < 1.0 / 60);
        TIMED_CHECK(w.interval.percentile(0.5) < 1.0 / 60 * 1.25);
    }
}

TEST_CASE("Workspace: ridisegno completo sotto 25 ms", "[stress][interface]") {
    @autoreleasepool {
        Workspace w;
        stress::Watchdog watchdog("Interfaccia", 20);
        populate(w.studio->document, 60);
        w.studio->document.select({w.studio->document.world.blocks[3].id});
        w.run(0.5, watchdog);
        w.perFrame = [&] { w.window->invalidate(); };
        w.reset();
        w.run(stress::seconds(3, 10, 30), watchdog);
        w.perFrame = nullptr;
        w.report("interfaccia ridisegnata a ogni frame");
        INFO("ridisegno completo: p50 " << w.interface.percentile(0.5) * 1000 << " ms, p95 " << w.interface.percentile(0.95) * 1000 << ", peggiore " << w.interface.worst() * 1000 << " su " << w.interfaceRenders << " ridisegni");
        CHECK(w.interfaceRenders > w.frames / 2);
        TIMED_CHECK(w.interface.percentile(0.5) < 0.025);
        TIMED_CHECK(!stress::regressed("interfaccia, ridisegno p50", w.interface.percentile(0.5), 1.5));
        TIMED_CHECK(w.interface.percentile(0.95) < 0.040);
    }
}

TEST_CASE("Metal: ridimensionamenti continui", "[stress][metal]") {
    @autoreleasepool {
        int rounds = stress::scaled(3, 12, 40);
        double baseline = 0, graphicsCeiling = 0, graphicsWorst = 0;
        stress::Distribution frames;
        for (int round = 0; round < rounds; ++round) {
            @autoreleasepool {
            Workspace w;
            stress::Watchdog watchdog("Metal", 20);
            populate(w.studio->document, 30);
            w.studio->document.perform(ld::StudioEdit::generate({round + 3, 3, 0, 1}));
            NSWindow* target = nil;
            for (NSWindow* candidate in NSApp.windows)
                if ([candidate.title isEqualToString:@"Stress del Workspace"] && candidate.isVisible) target = candidate;
            REQUIRE(target != nil);
            int step = 0;
            w.betweenFrames = [&] {
                double width = 900 + 250 * std::sin(step * 0.21), height = 700 + 120 * std::cos(step * 0.17);
                [target setContentSize:NSMakeSize(width, height)];
            };
            w.perFrame = [&] {
                ++step;
                w.pointer(ui::PointerPhase::rightDragged, 300, 300, 4, 1);
                if (step == 40) {
                    w.studio->document.playMode = ld::StudioPlayMode::character;
                    w.studio->document.play();
                }
                if (step == 110) w.studio->document.stop();
            };
            w.pointer(ui::PointerPhase::rightDown, 300, 300);
            w.reset();
            w.run(stress::seconds(3, 5, 8), watchdog);
            w.pointer(ui::PointerPhase::rightUp, 300, 300);
            w.perFrame = nullptr;
            w.betweenFrames = nullptr;
            if (w.studio->document.playing) w.studio->document.stop();
            for (double value : w.total.samples) frames.add(value);
            TIMED_CHECK(w.frames > 30);
            }
            double graphics = stress::graphicsMegabytes();
            if (round == 0) baseline = stress::megabytes() - graphics;
            if (round < 2) graphicsCeiling = std::max(graphicsCeiling, graphics);
            graphicsWorst = std::max(graphicsWorst, graphics);
        }
        double graphics = stress::graphicsMegabytes();
        double growth = stress::megabytes() - graphics - baseline;
        stress::record("metal", "finestre aperte e chiuse", rounds);
        stress::record("metal", "frame durante il ridimensionamento", frames);
        stress::record("metal", "memoria", ld::Json::object({{"processo riferimento", baseline}, {"processo finale", stress::megabytes() - graphics}, {"crescita del processo", growth}, {"grafica dei primi giri", graphicsCeiling}, {"grafica massima", graphicsWorst}, {"grafica finale", graphics}}));
        INFO("crescita del processo " << growth << " MB; grafica dei primi giri " << graphicsCeiling << " MB, massima " << graphicsWorst << " MB; frame p95 " << frames.percentile(0.95) * 1000 << " ms");
        UNINSTRUMENTED_CHECK(growth < 120);
        UNINSTRUMENTED_CHECK(graphicsWorst < graphicsCeiling + 250);
    }
}

TEST_CASE("Stress: gizmo e selezione", "[stress][gizmo]") {
    @autoreleasepool {
        Workspace w;
        stress::Watchdog watchdog("Gizmo", 20);
        auto& document = w.studio->document;
        populate(document, 40);
        w.run(1.0, watchdog);
        auto stage = w.studio->stage;
        REQUIRE(stage);
        document.tool = ld::StudioTool::move;
        document.moveSnap = 0;
        std::vector<std::string> parts;
        for (const auto& b : document.world.blocks)
            if (b.id != ld::Experience::baseplate(document.world.id).id) parts.push_back(b.id);
        REQUIRE(parts.size() >= 20);
        double baseline = 0, first = 0;
        stress::Distribution hover, selection, edits;
        int step = 0, hovers = 0, drags = 0, selections = 0, inserted = 0, wrongAxis = 0, lostCapture = 0, gridOff = 0;
        struct Head {
            std::pair<int, int> first;
            ld::Vec3 second, base;
        };
        auto heads = [&] {
            std::vector<Head> out;
            auto gizmo = stage->scene()->root->child("studio.gizmo");
            if (!gizmo || gizmo->hidden) return out;
            for (const auto& holder : gizmo->children()) {
                const std::string& name = holder->name;
                if (name.rfind("gizmo:", 0) != 0 || holder->children().size() < 2) continue;
                auto colon = name.find(':', 6);
                std::pair<int, int> handle{std::stoi(name.substr(6, colon - 6)), std::stoi(name.substr(colon + 1))};
                ld::render::Viewport view{stage->size.width, stage->size.height};
                out.push_back({handle, ld::render::project(*stage->camera(), view, holder->children()[1]->worldPosition()), ld::render::project(*stage->camera(), view, holder->worldPosition())});
            }
            return out;
        };
        auto isolated = [](const std::vector<Head>& all, std::size_t index) {
            ld::Vec3 p = all[index].second;
            for (std::size_t j = 0; j < all.size(); ++j) {
                if (j == index) continue;
                ld::Vec3 a = all[j].base, b = all[j].second;
                double dx = b.x - a.x, dy = b.y - a.y, length = dx * dx + dy * dy;
                double t = length > 1e-9 ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / length, 0.0, 1.0) : 0;
                if (std::hypot(p.x - (a.x + dx * t), p.y - (a.y + dy * t)) < 16) return false;
            }
            return true;
        };
        ld::Json samples = ld::Json::object();
        w.perFrame = [&] {
            ++step;
            if (step % 100 == 0) samples.set(std::to_string(step), stress::megabytes());
            if (step == 1) first = stress::megabytes();
            if (step == 200) baseline = stress::megabytes();
            int phase = (step / 30) % 5;
            if (phase == 0) {
                document.select({parts[static_cast<std::size_t>(step / 30) % parts.size()]});
                stage->refresh();
                auto all = heads();
                for (int i = 0; i < 24; ++i) {
                    double begun = stress::now();
                    if (!all.empty() && i % 2 == 0) {
                        std::size_t index = static_cast<std::size_t>((step + i) % static_cast<int>(all.size()));
                        w.pointer(ui::PointerPhase::moved, all[index].second.x, all[index].second.y);
                        if (isolated(all, index) && stage->highlightedHandle() != all[index].first) ++wrongAxis;
                    } else w.pointer(ui::PointerPhase::moved, 40 + (step * 37 + i * 53) % 800, 40 + (step * 29 + i * 41) % 600);
                    hover.add(stress::now() - begun);
                    ++hovers;
                }
            } else if (phase == 1) {
                auto all = heads();
                if (all.empty()) return;
                std::size_t index = static_cast<std::size_t>(step % static_cast<int>(all.size()));
                if (!isolated(all, index)) return;
                auto handle = all[index].first;
                auto at = all[index].second;
                ld::Vec3 centre = ld::render::project(*stage->camera(), {stage->size.width, stage->size.height}, stage->scene()->root->child("studio.gizmo")->worldPosition());
                double dx = at.x - centre.x, dy = at.y - centre.y, length = std::max(1e-6, std::hypot(dx, dy));
                dx /= length;
                dy /= length;
                double begun = stress::now();
                w.pointer(ui::PointerPhase::down, at.x, at.y);
                if (stage->gizmoState() != studio::StudioStage::GizmoState::pressed || stage->highlightedHandle() != handle) {
                    ++wrongAxis;
                    w.pointer(ui::PointerPhase::up, at.x, at.y);
                    return;
                }
                for (int k = 1; k <= 4; ++k) {
                    w.pointer(ui::PointerPhase::dragged, at.x + dx * 6 * k - dy * 70 * (k % 2), at.y + dy * 6 * k + dx * 70 * (k % 2));
                    if (stage->gizmoState() != studio::StudioStage::GizmoState::dragging || stage->highlightedHandle() != handle) ++lostCapture;
                }
                w.pointer(ui::PointerPhase::up, at.x + dx * 24, at.y + dy * 24);
                edits.add(stress::now() - begun);
                ++drags;
            } else if (phase == 2) {
                double begun = stress::now();
                for (int i = 0; i < 120; ++i) {
                    std::size_t a = static_cast<std::size_t>(step * 7 + i) % parts.size(), b = static_cast<std::size_t>(step * 3 + i * 5) % parts.size();
                    if (i % 3 == 0) document.select({parts[a], parts[b]});
                    else document.select({parts[a]});
                    ++selections;
                }
                stage->refresh();
                selection.add(stress::now() - begun);
            } else if (phase == 3) {
                std::string plate = ld::Experience::baseplate(document.world.id).id;
                document.update(plate, [step](ld::WorldBlock& b) {
                    b.x = std::sin(step * 0.05) * 6;
                    b.z = std::cos(step * 0.04) * 6;
                }, std::string("Sposta"), std::string("stress"));
                stage->refresh();
                auto grid = stage->scene()->root->child("studio.grid");
                const ld::WorldBlock* found = document.world.block(plate);
                if (!grid || !found || std::abs(grid->worldPosition().x - found->x) > 1e-9 || std::abs(grid->worldPosition().z - found->z) > 1e-9) ++gridOff;
            } else {
                double begun = stress::now();
                for (int i = 0; i < 6; ++i) {
                    if (document.addPart(i % 2 ? "cube" : "sphere")) {
                        ++inserted;
                        document.deleteSelection();
                    }
                }
                stage->refresh();
                edits.add(stress::now() - begun);
            }
        };
        w.reset();
        w.run(stress::seconds(10, 40, 180), watchdog);
        w.perFrame = nullptr;
        w.pointer(ui::PointerPhase::moved, 2, 2);
        double growth = stress::megabytes() - (baseline > 0 ? baseline : first);
        w.report("gizmo e selezione");
        stress::record("gizmo e selezione", "hover", hover);
        stress::record("gizmo e selezione", "selezioni per frame", selection);
        stress::record("gizmo e selezione", "trascinamenti e inserimenti", edits);
        stress::record("gizmo e selezione", "operazioni", ld::Json::object({{"hover", hovers}, {"trascinamenti", drags}, {"selezioni", selections}, {"inserimenti ed eliminazioni", inserted}}));
        stress::record("gizmo e selezione", "errori", ld::Json::object({{"asse sbagliato", wrongAxis}, {"cattura persa", lostCapture}, {"griglia fuori posto", gridOff}}));
        stress::record("gizmo e selezione", "memoria", ld::Json::object({{"riferimento", baseline}, {"finale", stress::megabytes()}, {"crescita", growth}, {"campioni", samples}}));
        INFO("hover p95 " << hover.percentile(0.95) * 1000 << " ms, peggiore " << hover.worst() * 1000 << "; selezioni p95 " << selection.percentile(0.95) * 1000 << " ms; frame p95 " << w.total.percentile(0.95) * 1000 << ", intervallo p95 " << w.interval.percentile(0.95) * 1000 << "; crescita " << growth << " MB; hover " << hovers << ", trascinamenti " << drags << ", selezioni " << selections << ", inserimenti " << inserted);
        TIMED_CHECK(hovers > 500);
        TIMED_CHECK(drags > 20);
        TIMED_CHECK(selections > 3000);
        TIMED_CHECK(inserted > 100);
        CHECK(wrongAxis == 0);
        CHECK(lostCapture == 0);
        CHECK(gridOff == 0);
        CHECK(document.world.blocks.size() == parts.size() + 1);
        TIMED_CHECK(hover.percentile(0.95) < 0.002);
        TIMED_CHECK(hover.worst() < 0.02);
        TIMED_CHECK(w.interval.percentile(0.95) < 1.0 / 60 * 3);
        UNINSTRUMENTED_CHECK(growth < 60);
    }
}

TEST_CASE("Stress: città dell'accesso", "[stress][city]") {
    @autoreleasepool {
        stress::prepare();
        stress::Watchdog watchdog("Città", 30);
        auto scene = city::build();
        REQUIRE(scene);
        double baseline = stress::megabytes();
        stress::Distribution frames, builds;
        int rounds = stress::scaled(40, 200, 800);
        for (int i = 0; i < rounds; ++i) {
            double t = i * 0.05;
            scene->camera->position = {std::sin(t) * 3, 3.1 + std::sin(t * 0.7) * 0.6, 22 - t * 2};
            scene->camera->look({std::sin(t * 0.5) * 4, 5.6, -70});
            for (const auto& person : scene->people) person->animate("Walk", 1.0 / 30);
            double begun = stress::now();
            auto image = ld::render::Renderer::shared().snapshot(scene->world->scene, *scene->camera, 1920, 1080, 4, true);
            frames.add(stress::now() - begun);
            REQUIRE(image.width == 1920);
            if (i % 10 == 0) watchdog.beat();
            if (i % 50 == 49) {
                double started = stress::now();
                auto rebuilt = city::build();
                builds.add(stress::now() - started);
                REQUIRE(rebuilt->parts == scene->parts);
            }
        }
        double growth = stress::megabytes() - baseline;
        stress::record("città", "parti", scene->parts);
        stress::record("città", "avatar", static_cast<double>(scene->people.size()));
        stress::record("città", "render 1920x1080", frames);
        stress::record("città", "ricostruzione della scena", builds);
        auto folder = std::filesystem::temp_directory_path() / ("Ludissey Città " + ld::makeUuid());
        auto store = stress::account(folder);
        store->profile.reset();
        ui::ClientView login(store);
        platform::Window window({"Stress Login", 1440, 900, 760, 560}, [&] { return login.build(); });
        stress::Distribution redraw;
        window.sampled = [&](const platform::FrameSample& sample) {
            if (sample.interfaceRendered) redraw.add(sample.interface);
        };
        window.tick = [&](double, double) { window.invalidate(); };
        window.show();
        double end = stress::now() + stress::seconds(3, 8, 20);
        while (stress::now() < end) {
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.002, true);
            watchdog.beat();
        }
        window.tick = nullptr;
        window.sampled = nullptr;
        window.views = nullptr;
        std::error_code ignored;
        std::filesystem::remove_all(folder, ignored);
        stress::record("città", "ridisegno della Login", redraw);
        stress::record("città", "memoria", ld::Json::object({{"riferimento", baseline}, {"finale", stress::megabytes()}, {"crescita", growth}}));
        INFO("render p50 " << frames.percentile(0.5) * 1000 << " ms, p95 " << frames.percentile(0.95) * 1000 << "; Login p50 " << redraw.percentile(0.5) * 1000 << " ms, p95 " << redraw.percentile(0.95) * 1000 << " su " << redraw.samples.size() << "; crescita " << growth << " MB");
        CHECK(frames.samples.size() == static_cast<std::size_t>(rounds));
        TIMED_CHECK(redraw.samples.size() > 30);
        TIMED_CHECK(frames.percentile(0.95) < 0.25);
        TIMED_CHECK(redraw.percentile(0.95) < 1.0 / 30);
        UNINSTRUMENTED_CHECK(growth < 200);
    }
}

TEST_CASE("Profilo: Workspace", "[stress][profile]") {
    @autoreleasepool {
        Workspace w;
        stress::Watchdog watchdog("Profilo", 30);
        auto& document = w.studio->document;
        populate(document, 60);
        w.run(1.0, watchdog);
        auto stage = w.studio->stage;
        REQUIRE(stage);
        double refresh = 1.0 / 60, length = stress::seconds(3, 8, 20);
        auto phase = [&](const std::string& name, const std::function<void(int)>& frame) {
            int step = 0;
            w.perFrame = [&] { frame(++step); };
            w.reset();
            w.run(length, watchdog);
            w.perFrame = nullptr;
            w.report("profilo " + name);
            stress::record("profilo " + name, "ridisegni completi al secondo", w.interfaceRenders / length);
            WARN(name << ": frame p50 " << w.total.percentile(0.5) * 1000 << " · p90 " << w.total.percentile(0.9) * 1000 << " · p95 " << w.total.percentile(0.95) * 1000 << " · p99 " << w.total.percentile(0.99) * 1000 << " · peggiore " << w.total.worst() * 1000 << " ms; intervallo p50 " << w.interval.percentile(0.5) * 1000 << " · p95 " << w.interval.percentile(0.95) * 1000 << " · p99 " << w.interval.percentile(0.99) * 1000 << " ms; ridisegni completi " << w.interfaceRenders << " su " << w.frames << " frame");
            CHECK(w.frames > 30);
        };
        document.select({document.world.blocks[5].id});
        w.run(0.5, watchdog);
        phase("idle in modifica", [](int) {});
        UNINSTRUMENTED_CHECK(w.interfaceRenders <= 2);
        TIMED_CHECK(w.total.percentile(0.95) < refresh);
        TIMED_CHECK(w.interval.percentile(0.95) < refresh * 1.5);
        w.pointer(ui::PointerPhase::rightDown, 400, 300);
        phase("orbita della camera", [&](int step) { w.pointer(ui::PointerPhase::rightDragged, 400, 300, std::sin(step * 0.05) * 6, std::cos(step * 0.07) * 2); });
        w.pointer(ui::PointerPhase::rightUp, 400, 300);
        UNINSTRUMENTED_CHECK(w.interfaceRenders < w.frames / 10);
        TIMED_CHECK(w.total.percentile(0.95) < refresh);
        TIMED_CHECK(w.interval.percentile(0.95) < refresh * 1.5);
        w.pointer(ui::PointerPhase::rightDown, 400, 300);
        phase("volo della camera", [&](int step) {
            static const int codes[4] = {13, 2, 1, 0};
            for (int code : codes) w.key(code, false);
            w.key(codes[(step / 40) % 4], true);
            w.pointer(ui::PointerPhase::rightDragged, 400, 300, std::sin(step * 0.05) * 4, 0);
        });
        for (int code : {13, 2, 1, 0}) w.key(code, false);
        w.pointer(ui::PointerPhase::rightUp, 400, 300);
        UNINSTRUMENTED_CHECK(w.interfaceRenders < w.frames / 10);
        TIMED_CHECK(w.total.percentile(0.95) < refresh);
        stage->orient(0.6, 0.9, 40, {0, 0, 0});
        stage->refresh();
        w.run(0.3, watchdog);
        document.tool = ld::StudioTool::select;
        int clicks = 0, hits = 0;
        phase("selezione", [&](int step) {
            if (step % 6) return;
            const auto& block = document.world.blocks[static_cast<std::size_t>(1 + (step / 6) % 60)];
            ld::Vec3 at = ld::render::project(*stage->camera(), {stage->size.width, stage->size.height}, {block.x, block.y, block.z});
            w.pointer(ui::PointerPhase::down, at.x, at.y);
            w.pointer(ui::PointerPhase::up, at.x, at.y);
            ++clicks;
            if (document.selectedIds() == std::vector<std::string>{block.id}) ++hits;
        });
        stress::record("profilo selezione", "clic", ld::Json::object({{"clic", clicks}, {"selezioni corrette", hits}}));
        CHECK(clicks > 10);
        CHECK(hits > clicks / 2);
        std::string edited = document.world.blocks[5].id;
        document.select({edited});
        phase("modifica delle Proprietà", [&](int step) {
            if (step % 10) return;
            document.update(edited, [step](ld::WorldBlock& b) { b.x = std::sin(step * 0.01) * 4; });
        });
        stage->orient(0.6, 1.0, 30, {0, 0, 0});
        stage->refresh();
        w.run(0.3, watchdog);
        document.setSelection(std::nullopt);
        document.terrainTool = ld::TerrainTool::add;
        document.terrainBrush.placement = ld::TerrainPlacement::surface;
        document.terrainBrush.radius = 4;
        bool painting = false;
        phase("pennello Terrain su superficie", [&](int step) {
            double x = 400 + std::cos(step * 0.03) * 120, y = 300 + std::sin(step * 0.03) * 80;
            if (!painting) w.pointer(ui::PointerPhase::down, x, y);
            painting = true;
            w.pointer(ui::PointerPhase::dragged, x, y);
            if (step % 40 == 0) {
                w.pointer(ui::PointerPhase::up, x, y);
                painting = false;
            }
        });
        if (painting) w.pointer(ui::PointerPhase::up, 400, 300);
        UNINSTRUMENTED_CHECK(w.interfaceRenders <= w.frames / 30);
        document.terrainTool.reset();
        CHECK_FALSE(document.world.terrain.empty());
        document.playMode = ld::StudioPlayMode::character;
        document.play();
        w.run(0.5, watchdog);
        w.pointer(ui::PointerPhase::rightDown, 400, 300);
        phase("Play con personaggio in movimento", [&](int step) {
            static const int codes[4] = {13, 2, 1, 0};
            for (int code : codes) w.key(code, false);
            w.key(codes[(step / 25) % 4], true);
            w.pointer(ui::PointerPhase::rightDragged, 400, 300, std::sin(step * 0.04) * 5, 0);
        });
        for (int code : {13, 2, 1, 0}) w.key(code, false);
        w.pointer(ui::PointerPhase::rightUp, 400, 300);
        UNINSTRUMENTED_CHECK(w.interfaceRenders < w.frames / 10);
        TIMED_CHECK(w.total.percentile(0.95) < refresh);
        document.stop();
        w.run(0.3, watchdog);
    }
}

TEST_CASE("Profilo: accesso", "[stress][profile][login]") {
    @autoreleasepool {
        stress::prepare();
        stress::Watchdog watchdog("Profilo Login", 30);
        double begun = stress::now();
        ui::Image decoded = ui::loadImage(ld::resourceFolder() / "Backgrounds/Login.jpg");
        double decode = stress::now() - begun;
        REQUIRE(decoded.valid());
        auto folder = std::filesystem::temp_directory_path() / ("Ludissey Profilo Login " + ld::makeUuid());
        auto store = stress::account(folder);
        store->profile.reset();
        double baseline = stress::megabytes();
        ui::ClientView login(store);
        begun = stress::now();
        (void)login.build();
        double firstBuild = stress::now() - begun;
        begun = stress::now();
        (void)login.build();
        double secondBuild = stress::now() - begun;
        platform::Window window({"Profilo Login", 1440, 900, 760, 560}, [&] { return login.build(); });
        stress::Distribution redraw, interval;
        int renders = 0, frames = 0;
        std::optional<double> first;
        window.sampled = [&](const platform::FrameSample& sample) {
            ++frames;
            interval.add(sample.interval);
            if (!sample.interfaceRendered) return;
            ++renders;
            if (!first) first = sample.interface;
            else redraw.add(sample.interface);
        };
        auto pump = [&](double seconds) {
            double end = stress::now() + seconds;
            while (stress::now() < end) {
                CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.002, true);
                watchdog.beat();
            }
        };
        window.show();
        pump(1.0);
        REQUIRE(first);
        window.tick = [&](double, double) { window.invalidate(); };
        pump(stress::seconds(3, 6, 15));
        window.tick = nullptr;
        stress::Distribution resized;
        auto before = redraw.samples.size();
        int sizes = stress::scaled(10, 30, 90);
        NSWindow* target = nil;
        for (NSWindow* candidate in NSApp.windows)
            if ([candidate.title isEqualToString:@"Profilo Login"] && candidate.isVisible) target = candidate;
        REQUIRE(target != nil);
        for (int i = 0; i < sizes; ++i) {
            [target setContentSize:i % 2 ? NSMakeSize(1440, 900) : NSMakeSize(980, 640)];
            pump(0.12);
        }
        for (std::size_t i = before; i < redraw.samples.size(); ++i) resized.add(redraw.samples[i]);
        pump(0.5);
        int idleBefore = renders, idleFrames = frames;
        pump(stress::seconds(3, 6, 15));
        int idleRenders = renders - idleBefore;
        double growth = stress::megabytes() - baseline;
        stress::record("profilo login", "decodifica dello sfondo 3840x2160 (ms)", decode * 1000);
        stress::record("profilo login", "prima costruzione (ms)", firstBuild * 1000);
        stress::record("profilo login", "costruzione successiva (ms)", secondBuild * 1000);
        stress::record("profilo login", "primo disegno (ms)", *first * 1000);
        stress::record("profilo login", "ridisegno", redraw);
        stress::record("profilo login", "ridisegno durante il ridimensionamento", resized);
        stress::record("profilo login", "idle", ld::Json::object({{"frame", frames - idleFrames}, {"ridisegni", idleRenders}}));
        stress::record("profilo login", "memoria (MB)", ld::Json::object({{"prima", baseline}, {"crescita", growth}}));
        WARN("Login: decodifica " << decode * 1000 << " ms; prima costruzione " << firstBuild * 1000 << " ms, successiva " << secondBuild * 1000 << " ms; primo disegno " << *first * 1000 << " ms; ridisegno p50 " << redraw.percentile(0.5) * 1000 << " · p90 " << redraw.percentile(0.9) * 1000 << " · p95 " << redraw.percentile(0.95) * 1000 << " · p99 " << redraw.percentile(0.99) * 1000 << " · peggiore " << redraw.worst() * 1000 << " ms su " << redraw.samples.size() << "; ridimensionamento p50 " << resized.percentile(0.5) * 1000 << " · p95 " << resized.percentile(0.95) * 1000 << " ms su " << resized.samples.size() << "; idle " << idleRenders << " ridisegni su " << frames - idleFrames << " frame; crescita " << growth << " MB");
        CHECK(idleRenders == 0);
        CHECK(resized.samples.size() > static_cast<std::size_t>(sizes / 2));
        TIMED_CHECK(secondBuild < firstBuild + 0.05);
        TIMED_CHECK(redraw.percentile(0.95) < 1.0 / 30);
        UNINSTRUMENTED_CHECK(growth < 120);
        window.sampled = nullptr;
        std::error_code ignored;
        std::filesystem::remove_all(folder, ignored);
    }
}

namespace {

NSWindow* windowTitled(NSString* title) {
    for (NSWindow* candidate in NSApp.windows)
        if ([candidate.title isEqualToString:title] && candidate.isVisible) return candidate;
    return nil;
}

void clipViews(NSView* view, NSMutableArray<NSView*>* out) {
    if ([NSStringFromClass([view class]) isEqualToString:@"LDClipView"]) [out addObject:view];
    for (NSView* child in view.subviews) clipViews(child, out);
}

int escapedFields(NSWindow* window) {
    NSMutableArray<NSView*>* clips = [NSMutableArray array];
    clipViews(window.contentView, clips);
    NSRect bounds = window.contentView.bounds;
    int escaped = 0;
    for (NSView* clip in clips) {
        if (clip.hidden || NSIsEmptyRect(clip.frame)) continue;
        NSRect shown = [clip convertRect:clip.bounds toView:window.contentView];
        if (NSMinX(shown) < NSMinX(bounds) - 1 || NSMinY(shown) < NSMinY(bounds) - 1 || NSMaxX(shown) > NSMaxX(bounds) + 1 || NSMaxY(shown) > NSMaxY(bounds) + 1) ++escaped;
    }
    return escaped;
}

int negativeElements(ui::Surface& surface) {
    int negative = 0;
    for (const auto& element : surface.matching(""))
        if (element.frame.width < 0 || element.frame.height < 0 || !std::isfinite(element.frame.x + element.frame.y + element.frame.width + element.frame.height)) ++negative;
    return negative;
}

int openDescriptors() {
    int count = 0;
    std::error_code ignored;
    for (auto it = std::filesystem::directory_iterator("/dev/fd", ignored); it != std::filesystem::directory_iterator(); it.increment(ignored)) ++count;
    return count;
}

}

TEST_CASE("Tortura: ridimensionamento delle finestre", "[stress][torture][resize]") {
    @autoreleasepool {
        int steps = stress::scaled(60, 300, 1200);
        NSRect screen = NSScreen.mainScreen.visibleFrame;
        std::vector<NSSize> sizes{NSMakeSize(860, 680), NSMakeSize(1180, 760), NSMakeSize(std::max(1200.0, screen.size.width - 40), std::max(700.0, screen.size.height - 80)), NSMakeSize(980, 700), NSMakeSize(860, 680)};
        ld::Json results = ld::Json::object();
        {
            Workspace w;
            stress::Watchdog watchdog("Ridimensionamento Studio", 30);
            populate(w.studio->document, 30);
            w.studio->document.select({w.studio->document.world.blocks[3].id});
            w.run(0.5, watchdog);
            NSWindow* target = windowTitled(@"Stress del Workspace");
            REQUIRE(target != nil);
            int escaped = 0, negative = 0, starved = 0, step = 0;
            std::mt19937 random(31);
            for (; step < steps; ++step) {
                int framesBefore = w.frames;
                NSSize size = step % 7 == 6 ? NSMakeSize(860 + random() % 900, 680 + random() % 300) : sizes[static_cast<std::size_t>(step) % sizes.size()];
                if (step % 50 == 25) [target zoom:nil];
                else [target setContentSize:size];
                if (step % 20 == 10) {
                    w.studio->document.playMode = ld::StudioPlayMode::camera;
                    w.studio->document.play();
                }
                if (step % 20 == 15) w.studio->document.stop();
                w.run(step % 3 == 0 ? 0.08 : 0.03, watchdog);
                escaped += escapedFields(target);
                negative += negativeElements(w.window->surface());
                if (w.frames == framesBefore) ++starved;
            }
            if (w.studio->document.playing) w.studio->document.stop();
            results.set("Ludissey Studio", ld::Json::object({{"passi", step}, {"campi fuori dalla finestra", escaped}, {"elementi con dimensioni negative", negative}, {"passi senza frame", starved}}));
            CHECK(escaped == 0);
            CHECK(negative == 0);
            TIMED_CHECK(starved <= step / 20);
        }
        {
            auto folder = std::filesystem::temp_directory_path() / ("Ludissey Ridimensionamento " + ld::makeUuid());
            auto store = stress::account(folder);
            ui::ClientView client(store);
            platform::Window window({"Ridimensionamento Ludissey", 1180, 760, 860, 600}, [&] { return client.build(); });
            client.invalidated = [&] { window.invalidate(); };
            int frames = 0;
            window.sampled = [&](const platform::FrameSample&) { ++frames; };
            window.show();
            stress::Watchdog watchdog("Ridimensionamento Ludissey", 30);
            auto pump = [&](double seconds) {
                double end = stress::now() + seconds;
                while (stress::now() < end) {
                    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.002, true);
                    ld::fallbackQueue().pump(0.0005);
                    watchdog.beat();
                }
            };
            pump(0.5);
            NSWindow* target = windowTitled(@"Ridimensionamento Ludissey");
            REQUIRE(target != nil);
            int escaped = 0, negative = 0, starved = 0, step = 0;
            const ld::Route routes[] = {ld::Route::home, ld::Route::experiences, ld::Route::avatar, ld::Route::settings};
            for (; step < steps; ++step) {
                int framesBefore = frames;
                NSSize size = sizes[static_cast<std::size_t>(step) % sizes.size()];
                if (step % 50 == 25) [target zoom:nil];
                else [target setContentSize:NSMakeSize(size.width, std::max(600.0, size.height))];
                if (step % 10 == 0) store->navigate(routes[(step / 10) % 4]);
                pump(step % 3 == 0 ? 0.08 : 0.03);
                escaped += escapedFields(target);
                negative += negativeElements(window.surface());
                if (frames == framesBefore) ++starved;
            }
            window.sampled = nullptr;
            client.invalidated = nullptr;
            std::error_code ignored;
            std::filesystem::remove_all(folder, ignored);
            results.set("Ludissey", ld::Json::object({{"passi", step}, {"campi fuori dalla finestra", escaped}, {"elementi con dimensioni negative", negative}, {"passi senza ridisegno", starved}}));
            CHECK(escaped == 0);
            CHECK(negative == 0);
        }
        stress::record("tortura ridimensionamento", "risultati", results);
        INFO(results.dump(true));
    }
}

TEST_CASE("Stress: sessione lunga di Studio", "[stress][soak]") {
    @autoreleasepool {
        double minutes = stress::seconds(0.5, 1.5, 4);
        if (const char* chosen = std::getenv("LUDISSEY_SOAK_MINUTES")) minutes = std::max(0.1, std::atof(chosen));
        Workspace w;
        stress::Watchdog watchdog("Sessione lunga", 60);
        auto& document = w.studio->document;
        populate(document, 40);
        auto script = document.insert("Script", std::string("ServerScriptService"));
        REQUIRE(script);
        document.setSource(*script, "var n = 0\non Time.heartbeat(delta) {\n    n += 1\n}\n");
        w.run(1.0, watchdog);
        auto stage = w.studio->stage;
        REQUIRE(stage);
        NSWindow* target = windowTitled(@"Stress del Workspace");
        REQUIRE(target != nil);
        lim::TypeRegistry registry = lim::standardRegistry();
        struct Sample {
            double minute, megabytes;
            int threads, descriptors;
            double p95, interval;
        };
        std::vector<Sample> samples;
        double started = stress::now(), nextSample = started + 20;
        int activity = 0, played = 0, inserted = 0, removed = 0, undone = 0, painted = 0, typed = 0, resized = 0, scrolled = 0, clicks = 0, drags = 0;
        std::mt19937 random(41);
        w.reset();
        while (stress::now() - started < minutes * 60) {
            switch (activity++ % 10) {
            case 0: {
                w.pointer(ui::PointerPhase::rightDown, 400, 300);
                int step = 0;
                w.perFrame = [&] { w.pointer(ui::PointerPhase::rightDragged, 400, 300, std::sin(++step * 0.05) * 5, 0); };
                w.run(2, watchdog);
                w.perFrame = nullptr;
                w.pointer(ui::PointerPhase::rightUp, 400, 300);
                break;
            }
            case 1: {
                document.tool = ld::StudioTool::select;
                for (int i = 0; i < 20; ++i) {
                    const auto& b = document.world.blocks[1 + random() % (document.world.blocks.size() - 1)];
                    ld::Vec3 at = ld::render::project(*stage->camera(), {stage->size.width, stage->size.height}, {b.x, b.y, b.z});
                    w.pointer(ui::PointerPhase::down, at.x, at.y);
                    w.pointer(ui::PointerPhase::up, at.x, at.y);
                    ++clicks;
                    w.run(0.05, watchdog);
                }
                break;
            }
            case 2: {
                document.tool = ld::StudioTool::move;
                document.select({document.world.blocks.back().id});
                stage->refresh();
                w.run(0.1, watchdog);
                auto gizmo = stage->scene()->root->child("studio.gizmo");
                if (!gizmo || gizmo->hidden) break;
                ld::render::Viewport view{stage->size.width, stage->size.height};
                ld::Vec3 centre = ld::render::project(*stage->camera(), view, gizmo->worldPosition());
                for (double r = 8; r < 160; r += 4) {
                    std::optional<std::pair<int, int>> handle = stage->handleAt({centre.x + r, centre.y});
                    if (!handle) handle = stage->handleAt({centre.x, centre.y - r});
                    if (!handle) continue;
                    ui::Point at = stage->handleAt({centre.x + r, centre.y}) ? ui::Point{centre.x + r, centre.y} : ui::Point{centre.x, centre.y - r};
                    w.pointer(ui::PointerPhase::moved, at.x, at.y);
                    w.pointer(ui::PointerPhase::down, at.x, at.y);
                    int step = 0;
                    w.perFrame = [&] {
                        ++step;
                        w.pointer(ui::PointerPhase::dragged, at.x + std::sin(step * 0.1) * 40, at.y + std::cos(step * 0.1) * 40);
                    };
                    w.run(1.5, watchdog);
                    w.perFrame = nullptr;
                    w.pointer(ui::PointerPhase::up, at.x, at.y);
                    ++drags;
                    break;
                }
                break;
            }
            case 3:
                if (document.world.blocks.size() < 120) {
                    document.addPart(random() % 2 ? "cube" : "sphere");
                    ++inserted;
                }
                w.run(0.3, watchdog);
                document.select({document.world.blocks.back().id});
                document.deleteSelection();
                ++removed;
                w.run(0.3, watchdog);
                break;
            case 4:
                for (int i = 0; i < 6; ++i) {
                    document.undo();
                    w.run(0.05, watchdog);
                }
                for (int i = 0; i < 6; ++i) {
                    document.redo();
                    w.run(0.05, watchdog);
                }
                undone += 12;
                break;
            case 5: {
                document.setSelection(std::nullopt);
                document.terrainTool = random() % 2 ? ld::TerrainTool::add : ld::TerrainTool::raise;
                document.terrainBrush.placement = random() % 2 ? ld::TerrainPlacement::surface : ld::TerrainPlacement::free;
                w.pointer(ui::PointerPhase::down, 400, 320);
                int step = 0;
                w.perFrame = [&] {
                    ++step;
                    w.pointer(ui::PointerPhase::dragged, 400 + std::cos(step * 0.04) * 100, 320 + std::sin(step * 0.04) * 60);
                };
                w.run(1.5, watchdog);
                w.perFrame = nullptr;
                w.pointer(ui::PointerPhase::up, 400, 320);
                document.terrainTool.reset();
                ++painted;
                break;
            }
            case 6:
                document.playMode = played % 2 ? ld::StudioPlayMode::camera : ld::StudioPlayMode::character;
                document.play();
                w.run(2, watchdog);
                document.stop();
                w.run(0.3, watchdog);
                ++played;
                break;
            case 7: {
                std::string source = "let limite = 10\nfor i in 1...limite {\n    print(i)\n}\nx = foo(\nmatch x\n";
                document.openScript(*script);
                for (std::size_t i = 1; i <= source.size(); i += 3) {
                    std::string prefix = source.substr(0, i);
                    lim::tooling::Context context{registry, {}};
                    lim::tooling::hover(prefix, static_cast<std::uint32_t>(prefix.size()), context);
                    lim::tooling::complete(prefix, static_cast<std::uint32_t>(prefix.size()), context);
                    ++typed;
                }
                document.setSource(*script, "var n = 0\non Time.heartbeat(delta) {\n    n += 1\n}\n");
                w.run(0.5, watchdog);
                break;
            }
            case 8:
                for (int i = 0; i < 30; ++i) {
                    w.window->surface().scrollWheel({1100, 300}, 0, i % 10 < 5 ? -12 : 12);
                    w.window->invalidate();
                    w.run(0.03, watchdog);
                    ++scrolled;
                }
                break;
            default:
                for (int i = 0; i < 6; ++i) {
                    [target setContentSize:i % 2 ? NSMakeSize(1180, 760) : NSMakeSize(900 + random() % 300, 700)];
                    w.run(0.1, watchdog);
                    ++resized;
                }
                break;
            }
            if (stress::now() >= nextSample) {
                samples.push_back({(stress::now() - started) / 60, stress::megabytes(), stress::threads(), openDescriptors(), w.total.percentile(0.95) * 1000, w.interval.percentile(0.95) * 1000});
                w.reset();
                nextSample += 20;
            }
        }
        if (w.frames > 30 || samples.empty()) samples.push_back({(stress::now() - started) / 60, stress::megabytes(), stress::threads(), openDescriptors(), w.total.percentile(0.95) * 1000, w.interval.percentile(0.95) * 1000});
        ld::Json series = ld::Json::array();
        for (const auto& s : samples) series.push(ld::Json::object({{"minuto", s.minute}, {"MB", s.megabytes}, {"thread", s.threads}, {"descrittori", s.descriptors}, {"frame p95 ms", s.p95}, {"intervallo p95 ms", s.interval}}));
        stress::record("sessione lunga", "minuti", minutes);
        stress::record("sessione lunga", "attività", ld::Json::object({{"Play", played}, {"inserimenti", inserted}, {"eliminazioni", removed}, {"annulla e ripeti", undone}, {"pennellate", painted}, {"stati dell'editor", typed}, {"scorrimenti", scrolled}, {"ridimensionamenti", resized}, {"clic", clicks}, {"trascinamenti", drags}}));
        stress::record("sessione lunga", "campioni", series);
        REQUIRE(samples.size() >= 2);
        CHECK(samples.back().p95 > 0);
        const auto& first = samples[std::min<std::size_t>(1, samples.size() - 1)];
        const auto& last = samples.back();
        double peakThreads = 0;
        for (const auto& s : samples) peakThreads = std::max<double>(peakThreads, s.threads);
        WARN("sessione lunga " << minutes << " min: MB " << first.megabytes << " → " << last.megabytes << ", thread " << first.threads << " → " << last.threads << " (massimo " << peakThreads << "), descrittori " << first.descriptors << " → " << last.descriptors << ", frame p95 " << first.p95 << " → " << last.p95 << " ms; Play " << played << ", pennellate " << painted << ", trascinamenti " << drags);
        CHECK(played > 0);
        CHECK(drags > 0);
        UNINSTRUMENTED_CHECK(last.megabytes - first.megabytes < 150);
        CHECK(last.threads <= first.threads + 3);
        CHECK(last.descriptors <= first.descriptors + 8);
        TIMED_CHECK(last.p95 < std::max(first.p95 * 2, 1000.0 / 60));
    }
}
