#include "Engine/Backend/Assets.hpp"
#include "Engine/Backend/LocalServer.hpp"
#include "Engine/Character/Character.hpp"
#include "Engine/Core/Resources.hpp"
#include "Engine/Core/Text.hpp"
#include "Engine/Rendering/Renderer.hpp"
#include "Engine/Rendering/World.hpp"
#include "Engine/Studio/StudioDocument.hpp"
#include "Golden.hpp"
#include "Lim/Compiler/Compiler.hpp"
#include "Lim/Parser/Parser.hpp"
#include "Tests/Support/Timing.hpp"

#include <filesystem>
#include <mach/mach.h>

#include <algorithm>
#include <chrono>
#include <thread>

using namespace ld;

namespace {
using Clock = std::chrono::steady_clock;
double milliseconds(Clock::time_point start) { return std::chrono::duration<double, std::milli>(Clock::now() - start).count(); }

Json percentiles(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    auto at = [&](double fraction) { return values[std::min(values.size() - 1, static_cast<std::size_t>(fraction * (values.size() - 1)))]; };
    return Json::object({{"count", static_cast<int>(values.size())}, {"p50Milliseconds", at(.50)}, {"p95Milliseconds", at(.95)}, {"p99Milliseconds", at(.99)}});
}

void evidence(const std::string& name, const Json& data) {
    if (const char* folder = std::getenv("LUDISSEY_SHOTS")) {
        std::filesystem::create_directories(folder);
        std::ofstream stream(std::filesystem::path(folder) / (name + ".json"));
        stream << data.dump() << '\n';
        REQUIRE(stream.good());
    }
}

struct TemporaryServer {
    std::filesystem::path folder = std::filesystem::temp_directory_path() / ("LudisseyConcurrency-" + makeUuid());
    ~TemporaryServer() { std::error_code error; std::filesystem::remove_all(folder, error); }
};
}

TEST_CASE("Prestazioni: 100 join sul server locale", "[native-server][stress][concurrency]") {
    TemporaryServer files;
    LocalServer server(files.folder);
    server.enrollNow(PlayerId("owner"), {"Owner", "male", "2026-09-29"});
    auto world = Experience::blank();
    world.serverSize = 10;
    server.invokeNow(PlayerId("owner"), "LudisseyPublishWorld", Json::object({{"world", world.json()}}));
    std::vector<Json> tickets(100);
    std::vector<std::exception_ptr> failures(100);
    auto parallel = [&](const std::function<void(LocalServer&, int)>& action) {
        std::vector<std::thread> workers;
        for (int worker = 0; worker < 4; ++worker) workers.emplace_back([&, worker] {
            LocalServer independent(files.folder);
            for (int i = worker; i < 100; i += 4) {
                try { action(independent, i); } catch (...) { failures[i] = std::current_exception(); }
            }
        });
        for (auto& worker : workers) worker.join();
        for (int i = 0; i < 100; ++i) {
            INFO(i);
            if (failures[i]) FAIL(describe(failures[i]));
        }
    };
    auto started = Clock::now();
    parallel([&](LocalServer& backend, int i) {
        PlayerId player("P" + std::to_string(i));
        backend.enrollNow(player, {player.text(), "male", "2026-09-29"});
        tickets[i] = backend.invokeNow(player, "LudisseyRequestSession", Json::object({{"experienceId", world.id}, {"playerProtocol", rules::playerProtocol}}));
    });
    auto details = [&] { return server.invokeNow(PlayerId("owner"), "LudisseyExperience", Json::object({{"id", world.id}})); };
    CHECK(details()["servers"].size() == 0);
    CHECK(details()["experience"]["visits"].integer() == 0);
    parallel([&](LocalServer& backend, int i) {
        PlayerId player("P" + std::to_string(i));
        auto args = Json::object({{"experienceId", world.id}, {"sessionId", tickets[i]["sessionId"]}, {"joinTicket", tickets[i]["joinTicket"]}});
        backend.invokeNow(player, "LudisseyOpenSession", args);
        backend.invokeNow(player, "LudisseyReadySession", args);
        backend.invokeNow(player, "LudisseyReadySession", args);
    });
    auto full = details();
    REQUIRE(full["servers"].size() == 10);
    for (const auto& item : full["servers"].items()) CHECK(item["players"].integer() == 10);
    CHECK(full["experience"]["visits"].integer() == 100);
    parallel([&](LocalServer& backend, int i) {
        backend.invokeNow(PlayerId("P" + std::to_string(i)), "LudisseyLeaveSession", Json::object({{"experienceId", world.id}, {"sessionId", tickets[i]["sessionId"]}}));
    });
    CHECK(details()["servers"].size() == 0);
    CHECK(details()["experience"]["visits"].integer() == 100);
    evidence("native-concurrency", Json::object({{"players", 100}, {"independentOwners", 4}, {"milliseconds", milliseconds(started)}}));
}

TEST_CASE("Prestazioni: Gioca e Ferma con 1000 istanze", "[stress][ownership][lim]") {
    golden::registry();
    auto world = Experience::blank();
    for (int i = 1; i < 1000; ++i) {
        WorldBlock part;
        part.name = "Part" + std::to_string(i);
        part.x = (i % 30) * 2 - 30;
        part.z = (i / 30) * 2 - 30;
        world.blocks.push_back(part);
    }
    world.terrain.generate({42, 4, 0, 1});
    for (int i = 0; i < 100; ++i) {
        InstanceRecord script;
        script.id = makeUuid();
        script.className = "Script";
        script.name = "Script" + std::to_string(i);
        script.parent = "ServerScriptService";
        script.setSource("let p = Part()\np.name = \"Created\"\np.anchored = true\nWorld.add(p)\nawait Task.nextFrame()\nprint(script.name)\n");
        world.instances.push_back(script);
    }
    auto serialized = world.json();
    auto readback = Experience::fromJson(serialized);
    REQUIRE(readback);
    CHECK(*readback == world);
    std::vector<double> starts;
    std::vector<std::weak_ptr<LiveInstance>> roots;
    for (int iteration = 0; iteration < 20; ++iteration) {
        auto begin = Clock::now();
        {
            std::vector<OutputLine> lines;
            WorldSession session(world, WorldSession::Mode::run, "Stress", 0, true, [&](const auto& line) { lines.push_back(line); });
            roots.push_back(session.model->game);
            session.physics = false;
            session.start({0, 1, 0});
            session.advance(1.0 / 60);
            session.advance(1.0 / 60);
            REQUIRE(session.parts().size() == 1100);
            REQUIRE(lines.size() == 100);
            for (const auto& line : lines) CHECK_FALSE(line.error());
            session.stop();
            CHECK(session.running() == 0);
            CHECK(world.json() == serialized);
        }
        starts.push_back(milliseconds(begin));
        for (const auto& root : roots) CHECK(root.expired());
    }
    evidence("play-stop-stress", percentiles(starts));
}

TEST_CASE("Prestazioni: compilatore e VM di Lim", "[lim][performance]") {
    std::string source = "fn twice(value: Number) -> Number { return value * 2 }\nvar total = 0.0\nfor i in 1...100 { total += twice(i) }\n";
    auto registry = lim::standardRegistry();
    lim::AnalyzeOptions options;
    Json result = Json::object();
    auto measure = [&](const std::string& name, const std::function<void()>& operation) {
        std::vector<double> samples;
        for (int i = 0; i < 100; ++i) {
            auto start = Clock::now();
            operation();
            samples.push_back(milliseconds(start));
        }
        result.set(name, percentiles(samples));
    };
    measure("lex", [&] { REQUIRE(lim::lex(source).errors.empty()); });
    measure("parseIncludingLex", [&] { REQUIRE(lim::parse(source).diagnostics.empty()); });
    auto parsed = lim::parse(source);
    measure("semantic", [&] { REQUIRE(lim::analyze(parsed.program, registry, options).diagnostics.empty()); });
    auto analysis = lim::analyze(parsed.program, registry, options);
    measure("bytecodeCompile", [&] { CHECK_FALSE(lim::validate(lim::compileProgram(parsed.program, analysis, "Perf", options.context))); });
    auto module = std::make_shared<lim::Module>(lim::compileProgram(parsed.program, analysis, "Perf", options.context));
    measure("vmStartupAndExecution", [&] {
        lim::Vm vm;
        auto environment = vm.newEnvironment("Perf");
        auto fiber = vm.run(module, environment, "Perf");
        REQUIRE(fiber->state == lim::Fiber::State::done);
        CHECK(environment->values.at("total").toNumber() == 10100);
    });
    evidence("lim-pipeline", result);
}

TEST_CASE("Prestazioni: runtime Lim e DataModel", "[lim][performance][stress]") {
    golden::registry();
    DataModel model(Experience::blank());
    std::vector<std::string> lines;
    ScriptEngine engine(model, [&](const OutputLine& l) { lines.push_back(l.text); });
    auto measure = [&](const std::string& label, const std::string& source, double limit) {
        lines.clear();
        auto started = std::chrono::steady_clock::now();
        engine.execute(source, ScriptContext::server, "Workspace.Misura");
        double elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        INFO(label << ": " << elapsed << " ms");
        for (const auto& l : lines) INFO(l);
        TIMED_CHECK(elapsed < limit);
        UNINSTRUMENTED_CHECK(std::none_of(lines.begin(), lines.end(), [](const std::string& l) { return l.find("Workspace.Misura:") != std::string::npos; }));
    };
    measure("ciclo da 1.000.000 somme", "var s = 0\nfor i in 1...1000000 {\n    s += i\n}\nprint(s)", 250);
    UNINSTRUMENTED_CHECK(lines == std::vector<std::string>{"500000500000"});
    measure("1000 Part con parent", "for i in 1...1000 {\n    let p = Part(parent: World)\n    p.name = \"P\\(i)\"\n}", 250);
    measure("find su 1000 figli", "for i in 1...1000 {\n    let p = World.find(\"P\\(i)\")\n}", 250);
    measure("1000 eventi BindableEvent", "let e = BindableEvent()\nvar n = 0\ne.event.connect(fn() {\n    n += 1\n})\nfor i in 1...1000 {\n    e.fire()\n}\nprint(n)", 250);
    CHECK(lines == std::vector<std::string>{"1000"});
    Experience world;
    for (int i = 0; i < 400; ++i) {
        InstanceRecord r;
        r.id = makeUuid();
        r.className = "Script";
        r.name = "S" + std::to_string(i);
        r.parent = "ServerScriptService";
        r.properties["Source"] = PropertyValue::string("let x = " + std::to_string(i) + "\n");
        world.instances.push_back(r);
    }
    auto started = std::chrono::steady_clock::now();
    DataModel loaded(world);
    double elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    INFO("DataModel da 400 istanze: " << elapsed << " ms");
    TIMED_CHECK(elapsed < 100);
    CHECK(loaded.service("ServerScriptService")->children.size() == 400);
}

namespace {

double percentileOf(std::vector<double> values, double fraction) {
    std::sort(values.begin(), values.end());
    return values.empty() ? 0 : values[std::min(values.size() - 1, static_cast<std::size_t>(fraction * values.size()))];
}

template <typename F>
std::vector<double> timings(int count, F body) {
    std::vector<double> out;
    for (int i = 0; i < count; ++i) {
        auto started = std::chrono::steady_clock::now();
        body();
        out.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
    }
    return out;
}

template <typename F>
double average(int count, F body) {
    auto t = timings(count, body);
    double sum = 0;
    for (double v : t) sum += v;
    return sum / count;
}

double residentMegabytes() {
    mach_task_basic_info info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    return task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS ? info.resident_size / 1048576.0 : 0;
}

}

TEST_CASE("Prestazioni: asset, terreno, fisica e Metal", "[performance][stress][rendering]") {
    golden::registry();
    auto folder = std::filesystem::temp_directory_path() / ("Ludissey Prestazioni " + makeUuid());
    asset_cache::setFolder(folder);
    std::string payload(2 * 1024 * 1024, '\0');
    for (std::size_t i = 0; i < payload.size(); ++i) payload[i] = static_cast<char>(i % 251);
    std::string asset = asset_cache::identifier(payload, "obj");
    asset_cache::store(payload, asset);
    double hashed = average(20, [&] { (void)sha256Hex(asset_cache::readFile(asset_cache::location(asset))); });
    (void)asset_cache::verified(asset);
    double memo = average(20, [&] { (void)asset_cache::verified(asset); });
    WARN("verifica asset 2 MB: hash a ogni chiamata " << hashed << " ms, verifica memorizzata " << memo << " ms");
    CHECK(memo < hashed);
    asset_cache::setFolder(std::nullopt);
    std::filesystem::remove_all(folder);

    Terrain terrain;
    double generate = average(5, [&] { terrain = Terrain(); terrain.generate(TerrainSettings{2026, 6, 1, 0}); });
    int changedCells = 0;
    TerrainBrush brush;
    brush.radius = 4;
    brush.strength = 0.5;
    brush.material = TerrainMaterial::rock;
    double single = average(50, [&] { Terrain copy = terrain; changedCells = copy.apply(TerrainTool::add, {3, 2, 3}, brush); });
    CHECK(changedCells > 0);
    render::TerrainView view;
    double full = average(1, [&] { view.sync(terrain); });
    Terrain edited = terrain;
    TerrainBrush small;
    small.radius = 2;
    small.strength = 1;
    edited.apply(TerrainTool::add, {-26, 2, -26}, small);
    double partial = average(1, [&] { view.sync(edited); });
    CHECK(partial < full);
    std::size_t encoded = terrain.json().dump().size();
    CHECK(encoded <= static_cast<std::size_t>(rules::terrain::keys * rules::terrain::keyCharacters));
    auto walker = motion::spawn({}, terrain, 0, 0);
    CharacterInput forward;
    forward.forward = 1;
    double step = average(600, [&] { walker = motion::step(walker, forward, 0.3, {}, terrain, 1.0 / 60); });
    CHECK(step < 1000.0 / 60);
    auto runner = motion::spawn({}, terrain, 0, 0);
    auto steps = timings(2000, [&] { runner = motion::step(runner, forward, 0.7, {}, terrain, 1.0 / 60); });
    WARN("Terrain: Generate " << generate << " ms, pennellata " << single << " ms (" << changedCells << " celle), mesh completa " << full << " ms, incrementale " << partial
                              << " ms, payload " << encoded << " byte; passo del personaggio p50 " << percentileOf(steps, 0.5) << " p95 " << percentileOf(steps, 0.95) << " p99 "
                              << percentileOf(steps, 0.99) << " ms");
    CHECK(percentileOf(steps, 0.99) < 1000.0 / 120);

    Experience stage = Experience::blank();
    for (int i = 0; i < rules::world::parts - 1; ++i) {
        WorldBlock b;
        b.id = makeUuid();
        b.name = "Parte" + std::to_string(i);
        b.x = i % 20 - 10;
        b.z = i / 20 - 5;
        b.y = 0.5;
        stage.blocks.push_back(b);
    }
    stage.terrain = terrain;
    double before = residentMegabytes();
    ld::setResourceFolder(golden::root() / "Resources");
    auto scene = render::worldScene(stage);
    auto body = render::scene_factory::avatar(AvatarConfiguration::standard("Misura", "male"));
    REQUIRE(body);
    scene->scene.root->add(body->node);
    auto camera = render::Node::make("misura.camera");
    camera->camera = render::Camera{};
    camera->camera->zFar = 400;
    camera->position = {0, 14, 26};
    camera->look({0, 1, 0});
    scene->scene.root->add(camera);
    (void)render::Renderer::shared().snapshot(scene->scene, *camera, 1280, 800, 4, true);
    double clock = 0;
    auto frames = timings(90, [&] {
        clock += 1.0 / 60;
        scene->scene.time = clock;
        body->animate("Walk", 1.0 / 60);
        (void)render::Renderer::shared().snapshot(scene->scene, *camera, 1280, 800, 4, true);
    });
    double after = residentMegabytes();
    WARN("frame offscreen 1280×800 MSAA 4× con 199 parti, Terrain, acqua animata e avatar (lettura pixel inclusa): p50 " << percentileOf(frames, 0.5) << " ms, p95 " << percentileOf(frames, 0.95)
                                                                                                                         << " ms, p99 " << percentileOf(frames, 0.99) << " ms · memoria residente " << before
                                                                                                                         << " → " << after << " MB");
    TIMED_CHECK(percentileOf(frames, 0.5) < 1000.0 / 30);
}
