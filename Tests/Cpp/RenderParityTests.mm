#include "Engine/Character/Character.hpp"
#include "Engine/Core/Resources.hpp"
#include "Engine/Rendering/Renderer.hpp"
#include "Engine/Rendering/World.hpp"
#include "Golden.hpp"
#include "Pixels.hpp"
#include "Studio/StudioStage.hpp"

#include <Catch2/catch_amalgamated.hpp>

#include <cmath>
#include <cstdlib>

using namespace ld;
using namespace ld::render;
using pixels::Pixels;

namespace {

Pixels shoot(Scene& scene, Vec3 eye, Vec3 target, int width, int height, int samples, double time) {
    auto camera = Node::make("parity.camera");
    camera->camera = Camera{};
    camera->camera->zFar = 400;
    camera->position = eye;
    camera->look(target);
    scene.root->add(camera);
    scene.time = time;
    Image image = Renderer::shared().snapshot(scene, *camera, width, height, samples, true);
    camera->remove();
    REQUIRE(image.width == width);
    return {image.width, image.height, image.rgba};
}

Pixels baseline(const std::string& name) { return pixels::load(golden::root() / "Tests/Golden/Render" / (name + ".png")); }

void keep(const Pixels& image, const std::string& name) {
    if (const char* folder = std::getenv("LUDISSEY_SHOTS")) pixels::save(image, std::filesystem::path(folder) / ("render " + name + ".png"));
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

struct Grass {
    double green = 0;
    double deviation = 0;
};

Grass grass(const Pixels& image) {
    std::vector<double> light;
    for (std::size_t i = 0; i < image.rgba.size(); i += 4) {
        double r = image.rgba[i] / 255.0, g = image.rgba[i + 1] / 255.0, b = image.rgba[i + 2] / 255.0;
        if (g > r * 1.05 && g > b * 1.05) light.push_back(0.3 * r + 0.59 * g + 0.11 * b);
    }
    double mean = 0;
    for (double l : light) mean += l;
    mean /= std::max<std::size_t>(1, light.size());
    double squares = 0;
    for (double l : light) squares += (l - mean) * (l - mean);
    return {static_cast<double>(light.size()) / (image.rgba.size() / 4), std::sqrt(squares / std::max<std::size_t>(1, light.size()))};
}

int changed(const Pixels& a, const Pixels& b) {
    int count = 0;
    for (std::size_t i = 0; i < a.rgba.size(); i += 4)
        if (a.rgba[i] != b.rgba[i] || a.rgba[i + 1] != b.rgba[i + 1] || a.rgba[i + 2] != b.rgba[i + 2]) ++count;
    return count;
}

}

TEST_CASE("Render: erba del terreno", "[rendering][parity][terrain]") {
    @autoreleasepool {
        Experience world = Experience::blank();
        world.blocks.clear();
        world.terrain.generate(TerrainSettings{9, 1.5, 2, -20});
        auto scene = worldScene(world);
        Pixels actual = shoot(scene->scene, {0, 9, 14}, {0, 2, 0}, 200, 150, 1, 0);
        keep(actual, "Terreno erba");
        Pixels expected = baseline("Terreno erba");
        Grass mine = grass(actual), theirs = grass(expected), flat = grass(baseline("Terreno erba senza shader"));
        double difference = structure(actual, expected, 20, 15);
        UNSCOPED_INFO("erba C++ " << mine.green << " / " << mine.deviation << ", riferimento " << theirs.green << " / " << theirs.deviation << ", struttura " << difference);
        CHECK(mine.green > 0.5);
        CHECK(std::abs(mine.green - theirs.green) < 0.01);
        CHECK(std::abs(mine.deviation - theirs.deviation) < theirs.deviation * 0.1);
        CHECK(mine.deviation > flat.deviation * 3);
        CHECK(mine.deviation > 0.01);
        CHECK(difference < 2.5);
    }
}

TEST_CASE("Render: acqua del terreno", "[rendering][parity][terrain][water]") {
    @autoreleasepool {
        Terrain island;
        island.generate(TerrainSettings{144, 8, 0, 0});
        Experience world = Experience::blank();
        world.blocks.clear();
        world.terrain = island;
        WorldBlock cube;
        cube.name = "Sommersa";
        cube.color = "D8473B";
        cube.anchored = true;
        cube.x = -22;
        cube.z = -2;
        cube.y = -1.5;
        world.blocks.push_back(cube);
        std::vector<double> before;
        for (double x = -24; x <= 24; x += 8)
            for (double z = -24; z <= 24; z += 8) before.push_back(island.sample(x, -0.5, z, true));
        auto scene = worldScene(world);
        auto check = [&](const Pixels& actual, const std::string& name) {
            keep(actual, name);
            auto path = golden::root() / "Tests/Golden/Render" / (name + ".png");
            if (golden::writing()) pixels::save(actual, path);
            double difference = structure(actual, pixels::load(path), 20, 15);
            UNSCOPED_INFO(name << ": struttura " << difference);
            CHECK(difference < 2.5);
        };
        Pixels above = shoot(scene->scene, {-8, 30, 30}, {-18, 0, 0}, 640, 400, 4, 0);
        Pixels again = shoot(scene->scene, {-8, 30, 30}, {-18, 0, 0}, 640, 400, 4, 0);
        Pixels moving = shoot(scene->scene, {-8, 30, 30}, {-18, 0, 0}, 640, 400, 4, 1.3);
        CHECK(changed(above, again) == 0);
        CHECK(changed(above, moving) > 500);
        check(above, "Acqua vista da sopra");
        check(shoot(scene->scene, {-30, 2, 4}, {-10, 0.5, 0}, 640, 400, 4, 0), "Acqua radente");
        Pixels below = shoot(scene->scene, {-28, -2.5, 4}, {-18, -1, -2}, 640, 400, 4, 0);
        check(below, "Acqua vista da sotto");
        Pixels shore = shoot(scene->scene, {-4, 6, 8}, {-13, 0, 0}, 640, 400, 4, 0);
        check(shore, "Riva");
        Pixels submerged = shoot(scene->scene, {-22, 6, 6}, {cube.x, cube.y, cube.z}, 640, 400, 4, 0);
        check(submerged, "Parte sommersa");
        check(shoot(scene->scene, {0, 12, 14}, {0, 6, 60}, 640, 400, 4, 0), "Mare aperto");
        std::size_t centre = static_cast<std::size_t>((200 * 640 + 320) * 4);
        double r = submerged.rgba[centre] / 255.0, g = submerged.rgba[centre + 1] / 255.0, bl = submerged.rgba[centre + 2] / 255.0;
        UNSCOPED_INFO("parte sommersa " << r << " " << g << " " << bl);
        CHECK(r > 0.25);
        CHECK(bl > 0.15);
        CHECK(r < 0.9);
        std::size_t corner = static_cast<std::size_t>((20 * 640 + 20) * 4);
        CHECK(below.rgba[corner + 2] > below.rgba[corner]);
        std::vector<double> after;
        for (double x = -24; x <= 24; x += 8)
            for (double z = -24; z <= 24; z += 8) after.push_back(island.sample(x, -0.5, z, true));
        CHECK(after == before);
    }
}

TEST_CASE("Render: volti dei personaggi", "[rendering][parity][face]") {
    @autoreleasepool {
        ld::setResourceFolder(golden::root() / "Resources");
        auto dark = [](const Pixels& image) {
            int count = 0;
            for (std::size_t i = 0; i < image.rgba.size(); i += 4)
                if (image.rgba[i] + image.rgba[i + 1] + image.rgba[i + 2] < 150) ++count;
            return count;
        };
        for (auto [preset, title] : {std::pair{"male", "Volto maschile"}, std::pair{"female", "Volto femminile"}}) {
            INFO(title);
            Scene scene;
            scene_factory::illuminate(scene, Experience::blank().lighting);
            auto body = scene_factory::avatar(AvatarConfiguration::standard("Martina", preset));
            REQUIRE(body);
            scene.root->add(body->node);
            body->animate("Idle", 0);
            Bounds head{{0, 0, 0}, {0, 0, 0}};
            for (const auto& child : body->node->children())
                if (child->name.rfind("Head_", 0) == 0 && child->geometry) head = child->geometry->bounds();
            REQUIRE(head.extent().y > 0);
            Vec3 centre = head.center();
            double reach = head.extent().y * 2.6;
            auto check = [&](const Pixels& actual, const std::string& name) {
                keep(actual, name);
                auto path = golden::root() / "Tests/Golden/Render" / (name + ".png");
                if (golden::writing()) pixels::save(actual, path);
                double difference = structure(actual, pixels::load(path), 16, 16);
                UNSCOPED_INFO(name << ": struttura " << difference);
                CHECK(difference < 2.5);
            };
            Pixels front = shoot(scene, centre + Vec3{0, 0, reach}, centre, 320, 320, 4, 0);
            CHECK(dark(front) > 400);
            check(front, title);
            Pixels side = shoot(scene, centre + Vec3{reach * 0.7, 0, reach * 0.7}, centre, 320, 320, 4, 0);
            check(side, std::string(title) + " tre quarti");
            Pixels back = shoot(scene, centre + Vec3{0, 0, -reach}, centre, 320, 320, 4, 0);
            CHECK(dark(back) < dark(front) / 4);
            body->animate("Walk", 0.35);
            Pixels walking = shoot(scene, centre + Vec3{0, 0, reach}, centre, 320, 320, 4, 0);
            CHECK(dark(walking) > dark(front) / 2);
        }
    }
}

TEST_CASE("Render: inquadrature del Player", "[rendering][parity][player]") {
    @autoreleasepool {
        ld::setResourceFolder(golden::root() / "Resources");
        AvatarConfiguration look = AvatarConfiguration::standard("Esploratrice", "female");
        look.bodyColors = {{"head", "C68A5E"}, {"torso", "2FA84F"}, {"leftArm", "C68A5E"}, {"rightArm", "C68A5E"}, {"leftLeg", "1B1D21"}, {"rightLeg", "1B1D21"}};
        auto frame = [&](const Experience& world, double x, double z, ld::CharacterInput walk, double seconds, double yaw, const std::string& name) {
            auto scene = worldScene(world);
            auto body = scene_factory::avatar(look);
            REQUIRE(body);
            scene->scene.root->add(body->node);
            auto blocks = world.workspaceParts();
            ld::CharacterState state = motion::spawn(blocks, world.terrain, x, z);
            for (int i = 0; i < static_cast<int>(seconds * 60); ++i) state = motion::step(state, walk, yaw, blocks, world.terrain, 1.0 / 60);
            body->node->position = state.position();
            body->node->setYaw(state.yaw);
            body->animate(state.swimming ? "Idle" : "Walk", 0.3);
            Vec3 head{state.x, state.y + 0.9, state.z};
            auto placed = camera_rig::place(head, yaw + 0.5, 0.3, 7, blocks, world.terrain);
            Pixels actual = shoot(scene->scene, placed.position, head, 960, 600, 4, 1);
            keep(actual, name);
            if (golden::writing()) pixels::save(actual, golden::root() / "Tests/Golden/Render" / (name + ".png"));
            double difference = structure(actual, baseline(name), 24, 15);
            UNSCOPED_INFO(name << ": struttura " << difference);
            CHECK(difference < 2.5);
            return state;
        };
        ld::CharacterInput forward;
        forward.forward = 1;
        auto plain = frame(Experience::blank(), 0, 5, forward, 0.5, 0, "QA Player piano");
        CHECK(plain.grounded);
        Experience hills = Experience::blank();
        hills.blocks.clear();
        hills.terrain.generate(TerrainSettings{4242, 6, 1, -20});
        CHECK(frame(hills, 0, 0, forward, 1.5, 0.6, "QA Player pendenza").grounded);
        Experience lake = Experience::blank();
        lake.blocks.clear();
        lake.terrain.generate(TerrainSettings{5, 0, -2, 1});
        CHECK(frame(lake, 0, 0, ld::CharacterInput{}, 1, 0.3, "QA Player nuoto").swimming);
    }
}

TEST_CASE("Render: viewport di Studio", "[rendering][parity][studio]") {
    @autoreleasepool {
        golden::registry();
        Experience world = Experience::blank();
        world.terrain.generate(TerrainSettings{2026, 6, 1, 0});
        const char* colors[] = {"3F14A9", "EFEAFD", "2FA84F", "22803C"};
        for (int index = 0; index < 4; ++index) {
            WorldBlock block;
            block.name = "Torre" + std::to_string(index);
            block.x = index * 4 - 6;
            block.z = -6;
            block.y = 3;
            block.sizeY = 4;
            block.color = colors[index];
            world.blocks.push_back(block);
        }
        StudioDocument document;
        REQUIRE(document.open(world));
        auto wheel = [](int lines) {
            CGEventRef cg = CGEventCreateScrollWheelEvent(nullptr, kCGScrollEventUnitLine, 1, lines);
            NSEvent* event = [NSEvent eventWithCGEvent:cg];
            CFRelease(cg);
            ui::PointerEvent e;
            e.phase = ui::PointerPhase::scroll;
            e.scrollY = event.scrollingDeltaY;
            e.precise = event.hasPreciseScrollingDeltas;
            return e;
        };
        auto check = [&](studio::StudioStage& stage, const std::string& name) {
            auto image = stage.snapshot(960, 600);
            Pixels actual{image.width, image.height, image.rgba};
            keep(actual, name);
            if (golden::writing()) pixels::save(actual, golden::root() / "Tests/Golden/Render" / (name + ".png"));
            double difference = structure(actual, baseline(name), 24, 15);
            UNSCOPED_INFO(name << ": struttura " << difference);
            CHECK(difference < 2.5);
        };
        {
            studio::StudioStage stage(document);
            check(stage, "QA Studio iniziale");
        }
        {
            studio::StudioStage stage(document);
            ui::PointerEvent down;
            down.phase = ui::PointerPhase::rightDown;
            down.location = {480, 300};
            stage.pointer(down, {960, 600});
            ui::PointerEvent drag;
            drag.phase = ui::PointerPhase::rightDragged;
            drag.location = {740, 260};
            stage.pointer(drag, {960, 600});
            check(stage, "QA Studio ruotata");
        }
        {
            studio::StudioStage stage(document);
            stage.pointer(wheel(-40), {960, 600});
            check(stage, "QA Studio lontana e Terrain lontano");
        }
        {
            studio::StudioStage stage(document);
            stage.pointer(wheel(14), {960, 600});
            check(stage, "QA Terrain vicino con erba");
        }
        {
            document.terrainEditing = true;
            document.terrainMode = "sea";
            document.terrainRegion = TerrainRegion::snapped({-14, -16, -10}, {10, 0.75, 12});
            studio::StudioStage stage(document);
            stage.size = {960, 600};
            stage.refresh();
            REQUIRE(stage.regionShown());
            check(stage, "QA Studio regione");
            document.closeTerrainTool();
            document.terrainTool = TerrainTool::add;
            document.terrainBrush.placement = TerrainPlacement::surface;
            ui::PointerEvent hover;
            hover.phase = ui::PointerPhase::moved;
            hover.location = {480, 330};
            stage.pointer(hover, {960, 600});
            REQUIRE(stage.brushShown());
            CHECK(stage.brushDisc());
            check(stage, "QA Studio pennello");
            document.terrainBrush.placement = TerrainPlacement::free;
            stage.pointer(hover, {960, 600});
            CHECK_FALSE(stage.brushDisc());
            document.closeTerrainTool();
            document.terrainEditing = false;
        }
    }
}
