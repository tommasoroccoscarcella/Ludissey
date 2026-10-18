#import <AppKit/AppKit.h>

#include "Engine/Rendering/PlayerScene.hpp"
#include "Golden.hpp"
#include "Studio/StudioStage.hpp"

#include <Catch2/catch_amalgamated.hpp>

#include <cmath>

using namespace ld;

namespace {

Vec3 front(const render::Node& camera) { return camera.orientation.rotate({0, 0, -1}).normalized(); }

ui::PointerEvent pointer(ui::PointerPhase phase, double x, double y) {
    ui::PointerEvent e;
    e.phase = phase;
    e.location = {x, y};
    return e;
}

ui::KeyInput key(int code, bool down) {
    ui::KeyInput k;
    k.keyCode = code;
    k.down = down;
    return k;
}

ui::PointerEvent wheel(int lines) {
    CGEventRef cg = CGEventCreateScrollWheelEvent(nullptr, kCGScrollEventUnitLine, 1, lines);
    NSEvent* event = [NSEvent eventWithCGEvent:cg];
    CFRelease(cg);
    ui::PointerEvent e;
    e.phase = ui::PointerPhase::scroll;
    e.scrollY = event.scrollingDeltaY;
    e.precise = event.hasPreciseScrollingDeltas;
    return e;
}

}

TEST_CASE("Input Player: mouse e WASD", "[input][player]") {
    golden::registry();
    for (auto [label, dx, dy] : std::vector<std::tuple<std::string, double, double>>{{"destra", 40, 0}, {"sinistra", -40, 0}, {"su", 0, 40}, {"giù", 0, -40}}) {
        INFO("mouse " << label);
        auto gameplay = std::make_shared<PlayerGameplay>();
        render::PlayerScene scene(Experience::blank(), AvatarConfiguration::standard("Martina", "female"), gameplay);
        scene.tick(0, 1.0 / 60, 1000);
        Vec3 before = front(scene.camera());
        Vec3 right = world_space::right(std::atan2(-before.x, -before.z));
        scene.look(dx, dy);
        scene.tick(1.0 / 60, 1.0 / 60, 1000 + 1.0 / 60);
        Vec3 after = front(scene.camera());
        double turn = dot(after - before, right), rise = after.y - before.y;
        if (dx > 0) CHECK(turn > 0.05);
        if (dx < 0) CHECK(turn < -0.05);
        if (dy > 0) CHECK(rise > 0.05);
        if (dy < 0) CHECK(rise < -0.05);
    }
    for (auto [label, code, axis] : std::vector<std::tuple<std::string, int, int>>{{"W", 13, 0}, {"S", 1, 1}, {"A", 0, 2}, {"D", 2, 3}}) {
        INFO("tasto " << label);
        auto gameplay = std::make_shared<PlayerGameplay>();
        render::PlayerScene scene(Experience::blank(), AvatarConfiguration::standard("Martina", "female"), gameplay);
        scene.look(120, 0);
        double t = 0;
        scene.tick(t, 1.0 / 60, 1000);
        Vec3 look = front(scene.camera());
        Vec3 flat = Vec3{look.x, 0, look.z}.normalized();
        Vec3 across = world_space::right(std::atan2(-look.x, -look.z));
        Vec3 start = scene.state().position();
        scene.key(static_cast<std::uint16_t>(code), true);
        for (int step = 0; step < 30; ++step) {
            t += 1.0 / 60;
            scene.tick(t, 1.0 / 60, 1000 + t);
        }
        scene.key(static_cast<std::uint16_t>(code), false);
        Vec3 move = scene.state().position() - start;
        move.y = 0;
        bool ok[] = {dot(move, flat) > 0.5, dot(move, flat) < -0.5, dot(move, across) < -0.5, dot(move, across) > 0.5};
        CHECK(ok[axis]);
    }
}

TEST_CASE("Input Studio: camera con mouse e tastiera", "[input][studio]") {
    golden::registry();
    StudioDocument document;
    REQUIRE(document.open(Experience::blank()));
    ui::Size size{400, 300};
    for (auto [label, dx, dy] : std::vector<std::tuple<std::string, double, double>>{{"destra", 40, 0}, {"sinistra", -40, 0}, {"su", 0, 40}, {"giù", 0, -40}}) {
        INFO("tasto destro " << label);
        studio::StudioStage stage(document);
        Vec3 before = front(*stage.camera()), eye = stage.camera()->position;
        Vec3 right = world_space::right(std::atan2(-before.x, -before.z));
        stage.pointer(pointer(ui::PointerPhase::rightDown, 200, 150), size);
        stage.pointer(pointer(ui::PointerPhase::rightDragged, 200 + dx, 150 - dy), size);
        stage.pointer(pointer(ui::PointerPhase::rightUp, 200 + dx, 150 - dy), size);
        Vec3 after = front(*stage.camera());
        double turn = dot(after - before, right), rise = after.y - before.y;
        if (dx > 0) CHECK(turn > 0.05);
        if (dx < 0) CHECK(turn < -0.05);
        if (dy > 0) CHECK(rise > 0.05);
        if (dy < 0) CHECK(rise < -0.05);
        Vec3 moved = stage.camera()->position - eye;
        CHECK(dot(moved, moved) < 1e-6);
    }
    {
        studio::StudioStage stage(document);
        Vec3 view = front(*stage.camera()), eye = stage.camera()->position;
        Vec3 right = world_space::right(std::atan2(-view.x, -view.z));
        stage.pointer(pointer(ui::PointerPhase::otherDown, 200, 150), size);
        stage.pointer(pointer(ui::PointerPhase::otherDragged, 240, 150), size);
        Vec3 pan = stage.camera()->position - eye;
        Vec3 turned = front(*stage.camera()) - view;
        CHECK(dot(pan, right) < -0.05);
        CHECK(dot(turned, turned) < 1e-9);
    }
    for (auto [label, code, axis] : std::vector<std::tuple<std::string, int, int>>{{"W", 13, 0}, {"S", 1, 1}, {"A", 0, 2}, {"D", 2, 3}, {"E", 14, 4}, {"Q", 12, 5}}) {
        INFO("volo " << label);
        studio::StudioStage stage(document);
        Vec3 forward = front(*stage.camera()), eye = stage.camera()->position;
        Vec3 right = world_space::right(std::atan2(-forward.x, -forward.z));
        stage.key(key(code, true));
        stage.tick(1, 0.05);
        stage.key(key(code, false));
        Vec3 move = stage.camera()->position - eye;
        bool ok[] = {dot(move, forward) > 0.1, dot(move, forward) < -0.1, dot(move, right) < -0.1, dot(move, right) > 0.1, move.y > 0.1, move.y < -0.1};
        CHECK(ok[axis]);
    }
    studio::StudioStage zoomed(document);
    Vec3 before = zoomed.camera()->position;
    zoomed.pointer(wheel(3), size);
    CHECK(dot(zoomed.camera()->position, zoomed.camera()->position) < dot(before, before));
}

TEST_CASE("Input Studio: orbita sopra la Baseplate", "[input][studio][baseplate]") {
    @autoreleasepool {
        golden::registry();
        StudioDocument document;
        REQUIRE(document.open(Experience::blank()));
        ui::Size size{400, 300};
        WorldBlock plate = *std::find_if(document.world.blocks.begin(), document.world.blocks.end(), [](const WorldBlock& b) { return b.name == rules::baseplate::name; });
        auto insidePlate = [&](Vec3 e) { return std::abs(e.x - plate.x) < plate.sizeX / 2 && std::abs(e.z - plate.z) < plate.sizeZ / 2 && std::abs(e.y - plate.y) < plate.sizeY / 2; };
        auto platePixels = [&](studio::StudioStage& stage) {
            auto shown = stage.snapshot(160, 120);
            auto saved = document.world.blocks;
            std::erase_if(document.world.blocks, [&](const WorldBlock& b) { return b.id == plate.id; });
            stage.refresh();
            auto hidden = stage.snapshot(160, 120);
            document.world.blocks = saved;
            stage.refresh();
            int count = 0;
            for (std::size_t i = 0; i + 3 < shown.rgba.size(); i += 4) {
                double d = (std::abs(shown.rgba[i] - hidden.rgba[i]) + std::abs(shown.rgba[i + 1] - hidden.rgba[i + 1]) + std::abs(shown.rgba[i + 2] - hidden.rgba[i + 2])) / 255.0;
                if (d > 0.06) ++count;
            }
            return count;
        };
        studio::StudioStage orbiting(document);
        orbiting.pointer(pointer(ui::PointerPhase::rightDown, 200, 150), size);
        double cx = 200, cy = 150;
        int buried = 0;
        for (int step = 0; step < 48; ++step) {
            cx += step % 2 == 0 ? 37 : -11;
            cy -= step < 24 ? -9 : 9;
            orbiting.pointer(pointer(ui::PointerPhase::rightDragged, cx, cy), size);
            if (insidePlate(orbiting.camera()->position)) ++buried;
        }
        CHECK(buried == 0);
        studio::StudioStage low(document);
        low.key(key(12, true));
        for (int i = 0; i < 40; ++i) low.tick(1 + i * 0.05, 0.05);
        low.key(key(12, false));
        Vec3 lowEye = low.camera()->position;
        low.pointer(pointer(ui::PointerPhase::rightDown, 200, 150), size);
        low.pointer(pointer(ui::PointerPhase::rightDragged, 200, 30), size);
        Vec3 shift = low.camera()->position - lowEye;
        CHECK(dot(shift, shift) < 1e-6);
        studio::StudioStage far(document);
        far.pointer(wheel(-40), size);
        studio::StudioStage turned(document);
        turned.pointer(pointer(ui::PointerPhase::rightDown, 200, 150), size);
        for (int step = 1; step <= 10; ++step) turned.pointer(pointer(ui::PointerPhase::rightDragged, 200 + step * 39.27, 150), size);
        studio::StudioStage above(document);
        above.key(key(14, true));
        for (int i = 0; i < 200; ++i) above.tick(1 + i * 0.05, 0.05);
        above.key(key(14, false));
        above.pointer(pointer(ui::PointerPhase::rightDown, 200, 150), size);
        above.pointer(pointer(ui::PointerPhase::rightDragged, 200, -250), size);
        studio::StudioStage initial(document);
        int hiddenPoses = 0;
        for (auto [label, stage, minimum] : std::vector<std::tuple<std::string, studio::StudioStage*, int>>{{"iniziale", &initial, 200}, {"ruotata di 180°", &turned, 200}, {"lontana", &far, 50}, {"rasoterra", &low, 200}}) {
            int pixels = platePixels(*stage);
            UNSCOPED_INFO(label << ": " << pixels << " pixel della Baseplate");
            if (pixels < minimum) ++hiddenPoses;
        }
        int control = platePixels(above);
        UNSCOPED_INFO("controllo con lo sguardo al cielo: " << control);
        CHECK(control < 50);
        CHECK(hiddenPoses == 0);
    }
}
