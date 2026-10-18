#include "Engine/Audio/Audio.hpp"
#include "Engine/Backend/Assets.hpp"
#include "Rig.hpp"
#include "Studio/StudioStage.hpp"

#include <cmath>
#include <map>
#include <utility>

namespace {

class Speakers final : public ld::audio::Output {
public:
    std::map<std::string, ld::audio::Playback> active;
    int started = 0;
    bool play(const std::string& id, const ld::audio::Playback& playback) override {
        active[id] = playback;
        ++started;
        return true;
    }
    void update(const std::string& id, const ld::audio::Playback& playback) override { active[id] = playback; }
    bool playing(const std::string& id) override { return active.count(id) > 0; }
    void stop(const std::string& id) override { active.erase(id); }
};

struct Bench {
    ld::StudioDocument document;
    studio::StudioStage stage;
    Bench() : stage(document) { stage.size = {900, 700}; }
};

void scripts(ld::StudioDocument& document, int count) {
    static const char* sources[] = {
        "var n = 0\non Time.heartbeat(delta) {\n    n += 1\n}\n",
        "for i in 1...5 {\n    await Task.nextFrame()\n}\nprint(\"fatto\")\n",
        "let p = Part(World, \"Creato\")\np.anchored = true\nawait Task.wait(0.05)\np.destroy()\n",
        "on World.childAdded(child) {\n    let name = child.name\n}\n",
        "let lista = [1, 2, 3]\nprint(lista[7])\n",
    };
    for (int i = 0; i < count; ++i) {
        auto id = document.insert("Script", std::string("ServerScriptService"));
        REQUIRE(id);
        document.setSource(*id, sources[i % 5]);
    }
}

void populate(ld::StudioDocument& document, int parts) {
    for (int i = 0; i < parts; ++i) {
        document.addPart(i % 3 == 0 ? "cube" : i % 3 == 1 ? "sphere" : "cylinder");
        std::string id = document.world.blocks.back().id;
        document.update(id, [i](ld::WorldBlock& b) {
            b.x = (i % 10) * 3 - 15;
            b.z = (i / 10) * 3 - 12;
            b.y = 1 + (i % 3);
            b.anchored = i % 7 != 0;
        });
    }
}

void drive(Bench& bench, int frames, double& clock) {
    static const int codes[4] = {13, 2, 1, 0};
    for (int frame = 0; frame < frames; ++frame) {
        clock += 1.0 / 60;
        ui::KeyInput key;
        key.keyCode = codes[(frame / 4) % 4];
        key.down = frame % 4 != 3;
        bench.stage.key(key);
        bench.document.tick(clock);
        bench.stage.tick(clock, 1.0 / 60);
    }
}

}

TEST_CASE("Stress: Gioca e Ferma ripetuti", "[stress][playstop]") {
    stress::prepare();
    stress::Watchdog watchdog("Play/Stop", 30);
    Bench bench;
    REQUIRE(bench.document.open(stress::project()));
    populate(bench.document, 40);
    scripts(bench.document, 15);
    bench.document.perform(ld::StudioEdit::generate({7, 3, 0, 1}));
    auto folder = std::filesystem::temp_directory_path() / ("Ludissey Stress Audio " + ld::makeUuid());
    ld::asset_cache::setFolder(folder);
    std::string data = "RIFF audio di prova";
    std::string asset = ld::asset_cache::identifier(data, "wav");
    ld::asset_cache::store(data, asset);
    auto speakers = std::make_shared<Speakers>();
    ld::audio::setOutput(speakers);
    ld::InstanceRecord music;
    music.id = ld::makeUuid();
    music.className = "Sound";
    music.name = "Musica";
    music.parent = "SoundService";
    music.properties["SoundId"] = ld::PropertyValue::content(ld::asset_cache::reference(asset));
    music.properties["Volume"] = ld::PropertyValue::number(0.5);
    music.properties["Looped"] = ld::PropertyValue::boolean(true);
    music.properties["Playing"] = ld::PropertyValue::boolean(true);
    bench.document.world.instances.push_back(music);
    ld::Experience before = bench.document.world;
    int cycles = stress::scaled(60, 400, 2000);
    stress::Distribution durations;
    double clock = 0, baseline = 0;
    int threads = 0;
    std::map<int, double> memory;
    int cameraMoved = 0, threadGrowth = 0, cameraChecks = 0;
    for (int cycle = 0; cycle < cycles; ++cycle) {
        double started = stress::now();
        bench.stage.tick(clock, 1.0 / 60);
        ld::Vec3 editEye = bench.stage.camera()->position;
        bench.document.playMode = cycle % 3 == 2 ? ld::StudioPlayMode::camera : ld::StudioPlayMode::character;
        bench.document.play();
        REQUIRE(bench.document.playing);
        drive(bench, 12, clock);
        bench.document.stop();
        REQUIRE_FALSE(bench.document.playing);
        REQUIRE(bench.document.session() == nullptr);
        REQUIRE(speakers->active.empty());
        bench.stage.tick(clock, 1.0 / 60);
        ld::Vec3 after = bench.stage.camera()->position;
        ++cameraChecks;
        if (std::abs(after.x - editEye.x) + std::abs(after.y - editEye.y) + std::abs(after.z - editEye.z) > 1e-6) ++cameraMoved;
        if (threads && stress::threads() > threads + 1) ++threadGrowth;
        durations.add(stress::now() - started);
        watchdog.beat();
        if (cycle == std::min(20, cycles / 3)) {
            baseline = stress::megabytes();
            threads = stress::threads();
        }
        if (cycle % std::max(1, cycles / 8) == 0) memory[cycle] = stress::megabytes();
        if (cycle % 25 == 0) REQUIRE(bench.document.world == before);
    }
    double growth = stress::megabytes() - baseline;
    ld::Json samples = ld::Json::object();
    for (const auto& [cycle, value] : memory) samples.set(std::to_string(cycle), value);
    stress::record("play stop", "cicli", cycles);
    stress::record("play stop", "durata del ciclo", durations);
    stress::record("play stop", "memoria", ld::Json::object({{"riferimento", baseline}, {"finale", stress::megabytes()}, {"crescita", growth}, {"campioni", samples}}));
    stress::record("play stop", "thread", ld::Json::object({{"riferimento", threads}, {"finale", stress::threads()}}));
    INFO(cycles << " cicli, crescita " << growth << " MB, ciclo p95 " << durations.percentile(0.95) * 1000 << " ms");
    stress::record("play stop", "controlli per ciclo", ld::Json::object({{"camera di modifica non ripristinata", cameraMoved}, {"cicli con thread in più", threadGrowth}, {"controlli della camera", cameraChecks}}));
    CHECK(bench.document.world == before);
    CHECK(speakers->started >= cycles);
    CHECK(cameraMoved == 0);
    CHECK(threadGrowth == 0);
    ld::audio::setOutput(nullptr);
    ld::asset_cache::setFolder(std::nullopt);
    std::filesystem::remove_all(folder);
    UNINSTRUMENTED_CHECK(growth < 24);
    UNINSTRUMENTED_CHECK(growth / std::max(1, cycles - 20) < 0.05);
    CHECK(stress::threads() <= threads + 1);
    TIMED_CHECK(durations.percentile(0.95) < durations.percentile(0.5) * 4 + 0.05);
    TIMED_CHECK(!stress::regressed("play stop, ciclo p50", durations.percentile(0.5)));
}

TEST_CASE("Stress: uso prolungato di Studio", "[stress][studio]") {
    stress::prepare();
    stress::Watchdog watchdog("Studio", 30);
    int rounds = stress::scaled(8, 60, 300);
    int operations = 0, terrainOperations = 0, compiled = 0, refusals = 0, refused = -1;
    stress::Distribution durations;
    double baseline = 0, clock = 0;
    ld::Experience carried = stress::project();
    for (int round = 0; round < rounds; ++round) {
        double started = stress::now();
        {
            Bench bench;
            auto& d = bench.document;
            REQUIRE(d.open(carried));
            if (d.world.blocks.size() < 30) populate(d, 12);
            std::vector<std::string> ids;
            for (const auto& b : d.world.blocks)
                if (!b.locked) ids.push_back(b.id);
            REQUIRE(ids.size() >= 4);
            d.select({ids[0]});
            d.update(ids[0], [round](ld::WorldBlock& b) { b.x += 0.25 * (round % 5 - 2); });
            d.select({ids[1], ids[2], ids[3]});
            d.perform(ld::StudioEdit::rotateGroup(1, 15, std::nullopt));
            d.perform(ld::StudioEdit::scaleGroup(0, 1.1, ld::ScaleAnchor::middle(), std::nullopt));
            for (int i = 0; i < 4; ++i) d.undo();
            for (int i = 0; i < 4; ++i) d.redo();
            d.run(ld::StudioCommand::duplicate);
            d.run(ld::StudioCommand::group);
            d.run(ld::StudioCommand::ungroup);
            d.run(ld::StudioCommand::copy);
            d.run(ld::StudioCommand::paste);
            d.run(ld::StudioCommand::remove);
            d.run(ld::StudioCommand::undo);
            d.run(ld::StudioCommand::redo);
            operations += 20;
            bench.stage.orient(round * 0.4, 0.5, 20 + round % 9, {0, 2, 0});
            bench.stage.refresh();
            if (round % 2 == 0) d.perform(ld::StudioEdit::generate({round + 1, 3, 0, 1}));
            int stroke = d.beginStroke();
            for (int i = 0; i < 12; ++i) {
                auto tool = static_cast<ld::TerrainTool>((round + i) % 6);
                d.perform(ld::StudioEdit::paint(tool, {std::sin(i * 0.7) * 14, 2.0 + i % 3, std::cos(i * 0.9) * 14}, stroke));
                ++terrainOperations;
            }
            d.endStroke();
            d.undo();
            d.redo();
            bench.stage.refresh();
            auto script = d.insert("Script", std::string("ServerScriptService"));
            REQUIRE(script);
            d.setSource(*script, round % 4 == 3 ? "var x = \nprint(" : "print(\"giro " + std::to_string(round) + "\")\nawait Task.nextFrame()\n");
            d.openScript(*script);
            ++compiled;
            auto value = d.insert("StringValue", std::string("ReplicatedStorage"));
            if (value) d.setProperty(*value, "Value", ld::PropertyValue::string("stress " + std::to_string(round)));
            d.playMode = round % 2 ? ld::StudioPlayMode::camera : ld::StudioPlayMode::character;
            d.play();
            drive(bench, 20, clock);
            d.stop();
            d.playMode = ld::StudioPlayMode::camera;
            d.run(ld::StudioCommand::run);
            drive(bench, 6, clock);
            d.stop();
            d.closeScript(*script);
            if (round % 4 == 3) d.select({*script});
            if (round % 4 == 3) d.deleteSelection();
            while (d.world.blocks.size() > 60) {
                d.select({d.world.blocks.back().id});
                d.deleteSelection();
            }
            while (d.world.instances.size() > 40) {
                d.select({d.world.instances.back().id});
                d.deleteSelection();
            }
            auto saved = d.publishable();
            while (!saved && saved.error().code == ld::ErrorCode::limit && d.world.blocks.size() > 8) {
                if (std::exchange(refused, round) != round) ++refusals;
                d.select({d.world.blocks.back().id});
                d.deleteSelection();
                saved = d.publishable();
            }
            INFO("giro " << round << ": " << (saved ? std::string() : saved.error().message) << "; parti " << d.world.blocks.size() << ", istanze " << d.world.instances.size());
            REQUIRE(saved);
            auto reloaded = ld::Experience::parse(saved->json().dump());
            REQUIRE(reloaded.ok());
            CHECK(*reloaded == *saved);
            carried = *reloaded;
            operations += 12;
        }
        durations.add(stress::now() - started);
        watchdog.beat();
        if (round == std::min(3, rounds / 2)) baseline = stress::megabytes();
    }
    double growth = stress::megabytes() - baseline;
    stress::record("studio", "giri", rounds);
    stress::record("studio", "operazioni", operations);
    stress::record("studio", "pubblicazioni rifiutate per limite di spazio e poi riuscite", refusals);
    stress::record("studio", "operazioni sul Terrain", terrainOperations);
    stress::record("studio", "script compilati", compiled);
    stress::record("studio", "durata del giro", durations);
    stress::record("studio", "memoria", ld::Json::object({{"riferimento", baseline}, {"finale", stress::megabytes()}, {"crescita", growth}}));
    INFO(rounds << " giri, crescita " << growth << " MB");
    UNINSTRUMENTED_CHECK(growth < 40);
    TIMED_CHECK(durations.percentile(0.95) < durations.percentile(0.5) * 4 + 0.1);
}
