#include "Harbor.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <tuple>

namespace harbor {

using namespace ld;

namespace {

const Vec3 eye{24, 10, 16};
const double heading = 74;
const Vec3 right{std::cos(heading * pi / 180), 0, -std::sin(heading * pi / 180)};
const Vec3 ahead{-std::sin(heading * pi / 180), 0, -std::cos(heading * pi / 180)};

Vec3 local(double r, double y, double f) { return {eye.x + right.x * r + ahead.x * f, y, eye.z + right.z * r + ahead.z * f}; }

double beach(double r, double f) { return std::sqrt(std::pow(r / 30, 2) + std::pow((f - 58) / 22, 2)); }

double promontory(double r, double f) { return std::min(2.5 - 0.35 * std::max(0.0, f - 4) - r, 12 - f); }

double ground(double r, double f) {
    double e = beach(r, f);
    double h = e < 1 ? std::min(1.6, (1 - e) / 0.25 * 1.6) : std::max(-5.5, -(e - 1) * 14);
    double rock = std::hypot(r - 24, f - 44);
    if (rock < 8) h = std::max(h, 3.6 * (1 - std::pow(rock / 8, 2)) + 0.5 * std::sin(r * 1.7) * std::cos(f * 1.3) - 0.4);
    if (double edge = promontory(r, f); edge > 0) h = std::max(h, edge >= 1.5 ? 7.5 : -1 + 8.5 * edge / 1.5);
    return h;
}

TerrainMaterial surface(double r, double f, double h) {
    if (promontory(r, f) > 0) return h > 6.4 ? TerrainMaterial::grass : TerrainMaterial::rock;
    if (std::hypot(r - 24, f - 44) < 8 && h > 0.2) return TerrainMaterial::rock;
    double e = beach(r, f);
    if (e < 0.82) return TerrainMaterial::grass;
    return e < 1.3 ? TerrainMaterial::sand : TerrainMaterial::mud;
}

struct Builder : scenery::Builder {
    Builder() : scenery::Builder(20261017, "harbor", "Porto") {}

    WorldBlock& at(double r, double y, double f, Vec3 size, const std::string& color, double yaw = 0, const std::string& kind = "cube") {
        auto& b = part(local(r, y, f), size, color, kind);
        b.rotationY = heading + yaw;
        return b;
    }

    WorldBlock& light(double r, double y, double f, Vec3 size, const std::string& color, const std::string& kind = "cube") {
        auto& b = at(r, y, f, size, color, 0, kind);
        b.material = "neon";
        return b;
    }

    double vary(double value, double amount) { return value * (1 + uniform(-amount, amount)); }

    void terrain() {
        Terrain& t = world.terrain;
        for (int i = 0; i < Terrain::cellsX; ++i)
            for (int k = 0; k < Terrain::cellsZ; ++k) {
                Vec3 c = Terrain::center(i, 0, k);
                double dx = c.x - eye.x, dz = c.z - eye.z;
                double r = dx * right.x + dz * right.z, f = dx * ahead.x + dz * ahead.z;
                double h = ground(r, f);
                TerrainMaterial top = surface(r, f, h);
                for (int j = 0; j < Terrain::cellsY; ++j) {
                    double bottom = Terrain::origin.y + j * Terrain::cellSize;
                    int solid = static_cast<int>(std::lround(std::clamp((h - bottom) / Terrain::cellSize, 0.0, 1.0) * Terrain::levels));
                    int wet = static_cast<int>(std::lround(std::clamp((0 - bottom) / Terrain::cellSize, 0.0, 1.0) * Terrain::levels));
                    TerrainMaterial m = bottom + Terrain::cellSize < h - 1.2 && top != TerrainMaterial::sand ? TerrainMaterial::rock : top;
                    if (solid > 0 || wet > 0) t.set(i, j, k, Terrain::compose(solid > 0 ? m : TerrainMaterial::air, solid, wet));
                }
            }
        t.waterProperties.color = "1F8A9E";
        t.waterProperties.transparency = 0.4;
        t.waterProperties.reflectance = 0.7;
        t.waterProperties.waveSize = 0.15;
    }

    void pier() {
        const double r = 10, start = 39, end = 24, deck = 1.2;
        std::array<const char*, 3> tones{"A47E5A", "8E6A49", "B8926B"};
        for (double f = start; f > end + 0.2; f -= 0.42) at(r, deck, f, {vary(2.4, 0.04), 0.15, 0.38}, tones[static_cast<std::size_t>(pick(3))]);
        for (double f = start - 1; f >= end; f -= 2)
            for (double side : {-1.15, 1.15}) at(r + side, (deck - 3) / 2, f, {0.26, deck + 3, 0.26}, "6B4E36", 0, "cylinder");
        for (double x = -4; x <= 4.01; x += 0.42) at(r + x, deck, end - 1.5, {0.38, 0.15, vary(3, 0.03)}, tones[static_cast<std::size_t>(pick(3))]);
        for (double x = -4; x <= 4.01; x += 2)
            for (double side : {-1.4, 1.4}) at(r + x, (deck - 3) / 2, end - 1.5 + side, {0.26, deck + 3, 0.26}, "6B4E36", 0, "cylinder");
        at(r + 2.6, deck + 0.55, end - 2.3, {0.9, 0.9, 0.9}, "A0784F");
        at(r + 2.6, deck + 1.45, end - 2.3, {0.9, 0.9, 0.9}, "B08A5E", 7);
        at(r + 3.6, deck + 0.55, end - 1.9, {0.9, 0.9, 0.9}, "9A7048", -7);
        for (double x : {-3.4, -2.8}) at(r + x, deck + 0.55, end - 2.5, {0.7, 0.95, 0.7}, "7A5134", 0, "cylinder");
        at(r - 3.2, deck + 0.55, end - 1.7, {0.7, 0.95, 0.7}, "355E8C", 0, "cylinder");
        at(r - 1.4, deck + 2.1, end - 2.85, {0.12, 3.6, 0.12}, "2B2E35", 0, "cylinder");
        at(r - 1.4, deck + 3.95, end - 2.85, {0.5, 0.12, 0.5}, "2B2E35");
        light(r - 1.4, deck + 3.65, end - 2.85, {0.4, 0.5, 0.4}, "FFD68A");
        auto& rod = at(r + 1.8, deck + 1.4, end - 3.05, {0.05, 3.2, 0.05}, "3A2E28", 0, "cylinder");
        rod.rotationZ = 38;
    }

    void boats() {
        const double r = 13.4, f = 28.5;
        at(r, 0.35, f, {1.4, 0.5, 3.5}, "C8553D");
        at(r, 0.62, f, {1.2, 0.06, 3.2}, "8E6A49");
        at(r, 0.68, f + 0.4, {1.2, 0.12, 0.35}, "A47E5A");
        at(r - 0.4, 0.9, f - 0.4, {0.06, 0.06, 2.2}, "D9CDB4", 30);
        Vec3 bow = local(r - 0.2, 0.62, f - 1.7), post = local(11.15, 1.4, 31);
        Vec3 middle = (bow + post) * 0.5, span = post - bow;
        auto& rope = part(middle, {0.05, 0.05, span.length()}, "D9CDB4");
        rope.rotationY = std::atan2(span.x, span.z) * 180 / pi;
        rope.rotationX = -std::asin(span.y / span.length()) * 180 / pi;
        const double sr = 46, sf = 150;
        at(sr, 0.5, sf, {1.8, 1.0, 6.5}, "F4F1EA");
        at(sr, 4.6, sf, {0.16, 7.5, 0.16}, "6B4E36", 0, "cylinder");
        at(sr, 4.6, sf + 1.2, {0.05, 5.6, 2.2}, "F4F1EA", 0, "wedge");
        at(sr, 3.8, sf - 1.2, {0.05, 4.0, 2.0}, "D8473B", 180, "wedge");
    }

    void house(double r, double f, double width, double wall, const std::string& color, bool chimney) {
        double base = ground(r, f) - 0.2, depth = vary(5.5, 0.06);
        at(r, base + wall / 2, f, {width, wall, depth}, color);
        double rise = 2.2;
        at(r, base + wall + rise / 2, f - depth / 4 - 0.15, {width + 0.5, rise, depth / 2 + 0.3}, "9C3B2A", 180, "wedge");
        at(r, base + wall + rise / 2, f + depth / 4 + 0.15, {width + 0.5, rise, depth / 2 + 0.3}, "9C3B2A", 0, "wedge");
        double front = f - depth / 2 - 0.06;
        at(r - width / 4, base + 1.05, front, {1.1, 2.1, 0.12}, "5A3A28");
        for (double x : {width / 5, width / 2.6}) {
            double cx = r + x - width / 8;
            at(cx, base + wall * 0.62, front, {1.0, 1.0, 0.1}, "2B2E35");
            auto& pane = at(cx, base + wall * 0.62, front - 0.04, {0.78, 0.78, 0.08}, "8FB4CF");
            pane.material = "glass";
            at(cx, base + wall * 0.62 - 0.62, front - 0.18, {1.0, 0.24, 0.3}, "7A5134");
            for (double fx : {-0.3, 0.0, 0.3}) at(cx + fx, base + wall * 0.62 - 0.42, front - 0.18, {0.22, 0.22, 0.22}, pick(2) ? "E0392B" : "F2C230", 0, "sphere");
        }
        if (chimney) at(r + width / 3, base + wall + rise + 0.2, f + depth / 4, {0.6, 1.6, 0.6}, "7A6A5A");
        for (double x : {-width / 3, 0.0, width / 3}) {
            at(r + x, base + wall * 0.62, f + depth / 2 + 0.06, {0.9, 0.9, 0.08}, "2B2E35");
            for (double side : {-0.6, 0.6}) at(r + x + side, base + wall * 0.62, f + depth / 2 + 0.1, {0.3, 0.95, 0.06}, "2F5F8A");
        }
        for (double x = -width / 2 + 0.6; x < width / 2 - 0.4; x += 0.7) {
            at(r + x, base + 0.3, front - 1.1, {0.08, 0.6, 0.08}, "F4F1EA");
            at(r + x, base + 0.1, front - 0.8, {0.18, 0.18, 0.18}, pick(3) ? "E0392B" : "F2C230", 0, "sphere");
        }
        at(r, base + 0.55, front - 1.1, {width - 0.8, 0.08, 0.06}, "F4F1EA");
    }

    void details() {
        for (int i = 0; i < 140; ++i) {
            double a = uniform(0, 2 * pi), d = uniform(5.5, 9), r = 24 + std::cos(a) * d, f = 44 + std::sin(a) * d * 0.9, size = uniform(0.5, 1.6);
            double h = ground(r, f);
            if (h < -1.2) continue;
            auto& stone = at(r, h + size * 0.2, f, {size, size * 0.7, size * uniform(0.8, 1.2)}, pick(2) ? "7F8087" : "6E7078", uniform(0, 90));
            stone.rotationZ = uniform(-6, 6);
        }
        for (int i = 0; i < 180; ++i) {
            double t = i / 180.0, r = 10 + (4 - 10) * t + uniform(-0.7, 0.7), f = 39.5 + (46 - 39.5) * t + uniform(-0.7, 0.7);
            at(r, ground(r, f) + 0.03, f, {uniform(0.35, 0.5), 0.06, uniform(0.35, 0.5)}, pick(3) ? "BDB6A8" : "A39C8E", uniform(0, 45));
        }
        for (double r = -20; r <= 18; r += 1.1) {
            double f = 58 - 22 * std::sqrt(std::max(0.0, 1 - std::pow(r / 30, 2))) + 3.2;
            at(r, ground(r, f) - 0.2, f, {1.05, 0.6, 0.5}, pick(2) ? "9A958C" : "8C877E");
        }
        for (int i = 0; i < 60; ++i) {
            double r = uniform(-20, 0), f = 58 - 22 * std::sqrt(std::max(0.0, 1 - std::pow(r / 30, 2))) + uniform(0.6, 2.4);
            at(r, ground(r, f) + 0.06, f, {uniform(0.15, 0.3), 0.12, uniform(0.15, 0.3)}, "9A958C", uniform(0, 90), "sphere");
        }
        for (double f = 38.5; f > 24.5; f -= 1)
            for (double side : {-1.15, 1.15}) at(10 + side, 1.27 + 0.5, f, {0.08, 1.0, 0.08}, "6B4E36");
        for (double side : {-1.15, 1.15}) at(10 + side, 1.27 + 1.0, 31.5, {0.06, 0.06, 14}, "6B4E36");
        for (double r = -16; r <= 16; r += 8) {
            double f = 58 - 22 * std::sqrt(std::max(0.0, 1 - std::pow(r / 30, 2))) + 4.6, b = ground(r, f);
            at(r, b + 1.6, f, {0.12, 3.2, 0.12}, "2B2E35", 0, "cylinder");
            light(r, b + 3.3, f, {0.36, 0.42, 0.36}, "FFD68A");
        }
        for (auto [r, f, yaw] : std::array<std::tuple<double, double, double>, 2>{{{-17.5, 37.8, 20}, {-14.5, 38.6, -15}}}) {
            double b = ground(r, f);
            at(r, b + 0.3, f, {1.3, 0.5, 3.4}, yaw > 0 ? "3A6EA5" : "F2C14E", yaw);
            at(r, b + 0.56, f, {1.1, 0.06, 3.1}, "8E6A49", yaw);
            at(r, b + 0.62, f + 0.5, {1.1, 0.1, 0.3}, "A47E5A", yaw);
            at(r, b + 0.62, f - 0.7, {1.1, 0.1, 0.3}, "A47E5A", yaw);
        }
    }

    void village() {
        std::array<const char*, 5> colors{"C8553D", "F2E3C6", "F2C14E", "3A6EA5", "F4F1EA"};
        std::array<double, 5> places{-15, -7, 1, 9, 17};
        for (std::size_t i = 0; i < places.size(); ++i) house(places[i], 52 + (i % 2) * 1.6, uniform(5, 8), uniform(3, 4), colors[i], i == 1 || i == 3);
        double stallR = 3.6, stallF = 42.5, base = ground(stallR, stallF);
        at(stallR, base + 0.5, stallF, {2.6, 1.0, 1.2}, "8E6A49");
        for (double x : {-1.2, 1.2})
            for (double z : {-0.5, 0.5}) at(stallR + x, base + 1.2, stallF + z, {0.1, 2.4, 0.1}, "6B4E36", 0, "cylinder");
        for (int s = 0; s < 6; ++s) at(stallR - 1.25 + s * 0.5, base + 2.45, stallF, {0.5, 0.1, 1.5}, s % 2 ? "F4F1EA" : "D8473B");
        at(stallR - 0.5, base + 1.1, stallF - 0.3, {0.3, 0.3, 0.3}, "F2C230", 0, "sphere");
        at(stallR + 0.4, base + 1.1, stallF - 0.3, {0.3, 0.3, 0.3}, "E0392B", 0, "sphere");
        double benchR = -2, benchF = 45, benchBase = ground(benchR, benchF);
        at(benchR, benchBase + 0.45, benchF, {1.8, 0.1, 0.5}, "8D6E4E");
        at(benchR, benchBase + 0.75, benchF + 0.25, {1.8, 0.5, 0.08}, "8D6E4E");
        for (auto [lr, lf] : std::array<std::pair<double, double>, 2>{{std::pair{-0.4, 43.0}, std::pair{6.8, 42.6}}}) {
            double lb = ground(lr, lf);
            at(lr, lb + 1.8, lf, {0.12, 3.6, 0.12}, "2B2E35", 0, "cylinder");
            light(lr, lb + 3.7, lf, {0.42, 0.5, 0.42}, "FFD68A");
        }
    }

    void lighthouse() {
        double r = 24, f = 44, base = ground(r, f) - 0.3;
        for (int band = 0; band < 5; ++band) {
            double width = 2.6 - band * 0.18;
            at(r, base + band * 1.8 + 0.9, f, {width, 1.8, width}, band % 2 ? "D8473B" : "F4F1EA", 0, "cylinder");
        }
        double top = base + 9;
        at(r, top + 0.1, f, {2.6, 0.2, 2.6}, "2B2E35", 0, "cylinder");
        for (int i = 0; i < 12; ++i) {
            double a = i * pi / 6;
            at(r + std::cos(a) * 1.2, top + 0.6, f + std::sin(a) * 1.2, {0.06, 0.8, 0.06}, "2B2E35", 0, "cylinder");
        }
        auto& glass = at(r, top + 1.2, f, {1.3, 1.6, 1.3}, "BFE3FF", 0, "cylinder");
        glass.material = "glass";
        light(r, top + 1.2, f, {0.8, 0.9, 0.8}, "FFE6A8", "sphere");
        at(r, top + 2.3, f, {1.6, 0.6, 1.6}, "D8473B", 0, "cylinder");
        at(r, top + 2.9, f, {0.4, 0.6, 0.4}, "2B2E35", 0, "sphere");
    }

    void tree(double r, double f, double scale) {
        double base = ground(r, f);
        at(r, base + 1.4 * scale, f, {0.32 * scale, 2.8 * scale, 0.32 * scale}, "6B4E36", 0, "cylinder");
        std::array<const char*, 3> leaves{"4B974B", "3E8E4A", "5FA65A"};
        int crowns = 2 + pick(2);
        for (int i = 0; i < crowns; ++i) {
            double size = uniform(1.6, 2.2) * scale;
            at(r + uniform(-0.5, 0.5) * scale, base + (3.0 + i * 0.9) * scale, f + uniform(-0.5, 0.5) * scale, {size, size * 0.9, size}, leaves[static_cast<std::size_t>(i)], 0, "sphere");
        }
    }

    void nature() {
        for (auto [r, f] : std::array<std::pair<double, double>, 7>{{{-19, 57}, {-11, 58}, {-3, 56.5}, {5, 57.5}, {13, 56}, {-6, 47}, {8, 46}}}) tree(r, f, uniform(0.8, 1.2));
        for (auto [r, f] : std::array<std::pair<double, double>, 6>{{{-17, 46}, {-20, 50}, {-14, 47}, {-9, 41.5}, {15.5, 47}, {-12.5, 48}}}) {
            double s = uniform(0.8, 1.3);
            at(r, ground(r, f) + 0.35 * s, f, {1.2 * s, 0.9 * s, 1.2 * s}, pick(2) ? "3E8E4A" : "4B974B", 0, "sphere");
        }
        double ur = -4, uf = 38.6, ub = ground(ur, uf);
        auto& pole = at(ur, ub + 1.1, uf, {0.07, 2.2, 0.07}, "F4F1EA", 0, "cylinder");
        pole.rotationZ = 6;
        at(ur + 0.1, ub + 2.2, uf, {2.4, 0.35, 2.4}, "D8473B", 0, "sphere");
        at(ur + 0.9, ub + 0.04, uf - 0.6, {1.0, 0.04, 1.9}, "3A6EA5", 12);
        at(-1.8, ground(-1.8, 37.8) + 0.2, 37.8, {0.7, 0.4, 0.7}, "D8C68E", 0, "cylinder");
        at(-1.8, ground(-1.8, 37.8) + 0.55, 37.8, {0.3, 0.4, 0.3}, "D8C68E", 0, "cylinder");
        for (auto [r, f] : std::array<std::pair<double, double>, 5>{{{3, 22}, {7, 18}, {15, 21}, {19, 31}, {1, 31}}}) {
            at(r, 0.2, f, {0.6, 0.7, 0.6}, "D8473B", 0, "sphere");
            at(r, 0.65, f, {0.3, 0.3, 0.3}, "F4F1EA", 0, "sphere");
        }
    }
};

}

std::unique_ptr<scenery::Scene> build() {
    Builder b;
    b.world = Experience();
    b.world.id = "5a1f0c2e-7d3b-4c1a-8e2f-0000000a0b70";
    b.world.lighting.clockTime = 17.8;
    b.world.lighting.brightness = 1.0;
    b.world.lighting.fogEnd = 600;
    b.world.lighting.ambient = "8E8AA8";
    b.world.lighting.skyTop = "3F6FC6";
    b.world.lighting.skyHorizon = "F4B97A";
    b.terrain();
    b.pier();
    b.boats();
    b.village();
    b.lighthouse();
    b.nature();
    b.details();
    auto scene = std::make_unique<scenery::Scene>();
    scene->parts = static_cast<int>(b.world.blocks.size());
    scene->world = render::worldScene(b.world);
    auto& stage = scene->world->scene;
    stage.fog.start = 120;
    stage.fog.color = Color::hex(b.world.lighting.skyHorizon);
    stage.background.top = Color::hex(b.world.lighting.skyTop);
    stage.background.bottom = Color::hex(b.world.lighting.skyHorizon);
    if (auto sun = stage.root->child("ludissey.sun")) {
        sun->light->intensity = 640;
        sun->light->color = Color::hex("FFD2A0");
    }
    if (auto ambient = stage.root->child("ludissey.ambient")) ambient->light->intensity = 560;
    stage.time = 7.3;
    std::mt19937 random(17);
    auto person = [&](double r, double f, double y, double facing, const char* motion, double phase, bool female) {
        auto config = scenery::look(random, female);
        auto body = render::scene_factory::avatar(config);
        if (!body) return;
        double scale = 1 + std::uniform_real_distribution<double>(-0.08, 0.08)(random);
        body->node->position = local(r, y, f);
        body->node->scale = {scale, scale, scale};
        Vec3 towards = right * std::sin(facing * pi / 180) - ahead * std::cos(facing * pi / 180);
        body->node->setEuler({0, std::atan2(towards.x, towards.z), 0});
        body->animate(motion, 0, phase);
        scene->world->scene.root->add(body->node);
        scene->people.push_back(body);
    };
    auto standing = [](double r, double f) { return ground(r, f) + 0.18; };
    const double deck = 1.38;
    person(-3.2, 7.0, standing(-3.2, 7.0), -38, "Idle", 0.2, true);
    person(-0.8, 8.4, standing(-0.8, 8.4), 34, "Idle", 0.65, false);
    person(9.2, 23.4, deck + 0.9, 150, "Jump", 0.45, false);
    person(11.8, 22.9, deck, -95, "Idle", 0.3, true);
    person(10.0, 31.0, deck, 172, "Walk", 0.55, true);
    person(-9.0, 37.5, standing(-9.0, 37.5), 70, "Walk", 0.1, false);
    person(-12.5, 39.0, standing(-12.5, 39.0), -110, "Walk", 0.8, true);
    person(-5.6, 37.2, standing(-5.6, 37.2), 25, "Idle", 0.4, false);
    person(-15.0, 33.0, standing(-15.0, 33.0), -20, "Idle", 0.75, true);
    person(2.6, 41.4, standing(2.6, 41.4), 85, "Idle", 0.15, true);
    person(4.6, 41.4, standing(4.6, 41.4), -85, "Idle", 0.9, false);
    auto camera = render::Node::make("harbor.camera");
    camera->camera = render::Camera{50, 0.1, 1200};
    camera->position = eye;
    Vec3 forward = ahead * std::cos(7.5 * pi / 180) - Vec3{0, std::sin(7.5 * pi / 180), 0};
    camera->look(eye + forward * 10);
    scene->world->scene.root->add(camera);
    scene->camera = camera;
    return scene;
}

}
