#include "City.hpp"
#include "Scenery.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <random>
#include <string>

namespace city {

using namespace ld;

namespace {

const std::array<const char*, 9> facades{"C8664A", "E9DFC8", "2F8F8A", "4D5B6E", "E0A93B", "E46F5E", "8C7BB8", "D9CDB4", "5F7F9C"};
const std::array<const char*, 8> paints{"E0392B", "2E6FF2", "F2C230", "2FA84F", "F2F3F5", "1B1D21", "E46F5E", "6730EE"};

struct Builder : scenery::Builder {
    Builder() : scenery::Builder(20261003, "city", "Città") {}

    void ground() {
        part({0, -0.5, -120}, {420, 1, 420}, "5B5F68");
        for (double side : {-1.0, 1.0}) {
            part({side * 12.25, 0.09, -120}, {4.5, 0.18, 420}, "C9C3B8");
            part({side * 10.05, 0.09, -120}, {0.1, 0.2, 420}, "9E978A");
        }
        for (double z = 24; z > -250; z -= 7) {
            if (z < -26 && z > -46) continue;
            part({0, 0.012, z}, {0.18, 0.02, 3.4}, "F2D35B");
        }
        for (double side : {-1.0, 1.0}) part({side * 5.2, 0.012, -120}, {0.14, 0.02, 420}, "E9EBEE");
        part({0, 0.004, -36}, {420, 0.01, 18}, "565A63");
        for (double side : {-1.0, 1.0}) part({0, 0.09, -36 + side * 11.25}, {420, 0.18, 4.5}, "C9C3B8");
        for (double x = -9; x <= 9.01; x += 1.5) part({x, 0.014, -24.6}, {0.75, 0.02, 3.2}, "F2F3F5");
        for (double x = -9; x <= 9.01; x += 1.5) part({x, 0.014, -47.4}, {0.75, 0.02, 3.2}, "F2F3F5");
        for (double z = -30; z >= -42; z -= 1.5) part({-12.2, 0.2, z}, {3.2, 0.02, 0.75}, "F2F3F5");
    }

    void building(double side, double front, double depth, double length, double height, double z) {
        std::string color = facades[static_cast<std::size_t>(pick(static_cast<int>(facades.size())))];
        double x = side * (front + depth / 2);
        part({x, height / 2, z}, {depth, height, length}, color);
        part({x, height + 0.4, z}, {depth + 0.4, 0.8, length + 0.4}, "E9EBEE");
        double face = side * front - side * 0.06;
        int floors = z < -40 ? 0 : std::max(2, static_cast<int>((height - 5) / 3.4));
        int columns = std::max(2, static_cast<int>(length / 3.2));
        bool warm = pick(3) == 0;
        for (int f = 0; f < floors; ++f) {
            double y = 6.2 + f * 3.4;
            if (y + 1.2 > height - 0.6) break;
            for (int c = 0; c < columns; ++c) {
                double along = z - length / 2 + (c + 0.5) * length / columns;
                bool lit = warm ? pick(3) != 0 : pick(7) == 0;
                if (lit) glow({face, y, along}, {0.12, 1.9, length / columns * 0.62}, pick(2) ? "FFD68A" : "FFE9C2");
                else {
                    auto& pane = part({face, y, along}, {0.12, 1.9, length / columns * 0.62}, "8FB4CF");
                    pane.material = "glass";
                    pane.reflectance = 0.3;
                }
            }
            part({face - side * 0.05, y - 1.3, z}, {0.1, 0.18, length}, "F2F3F5");
        }
        double shop = side * front - side * 0.08;
        glow({shop, 1.9, z}, {0.1, 2.9, length * 0.84}, pick(2) ? "E8C796" : "D9B98C");
        int mullions = std::max(2, static_cast<int>(length / 2.6));
        for (int m = 0; m <= mullions; ++m) part({shop - side * 0.06, 1.9, z - length * 0.42 + m * length * 0.84 / mullions}, {0.08, 3.0, 0.12}, "2B2E35");
        part({shop - side * 0.07, 1.25, z + length * 0.12}, {0.06, 2.4, 1.4}, "3A2E28");
        part({shop - side * 0.05, 3.45, z}, {0.12, 0.12, length * 0.86}, "2B2E35");
        std::string awning = paints[static_cast<std::size_t>(pick(static_cast<int>(paints.size())))];
        auto& cover = part({side * (front - 0.9), 3.9, z}, {1.9, 0.12, length * 0.8}, awning);
        cover.rotationZ = side * 12;
        if (pick(2) == 0) glow({shop - side * 0.08, 4.7, z + length * 0.2}, {0.15, 0.7, std::min(4.0, length * 0.35)}, pick(2) ? "4FD1C5" : "FF7AA2");
    }

    void street(double side) {
        double z = 18;
        while (z > -230) {
            double length = uniform(9, 17);
            if (z - length < -27 && z > -45) {
                z = -45;
                continue;
            }
            double height = uniform(14, 52) * (z < -120 ? 1.15 : 1.0);
            building(side, 14.5, uniform(14, 22), length, height, z - length / 2);
            z -= length + uniform(0.4, 2.2);
        }
    }

    void crossStreet() {
        for (double side : {-1.0, 1.0})
            for (double x = 16; x < 140; x += uniform(12, 18)) {
                double length = uniform(9, 14), height = uniform(12, 40);
                std::string color = facades[static_cast<std::size_t>(pick(static_cast<int>(facades.size())))];
                for (double s : {-1.0, 1.0}) {
                    double zEdge = -36 + side * 13.5;
                    part({s * (x + length / 2), height / 2, zEdge + side * 9}, {length, height, 18}, color);
                    for (int f = 0; f < static_cast<int>((height - 5) / 3.4); ++f) {
                        Vec3 at{s * (x + length / 2), 6.2 + f * 3.4, zEdge - side * 0.05};
                        if (pick(4)) part(at, {length * 0.8, 1.6, 0.12}, "8FB4CF").material = "glass";
                        else glow(at, {length * 0.8, 1.6, 0.12}, "FFD68A");
                    }
                }
            }
    }

    void skyline() {
        for (int i = 0; i < 40; ++i) {
            double x = uniform(-160, 160), z = uniform(-420, -260), h = uniform(30, 120);
            part({x, h / 2, z}, {uniform(14, 30), h, uniform(14, 30)}, pick(2) ? "9FA9BB" : "B8B3C4");
        }
    }

    void lamp(Vec3 base, double facing) {
        part({base.x, 3.2, base.z}, {0.16, 6.4, 0.16}, "2B2E35", "cylinder");
        part({base.x - facing * 0.8, 6.35, base.z}, {1.7, 0.12, 0.12}, "2B2E35");
        glow({base.x - facing * 1.55, 6.15, base.z}, {0.5, 0.25, 0.5}, "FFE6A8", "sphere");
    }

    void tree(Vec3 base) {
        part({base.x, 0.35, base.z}, {1.2, 0.5, 1.2}, "7A6A5A");
        part({base.x, 1.8, base.z}, {0.28, 3.2, 0.28}, "6B4E36", "cylinder");
        std::array<const char*, 4> leaves{"4B974B", "3E8E4A", "5FA65A", "2F7D46"};
        for (int i = 0; i < 4; ++i) {
            double r = uniform(1.4, 2.0);
            part({base.x + uniform(-0.6, 0.6), 3.9 + uniform(0, 1.2), base.z + uniform(-0.6, 0.6)}, {r * 2, r * 1.8, r * 2}, leaves[static_cast<std::size_t>(i)], "sphere");
        }
    }

    void car(Vec3 centre, double heading, const std::string& color, bool bus = false) {
        double length = bus ? 11 : 4.4, width = bus ? 2.6 : 1.9, body = bus ? 2.6 : 0.9;
        auto rotated = [&](double ax, double az) {
            double c = std::cos(heading * pi / 180), s = std::sin(heading * pi / 180);
            return Vec3{centre.x + ax * c + az * s, 0, centre.z - ax * s + az * c};
        };
        auto at = [&](double ax, double y, double az) {
            Vec3 p = rotated(ax, az);
            p.y = y;
            return p;
        };
        part(at(0, 0.35 + body / 2, 0), {width, body, length}, color).rotationY = heading;
        if (!bus) {
            auto& cabin = part(at(0, 1.25 + 0.38, -0.2), {width * 0.88, 0.76, length * 0.5}, "2F3A4A");
            cabin.rotationY = heading;
            cabin.material = "glass";
        } else
            for (int i = 0; i < 6; ++i) {
                auto& w = part(at(width / 2 + 0.02, 2.1, -length / 2 + 1.4 + i * 1.6), {0.06, 0.9, 1.2}, "9CC9E8");
                w.rotationY = heading;
                w.material = "glass";
                auto& o = part(at(-width / 2 - 0.02, 2.1, -length / 2 + 1.4 + i * 1.6), {0.06, 0.9, 1.2}, "9CC9E8");
                o.rotationY = heading;
                o.material = "glass";
            }
        for (double ax : {-width / 2 + 0.1, width / 2 - 0.1})
            for (double az : {-length / 2 + 0.9, length / 2 - 0.9}) {
                auto& wheel = part(at(ax, 0.36, az), {0.72, 0.28, 0.72}, "1B1D21", "cylinder");
                wheel.rotationZ = 90;
                wheel.rotationY = heading;
            }
        for (double ax : {-width / 2 + 0.3, width / 2 - 0.3}) {
            glow(at(ax, 0.75, -length / 2 - 0.02), {0.36, 0.16, 0.06}, "FFF2C2").rotationY = heading;
            glow(at(ax, 0.8, length / 2 + 0.02), {0.36, 0.14, 0.06}, "FF4A3D").rotationY = heading;
        }
    }

    void props() {
        for (double side : {-1.0, 1.0})
            for (double z = 20; z > -60; z -= 16) {
                if (z < -24 && z > -48) continue;
                lamp({side * 10.6, 0, z}, side);
                if (static_cast<int>(-z) % 32 < 16) tree({side * 12.4, 0, z - 8});
                else {
                    part({side * 13.6, 0.45, z - 8}, {0.6, 0.12, 2.0}, "8D6E4E");
                    part({side * 13.9, 0.75, z - 8}, {0.1, 0.5, 2.0}, "8D6E4E");
                }
            }
        for (double x : {-10.6, 10.6})
            for (double z : {-25.0, -47.0}) {
                part({x, 2.2, z}, {0.14, 4.4, 0.14}, "2B2E35", "cylinder");
                part({x, 4.2, z}, {0.5, 1.3, 0.4}, "1B1D21");
                glow({x, 4.6, z + (z > -36 ? -0.21 : 0.21)}, {0.26, 0.26, 0.06}, "FF4A3D", "sphere");
                glow({x, 4.2, z + (z > -36 ? -0.21 : 0.21)}, {0.26, 0.26, 0.06}, "4A3A20", "sphere");
                glow({x, 3.8, z + (z > -36 ? -0.21 : 0.21)}, {0.26, 0.26, 0.06}, "234A2A", "sphere");
            }
        for (double z = -60; z > -200; z -= 30) {
            part({-13.8, 1.2, z}, {0.12, 2.4, 1.6}, "2E6FF2");
            glow({-13.74, 1.6, z}, {0.06, 1.2, 1.3}, "BFE3FF");
        }
    }

    void traffic() {
        car({7.6, 0, -12}, 180, paints[6]);
        car({-7.6, 0, -2}, 0, paints[1], true);
        car({18, 0, -33.2}, 90, paints[7]);
        car({-24, 0, -38.8}, -90, paints[3]);
        car({34, 0, -38.8}, -90, paints[0]);
    }
};

}

std::unique_ptr<scenery::Scene> build() {
    Builder b;
    b.world = Experience();
    b.world.id = "5a1f0c2e-7d3b-4c1a-8e2f-00000000c17e";
    b.world.lighting.clockTime = 9.3;
    b.world.lighting.brightness = 1.15;
    b.world.lighting.fogEnd = 260;
    b.world.lighting.ambient = "A9A6BE";
    b.world.lighting.skyTop = "4A86D8";
    b.world.lighting.skyHorizon = "F7C9A0";
    b.ground();
    b.street(1);
    b.street(-1);
    b.crossStreet();
    b.skyline();
    b.props();
    b.traffic();
    auto scene = std::make_unique<scenery::Scene>();
    scene->parts = static_cast<int>(b.world.blocks.size());
    scene->world = render::worldScene(b.world);
    scene->world->scene.fog.color = Color::hex(b.world.lighting.skyHorizon).blended(Color{1, 1, 1, 1}, 0.15);
    std::mt19937 random(7);
    auto person = [&](Vec3 at, double yaw, bool walking, double phase) {
        auto body = render::scene_factory::avatar(scenery::look(random, std::uniform_int_distribution<int>(0, 1)(random) == 1));
        if (!body) return;
        body->node->position = at;
        body->node->setEuler({0, yaw * pi / 180, 0});
        body->animate(walking ? "Walk" : "Idle", 0, phase);
        scene->world->scene.root->add(body->node);
        scene->people.push_back(body);
    };
    std::uniform_real_distribution<double> unit(0, 1);
    for (double side : {-1.0, 1.0})
        for (int i = 0; i < 14; ++i) {
            double z = 16 - i * 7.5 - unit(random) * 4;
            if (z < -24 && z > -48) continue;
            double x = side * (11.3 + unit(random) * 2.6);
            bool forward = unit(random) < 0.5;
            person({x, 0.18, z}, forward ? 180 : 0, unit(random) < 0.8, unit(random));
        }
    for (double side : {-1.0, 1.0})
        for (int i = 0; i < 14; ++i) {
            double z = 18 - i * 1.6 - unit(random) * 0.8;
            double x = side * (10.9 + unit(random) * 3.4);
            bool forward = unit(random) < 0.5;
            person({x, 0.18, z}, forward ? 180 + (unit(random) - 0.5) * 20 : (unit(random) - 0.5) * 20, unit(random) < 0.7, unit(random));
        }
    for (int i = 0; i < 4; ++i) {
        person({-11.2 - unit(random) * 2.6, 0.18, 10.0 - i * 1.4}, i % 2 ? 160 : 20, i % 3 != 0, unit(random));
        person({11.2 + unit(random) * 2.6, 0.18, 10.4 - i * 1.4}, i % 2 ? 200 : -20, i % 3 != 1, unit(random));
    }

    for (int g = 0; g < 4; ++g) {
        Vec3 centre{(g % 2 ? 1 : -1) * 12.8, 0.18, 6.0 - g * 22};
        for (int k = 0; k < 3; ++k) {
            double angle = k * 2.1 + g;
            person({centre.x + std::cos(angle) * 0.9, 0.18, centre.z + std::sin(angle) * 0.9}, 90 - angle * 180 / pi, false, unit(random));
        }
    }
    auto camera = render::Node::make("city.camera");
    camera->camera = render::Camera{52, 0.3, 700};
    camera->position = {0.4, 3.1, 22};
    camera->look({0, 5.6, -70});
    scene->world->scene.root->add(camera);
    scene->camera = camera;
    return scene;
}

}
