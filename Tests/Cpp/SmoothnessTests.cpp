#include "Engine/Rendering/PlayerScene.hpp"
#include "Golden.hpp"
#include "Tests/Support/Timing.hpp"

#include <algorithm>
#include <cmath>
#include <random>

using namespace ld;

namespace {

constexpr double shift = 22;

Terrain field(const std::function<double(double, double)>& height, std::optional<double> water = std::nullopt) {
    Terrain t;
    for (int i = 0; i < Terrain::cellsX; ++i)
        for (int k = 0; k < Terrain::cellsZ; ++k) {
            Vec3 c = Terrain::center(i, 0, k);
            double top = height(c.x, c.z + shift);
            for (int j = 0; j < Terrain::cellsY; ++j) {
                double bottom = Terrain::origin.y + j * Terrain::cellSize;
                double fill = std::clamp((top - bottom) / Terrain::cellSize, 0.0, 1.0);
                int level = static_cast<int>(std::round(fill * Terrain::levels));
                if (level > 0) t.set(i, j, k, Terrain::compose(TerrainMaterial::grass, level, 0));
                else if (water && bottom < *water) {
                    int depth = static_cast<int>(std::round(std::clamp((*water - bottom) / Terrain::cellSize, 0.0, 1.0) * Terrain::levels));
                    if (depth > 0) t.set(i, j, k, Terrain::compose(TerrainMaterial::air, 0, depth));
                }
            }
        }
    return t;
}

struct Scenario {
    std::string name;
    Experience world;
    std::optional<double> jumpEvery;
};

Experience terrainWorld(const std::function<double(double, double)>& height, std::optional<double> water = std::nullopt) {
    Experience w = Experience::blank();
    w.blocks.clear();
    w.terrain = field(height, water);
    return w;
}

std::vector<Scenario> scenarios() {
    auto moved = [](Experience w, double dz) {
        for (auto& b : w.blocks) b.z += dz;
        return w;
    };
    Experience flat = Experience::blank();
    flat.blocks[0].sizeZ = 120;
    Experience stairs = Experience::blank();
    stairs.blocks[0].sizeZ = 120;
    for (int i = 0; i < 12; ++i) {
        WorldBlock step;
        step.id = makeUuid();
        step.name = "Gradino" + std::to_string(i);
        step.sizeX = 8; step.sizeZ = 1.5; step.sizeY = 0.3 * (i + 1);
        step.y = step.sizeY / 2;
        step.z = 12 - i * 1.5;
        stairs.blocks.push_back(step);
    }
    Experience fall = Experience::blank();
    fall.blocks[0].sizeZ = 120;
    WorldBlock ledge;
    ledge.id = makeUuid();
    ledge.name = "Sporgenza";
    ledge.sizeX = 10; ledge.sizeZ = 16; ledge.sizeY = 6; ledge.y = 3; ledge.z = 20;
    fall.blocks.push_back(ledge);
    double t15 = std::tan(15 * pi / 180), t40 = std::tan(40 * pi / 180);
    return {
        {"piano", moved(flat, 5 - 20), std::nullopt},
        {"pendenza dolce", terrainWorld([=](double, double z) { return 1 + std::clamp(27 - z, 0.0, 40.0) * t15; }), std::nullopt},
        {"pendenza ripida", terrainWorld([=](double, double z) { return 1 + std::clamp(27 - z, 0.0, 12.0) * t40; }), std::nullopt},
        {"scalini", moved(stairs, 5 - 20), std::nullopt},
        {"rumore voxel", terrainWorld([](double x, double z) { return 2 + 0.35 * std::sin(x * 1.7) * std::cos(z * 1.3) + 0.25 * std::sin(z * 2.9); }), std::nullopt},
        {"giunzioni tra chunk", terrainWorld([](double x, double z) { return 2 + 0.2 * x / 16 + 0.3 * std::sin(z / 5); }), std::nullopt},
        {"cresta e valle", terrainWorld([](double, double z) { return 3 + 3 * std::sin(z / 6); }), std::nullopt},
        {"salti", terrainWorld([](double, double) { return 2.0; }), 1.0},
        {"caduta", moved(fall, 5 - 26), std::nullopt},
        {"ingresso e uscita dall’acqua", terrainWorld([](double, double z) { return 1 - 3.2 * std::exp(-(z / 7) * (z / 7)); }, 0.2), std::nullopt},
    };
}

struct Sample {
    double time;
    Vec3 body, eye;
};

struct Report {
    std::string name;
    double speedVariation = 0, verticalJitter = 0, cameraVariation = 0, cameraVerticalJitter = 0;
    int frozen = 0, doubled = 0, reversals = 0, frames = 0;
    std::vector<double> ticks;

    Report(std::string label, const std::vector<Sample>& samples, double vsync) : name(std::move(label)), frames(static_cast<int>(samples.size())) {
        std::vector<double> spans, steps, eyes;
        auto planar = [](Vec3 a, Vec3 b) { return std::hypot(b.x - a.x, b.z - a.z); };
        for (std::size_t i = 1; i < samples.size(); ++i) {
            double span = std::max(1.0, std::round((samples[i].time - samples[i - 1].time) / vsync));
            spans.push_back(span);
            steps.push_back(planar(samples[i - 1].body, samples[i].body) / span);
            eyes.push_back(planar(samples[i - 1].eye, samples[i].eye) / span);
        }
        auto variation = [](const std::vector<double>& v, double* meanOut = nullptr) {
            double mean = 0;
            for (double x : v) mean += x;
            mean /= std::max<std::size_t>(1, v.size());
            double dev = 0;
            for (double x : v) dev += (x - mean) * (x - mean);
            dev = std::sqrt(dev / std::max<std::size_t>(1, v.size()));
            if (meanOut) *meanOut = mean;
            return mean > 1e-6 ? dev / mean : 0.0;
        };
        double mean = 0;
        speedVariation = variation(steps, &mean);
        for (double s : steps) {
            if (mean > 1e-6 && s < mean * 0.25) ++frozen;
            if (mean > 1e-6 && s > mean * 1.75) ++doubled;
        }
        cameraVariation = variation(eyes);
        auto bounce = [&](const std::function<double(const Sample&)>& value) {
            std::vector<double> rates;
            for (std::size_t i = 1; i < samples.size(); ++i) rates.push_back((value(samples[i]) - value(samples[i - 1])) / spans[i - 1]);
            double worst = 0;
            for (std::size_t i = 1; i < rates.size(); ++i) worst = std::max(worst, std::abs(rates[i] - rates[i - 1]));
            return worst;
        };
        verticalJitter = bounce([](const Sample& s) { return s.body.y; });
        cameraVerticalJitter = bounce([](const Sample& s) { return s.eye.y; });
        for (std::size_t i = 2; i < samples.size(); ++i) {
            double before = samples[i - 1].body.y - samples[i - 2].body.y, after = samples[i].body.y - samples[i - 1].body.y;
            if (std::abs(before) > 1e-4 && std::abs(after) > 1e-4 && (before > 0) != (after > 0)) ++reversals;
        }
    }
};

double percentile(std::vector<double> values, double fraction) {
    if (values.empty()) return 0;
    std::sort(values.begin(), values.end());
    return values[std::min(values.size() - 1, static_cast<std::size_t>(fraction * values.size()))];
}

const render::Node* avatarNode(const render::PlayerScene& scene) {
    for (const auto& child : scene.scene().root->children())
        if (child->name.rfind("remote.", 0) != 0 && child->child(render::name_tag::name)) return child.get();
    return nullptr;
}

Report measure(const Scenario& scenario, double hertz, double seconds, std::mt19937& random) {
    auto gameplay = std::make_shared<PlayerGameplay>();
    render::PlayerScene scene(scenario.world, AvatarConfiguration::standard("Martina", "male"), gameplay);
    const render::Node* body = avatarNode(scene);
    REQUIRE(body);
    double refresh = 1 / hertz, now = 0, last = 0;
    FramePacer pacer;
    std::uniform_real_distribution<double> jitter(-0.002, 0.002);
    std::vector<Sample> samples;
    std::vector<double> ticks;
    scene.key(13, true);
    int total = static_cast<int>((seconds + 1) * hertz);
    double nextJump = 1;
    for (int frame = 1; frame <= total; ++frame) {
        now = frame * refresh + jitter(random);
        double delta = pacer.next(now - last, refresh);
        last = now;
        if (scenario.jumpEvery && now >= nextJump) {
            scene.key(49, true);
            scene.key(49, false);
            nextJump += *scenario.jumpEvery;
        }
        auto started = std::chrono::steady_clock::now();
        scene.tick(now, delta, 1000 + now);
        ticks.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
        if (now >= 1) samples.push_back({std::round(frame) * refresh, body->position, scene.camera().position});
    }
    scene.key(13, false);
    Report report(scenario.name + " a " + std::to_string(static_cast<int>(hertz)) + " Hz", samples, refresh);
    report.ticks = ticks;
    return report;
}

}

TEST_CASE("Player: fluidità a ogni vsync", "[player][smoothness][performance]") {
    golden::registry();
    std::mt19937 random(2026);
    const std::vector<std::string> continuous = {"piano", "pendenza dolce", "pendenza ripida", "rumore voxel", "giunzioni tra chunk", "salti"};
    const std::vector<std::string> steady = {"piano", "pendenza dolce", "pendenza ripida", "giunzioni tra chunk"};
    std::vector<double> allTicks;
    for (const auto& scenario : scenarios())
        for (double hertz : {30.0, 60.0, 120.0}) {
            Report r = measure(scenario, hertz, 6, random);
            allTicks.insert(allTicks.end(), r.ticks.begin(), r.ticks.end());
            INFO(r.name << ": variazione velocità " << r.speedVariation << ", congelati " << r.frozen << ", doppi " << r.doubled << ", sobbalzo " << r.verticalJitter << ", inversioni "
                        << r.reversals << ", variazione camera " << r.cameraVariation << ", sobbalzo camera " << r.cameraVerticalJitter << ", tick p99 " << percentile(r.ticks, 0.99) << " ms");
            CHECK(r.frames >= static_cast<int>(6 * hertz) - 2);
            CHECK(r.frozen == 0);
            if (scenario.name != "ingresso e uscita dall’acqua") {
                CHECK(r.doubled == 0);
                CHECK(r.speedVariation < 0.06);
            }
            if (std::find(continuous.begin(), continuous.end(), scenario.name) != continuous.end()) CHECK(r.cameraVariation < 0.06);
            if (std::find(steady.begin(), steady.end(), scenario.name) != steady.end()) {
                CHECK(r.reversals == 0);
                double scale = (60 / hertz) * (60 / hertz);
                CHECK(r.verticalJitter < (scenario.name == "pendenza ripida" ? 0.05 : 0.02) * scale + 1e-9);
                CHECK(r.cameraVerticalJitter < 0.01 * scale + 1e-9);
            }
        }
    double p50 = percentile(allTicks, 0.5), p95 = percentile(allTicks, 0.95), p99 = percentile(allTicks, 0.99);
    WARN("tick del Player (simulazione, script, camera, sincronizzazione scena): p50 " << p50 << " ms, p95 " << p95 << " ms, p99 " << p99 << " ms");
    TIMED_CHECK(p99 < 1000.0 / 120);
}
