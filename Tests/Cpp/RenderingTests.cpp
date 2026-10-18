#include "Engine/Core/Resources.hpp"
#include "Engine/Core/Text.hpp"
#include "Engine/Rendering/Renderer.hpp"
#include "Engine/Rendering/World.hpp"
#include "Interface/Canvas.hpp"

#include <Catch2/catch_amalgamated.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>

using namespace ld;
using namespace ld::render;

namespace {

void save(const Image& image, const std::string& name) {
    const char* folder = std::getenv("LUDISSEY_SHOTS");
    if (!folder || image.rgba.empty()) return;
    ui::Image picture = ui::imageFromRgba(image.rgba, image.width, image.height);
    ui::Canvas canvas(image.width, image.height, 1);
    canvas.image(picture, {0, 0, static_cast<double>(image.width), static_cast<double>(image.height)});
    canvas.writePng(std::filesystem::path(folder) / name);
}

NodePtr sun(double intensity, bool shadows) {
    auto node = Node::make("sun");
    Light light;
    light.type = LightType::directional;
    light.intensity = intensity;
    light.castsShadow = shadows;
    node->light = light;
    node->setEuler({-pi / 2, 0, 0});
    return node;
}

NodePtr ambient(double intensity) {
    auto node = Node::make("ambient");
    Light light;
    light.intensity = intensity;
    node->light = light;
    return node;
}

NodePtr camera(Vec3 at, Vec3 target) {
    auto node = Node::make("camera");
    node->camera = Camera{};
    node->position = at;
    node->look(target);
    return node;
}

}

TEST_CASE("Render: illuminazione lineare", "[rendering]") {
    auto& renderer = Renderer::shared();
    REQUIRE(renderer.available());
    struct Probe {
        double ambient, sun, grey;
        int expected;
    };
    for (Probe p : {Probe{500, 0, 64, 64 + 0}, Probe{100, 500, 64, 49}, Probe{500, 500, 64, 64}, Probe{500, 1, 64, 44}, Probe{650, 900, 40, 51}, Probe{1000, 1000, 20, 31}}) {
        Scene scene;
        scene.background.color = {0, 0, 0, 1};
        scene.root->add(ambient(p.ambient));
        scene.root->add(sun(p.sun, false));
        auto plane = Node::make(shapes::plane(10, 10));
        plane->geometry->material().diffuse = {p.grey / 255, p.grey / 255, p.grey / 255, 1};
        plane->setEuler({-pi / 2, 0, 0});
        scene.root->add(plane);
        auto eye = camera({0, 5, 0}, {0, 0, 0.0001});
        scene.root->add(eye);
        Image image = renderer.snapshot(scene, *eye, 16, 16, 1, false);
        REQUIRE(image.width == 16);
        int red = image.rgba[(8 * 16 + 8) * 4];
        if (p.sun > 0) CHECK(std::abs(red - p.expected) <= 1);
    }
}

TEST_CASE("Render: primitive, ombre e wireframe", "[rendering]") {
    Scene scene;
    scene.background.kind = BackgroundKind::gradient;
    scene.background.top = Color::hex("4A86D8");
    scene.background.bottom = Color::hex("A9C8EE");
    auto light = sun(900, true);
    light->light->deferredShadows = true;
    light->light->shadowColor = {0, 0, 0, 0.45};
    light->setEuler({-pi / 3, -pi / 5, 0});
    scene.root->add(light);
    scene.root->add(ambient(650));
    auto ground = Node::make(shapes::box(40, 1, 40, 0.02));
    ground->geometry->material().diffuse = Color::hex("8E9299");
    ground->position = {0, -0.5, 0};
    scene.root->add(ground);
    auto cube = Node::make(shapes::box(1, 1, 1, 0.02));
    cube->geometry->material().diffuse = Color::hex("6730EE");
    cube->position = {1.5, 1, 1};
    cube->scale = {2, 2, 2};
    cube->setEulerDegrees(0, 30, 0);
    scene.root->add(cube);
    auto ball = Node::make(shapes::sphere(0.5));
    ball->geometry->material().diffuse = Color::hex("6730EE");
    ball->position = {-1.5, 0.5, 1};
    scene.root->add(ball);
    auto selection = Node::make(shapes::box(1, 1, 1, 0));
    selection->geometry->material().lighting = Lighting::constant;
    selection->geometry->material().wireframe = true;
    selection->geometry->material().diffuse = Color::hex("6730EE");
    selection->position = cube->position;
    selection->orientation = cube->orientation;
    selection->scale = {2.08, 2.08, 2.08};
    scene.root->add(selection);
    auto ring = Node::make(shapes::torus(1, 0.035));
    ring->geometry->material().lighting = Lighting::constant;
    ring->geometry->material().diffuse = Color::hex("2FA84F");
    ring->position = {0, 2.5, 0};
    scene.root->add(ring);
    auto eye = camera({8, 7, 8}, {0, 0.5, 0});
    eye->camera->zNear = 0.05;
    eye->camera->zFar = 400;
    scene.root->add(eye);
    Image image = Renderer::shared().snapshot(scene, *eye, 430, 460, 4, true);
    REQUIRE(image.width == 430);
    save(image, "render-primitives.png");
    auto top = &image.rgba[(2 * 430 + 215) * 4];
    CHECK(top[2] > top[0]);
    auto hits = hitTest(scene, *eye, {430, 460}, 215, 230, {});
    CHECK_FALSE(hits.empty());
}

TEST_CASE("Avatar preview renders the standard character", "[rendering][avatar]") {
    ld::setResourceFolder(std::filesystem::path(std::getenv("LUDISSEY_ROOT") ? std::getenv("LUDISSEY_ROOT") : ".") / "Resources");
    Scene scene;
    scene_factory::base(scene);
    auto look = AvatarConfiguration::standard("Martina", std::string(rules::avatar::standard::bodyPreset));
    auto body = scene_factory::avatar(look);
    REQUIRE(body);
    scene.root->add(body->node);
    auto plinth = Node::make(shapes::cylinder(1.0, 0.08));
    plinth->position = {0, -0.06, 0};
    plinth->geometry->material().diffuse = Color::hex("D1C6F2");
    scene.root->add(plinth);
    auto eye = camera({3, 1.8, 4}, {0, 0.9, 0});
    scene.root->add(eye);
    Image image = Renderer::shared().snapshot(scene, *eye, 380, 500, 4, true);
    REQUIRE(image.width == 380);
    REQUIRE(image.height == 500);
    REQUIRE(image.rgba.size() == 380 * 500 * 4);
    save(image, "render-avatar.png");
    auto centre = &image.rgba[(250 * 380 + 190) * 4];
    auto corner = &image.rgba[(5 * 380 + 5) * 4];
    CHECK(corner[0] == 0xE9);
    CHECK((centre[0] != 0xE9 || centre[1] != 0xE6));
}

TEST_CASE("Render: cartellino del nome", "[rendering][nametag]") {
    auto& renderer = Renderer::shared();
    REQUIRE(renderer.available());
    const std::string label = "Ludissey jp";
    ui::Font font{"Helvetica", 96, ui::Weight::bold};
    ui::Size measured = ui::measureText(label, {font, ui::Color::white()}, std::nullopt);
    int referenceWidth = static_cast<int>(std::ceil(measured.width)) + 4, referenceHeight = static_cast<int>(std::ceil(measured.height)) + 4;
    ui::Canvas reference(referenceWidth, referenceHeight, 1);
    reference.text(label, {2, 2, measured.width, measured.height}, {font, ui::Color::white()});
    const std::uint8_t* drawn = reference.pixels();
    REQUIRE(drawn);
    int inkLeft = referenceWidth, inkRight = -1, inkTop = referenceHeight, inkBottom = -1;
    for (int y = 0; y < referenceHeight; ++y)
        for (int x = 0; x < referenceWidth; ++x)
            if (drawn[static_cast<std::size_t>((y * referenceWidth + x) * 4 + 3)] > 128) inkLeft = std::min(inkLeft, x), inkRight = std::max(inkRight, x), inkTop = std::min(inkTop, y), inkBottom = std::max(inkBottom, y);
    REQUIRE(inkRight > inkLeft);
    std::vector<double> columns(static_cast<std::size_t>(inkRight - inkLeft + 1), 0);
    for (int y = inkTop; y <= inkBottom; ++y)
        for (int x = inkLeft; x <= inkRight; ++x)
            if (drawn[static_cast<std::size_t>((y * referenceWidth + x) * 4 + 3)] > 128) columns[static_cast<std::size_t>(x - inkLeft)] += 1;
    auto correlation = [](const std::vector<double>& a, const std::vector<double>& b) {
        double ma = 0, mb = 0;
        for (std::size_t i = 0; i < a.size(); ++i) ma += a[i], mb += b[i];
        ma /= static_cast<double>(a.size());
        mb /= static_cast<double>(b.size());
        double num = 0, da = 0, db = 0;
        for (std::size_t i = 0; i < a.size(); ++i) num += (a[i] - ma) * (b[i] - mb), da += (a[i] - ma) * (a[i] - ma), db += (b[i] - mb) * (b[i] - mb);
        return num / std::sqrt(std::max(1e-12, da * db));
    };
    auto resample = [](const std::vector<double>& source, std::size_t size) {
        std::vector<double> out(size, 0);
        for (std::size_t i = 0; i < size; ++i) out[i] = source[std::min(source.size() - 1, i * source.size() / size)];
        return out;
    };
    for (double yaw : {0.0, pi / 2, pi, -pi * 0.75}) {
        INFO("angolo camera " << yaw);
        Scene scene;
        scene.background.color = {0.2, 0.4, 0.8, 1};
        scene.root->add(ambient(1000));
        auto tag = name_tag::make(label);
        tag->position = {0, 0, 0};
        tag->scale = {1, 1, 1};
        scene.root->add(tag);
        auto eye = camera({std::sin(yaw) * 9, 0, std::cos(yaw) * 9}, {0, 0, 0});
        scene.root->add(eye);
        Image image = renderer.snapshot(scene, *eye, 640, 200, 1, false);
        save(image, "cartellino " + std::to_string(static_cast<int>(std::round(yaw * 180 / pi))) + ".png");
        int left = image.width, right = -1, top = image.height, bottom = -1;
        auto ink = [&](int x, int y) {
            const auto* p = &image.rgba[static_cast<std::size_t>((y * image.width + x) * 4)];
            return p[0] > 200 && p[1] > 200 && p[2] > 200;
        };
        for (int y = 0; y < image.height; ++y)
            for (int x = 0; x < image.width; ++x)
                if (ink(x, y)) left = std::min(left, x), right = std::max(right, x), top = std::min(top, y), bottom = std::max(bottom, y);
        REQUIRE(right > left + 20);
        REQUIRE(bottom > top + 5);
        std::vector<double> seenColumns(static_cast<std::size_t>(right - left + 1), 0);
        for (int y = top; y <= bottom; ++y)
            for (int x = left; x <= right; ++x)
                if (ink(x, y)) seenColumns[static_cast<std::size_t>(x - left)] += 1;
        auto expectedColumns = resample(columns, seenColumns.size());
        auto reversedColumns = expectedColumns;
        std::reverse(reversedColumns.begin(), reversedColumns.end());
        double straight = correlation(seenColumns, expectedColumns), mirrored = correlation(seenColumns, reversedColumns);
        auto centroid = [&](int from, int to) {
            double sum = 0, weight = 0;
            for (int y = from; y <= to; ++y)
                for (int x = left; x <= right; ++x)
                    if (ink(x, y)) sum += x - left, weight += 1;
            return weight > 0 ? sum / weight / (right - left) : 0.5;
        };
        int band = std::max(2, (bottom - top) / 5);
        double high = centroid(top, top + band), low = centroid(bottom - band, bottom);
        INFO("colonne dritte " << straight << ", specchiate " << mirrored << "; baricentro in alto " << high << ", in basso " << low);
        CHECK(straight > mirrored);
        CHECK(low > high + 0.1);
    }
}

TEST_CASE("Render: ombre stabili", "[rendering][shadows]") {
    auto& renderer = Renderer::shared();
    REQUIRE(renderer.available());
    Scene scene;
    scene.background.color = {0.5, 0.7, 1, 1};
    scene.root->add(ambient(500));
    auto light = sun(900, true);
    light->setEuler({-0.9, 0.6, 0});
    scene.root->add(light);
    auto ground = Node::make(shapes::box(512, 1, 512, 0));
    ground->position = {0, -0.5, 0};
    scene.root->add(ground);
    auto block = Node::make(shapes::box(2, 4, 2, 0));
    block->position = {3, 2, 0};
    scene.root->add(block);
    double smallest = 1e9, largest = 0;
    for (int step = 0; step < 24; ++step) {
        double yaw = step * pi / 12;
        auto eye = camera({std::sin(yaw) * 14, 7, std::cos(yaw) * 14}, {0, 1, 0});
        scene.root->add(eye);
        renderer.snapshot(scene, *eye, 160, 100, 1, true);
        double texel = renderer.statistics().shadowTexel;
        eye->remove();
        REQUIRE(texel > 0);
        smallest = std::min(smallest, texel);
        largest = std::max(largest, texel);
    }
    INFO("texel d'ombra da " << smallest << " a " << largest);
    CHECK(largest / smallest < 1.01);
}

TEST_CASE("Render: materiali delle Part", "[rendering][materials]") {
    Experience world = Experience::blank();
    const std::vector<std::string> materials{"plastic", "wood", "metal", "glass"};
    for (std::size_t i = 0; i < materials.size(); ++i) {
        WorldBlock part;
        part.id = makeUuid();
        part.name = materials[i];
        part.material = materials[i];
        part.x = -4.5 + 3.0 * static_cast<double>(i);
        part.y = 1.5;
        part.setSize(2.4);
        part.rotationY = 25;
        world.blocks.push_back(part);
    }
    auto scene = worldScene(world);
    auto eye = camera({0, 4.5, 11}, {0, 1.4, 0});
    eye->camera->zNear = 0.05;
    scene->scene.root->add(eye);
    const int width = 640, height = 360;
    Image image = Renderer::shared().snapshot(scene->scene, *eye, width, height, 4, true);
    REQUIRE(image.width == width);
    save(image, "materiali.png");
    struct Stats {
        double mean[3] = {0, 0, 0};
        double deviation = 0;
    };
    std::vector<Stats> stats;
    for (std::size_t i = 0; i < materials.size(); ++i) {
        Vec3 centre = project(*eye, {static_cast<double>(width), static_cast<double>(height)}, {world.blocks[i + 1].x, 1.5, world.blocks[i + 1].z + 1.2});
        std::vector<double> lumas;
        Stats s;
        for (int dy = -12; dy <= 12; ++dy)
            for (int dx = -12; dx <= 12; ++dx) {
                int x = std::clamp(static_cast<int>(centre.x) + dx, 0, width - 1), y = std::clamp(static_cast<int>(centre.y) + dy, 0, height - 1);
                const auto* p = &image.rgba[static_cast<std::size_t>(y * width + x) * 4];
                for (int c = 0; c < 3; ++c) s.mean[c] += p[c];
                lumas.push_back(0.3 * p[0] + 0.59 * p[1] + 0.11 * p[2]);
            }
        for (double& m : s.mean) m /= static_cast<double>(lumas.size());
        double average = 0;
        for (double l : lumas) average += l / static_cast<double>(lumas.size());
        for (double l : lumas) s.deviation += (l - average) * (l - average) / static_cast<double>(lumas.size());
        s.deviation = std::sqrt(s.deviation);
        stats.push_back(s);
    }
    auto distance = [&](std::size_t a, std::size_t b) {
        double d = 0;
        for (int c = 0; c < 3; ++c) d += std::abs(stats[a].mean[c] - stats[b].mean[c]);
        return d / 3 + std::abs(stats[a].deviation - stats[b].deviation);
    };
    for (std::size_t i = 0; i < materials.size(); ++i) INFO(materials[i] << ": media " << stats[i].mean[0] << "/" << stats[i].mean[1] << "/" << stats[i].mean[2] << " deviazione " << stats[i].deviation);
    std::string summary;
    for (std::size_t i = 0; i < materials.size(); ++i) summary += materials[i] + " " + std::to_string(stats[i].mean[0]) + "/" + std::to_string(stats[i].mean[2]) + " σ" + std::to_string(stats[i].deviation) + "; ";
    INFO(summary);
    CHECK(stats[1].deviation > stats[0].deviation + 4);
    CHECK((stats[2].mean[0] + stats[2].mean[1] + stats[2].mean[2]) < (stats[0].mean[0] + stats[0].mean[1] + stats[0].mean[2]) - 15);
    CHECK(stats[3].mean[2] > stats[3].mean[0] + 6);
    for (std::size_t a = 0; a < materials.size(); ++a)
        for (std::size_t b = a + 1; b < materials.size(); ++b) CHECK(distance(a, b) > 5);
}
