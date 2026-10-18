#include "Engine/Character/Character.hpp"
#include "Engine/Rendering/Renderer.hpp"
#include "Engine/Rendering/World.hpp"
#include "Engine/Scripting/WorldSession.hpp"
#include "Lim/Compiler/Compiler.hpp"
#include "Lim/Parser/Parser.hpp"
#include "Lim/Tooling/Tooling.hpp"
#include "Rig.hpp"

#include <cmath>
#include <random>

namespace {

ld::InstanceRecord script(const std::string& name, const std::string& source, const std::string& className = "Script", const std::string& parent = "ServerScriptService") {
    ld::InstanceRecord r;
    r.id = ld::makeUuid();
    r.className = className;
    r.name = name;
    r.parent = parent;
    r.setSource(source);
    return r;
}

}

TEST_CASE("Stress: Lim sotto carico", "[stress][lim]") {
    stress::prepare();
    stress::Watchdog watchdog("Lim", 40);
    static const char* sources[] = {
        "var n = 0\non Time.heartbeat(delta) {\n    n += 1\n}\n",
        "while true {\n    await Task.wait(0.02)\n}\n",
        "for i in 1...200 {\n    await Task.nextFrame()\n}\n",
        "var connessioni = 0\nfor i in 1...40 {\n    let c = World.childAdded.connect(fn(child) { })\n    c.disconnect()\n    connessioni += 1\n}\nprint(connessioni)\n",
        "let p = Part(World, \"Temporanea\")\np.anchored = true\nawait Task.wait(0.03)\np.destroy()\n",
        "let lista = [1, 2, 3]\nprint(lista[9])\n",
        "while true { }\n",
        "var dati = [0]\nwhile true {\n    dati.append(dati.count)\n}\n",
        "fn ricorsione(n) { return ricorsione(n + 1) }\nricorsione(0)\n",
        "import Modulo\nprint(Modulo.doppio(21))\n",
        "var x = \nprint(",
        "Task.spawn(fn() { error(\"errore voluto\") })\nawait Task.wait(0.01)\nprint(\"vivo\")\n",
    };
    int kinds = static_cast<int>(sizeof sources / sizeof sources[0]);
    int count = stress::scaled(60, 240, 600), rounds = stress::scaled(3, 12, 40), frames = stress::scaled(90, 240, 600);
    ld::Experience world = ld::Experience::blank();
    world.instances.push_back(script("Modulo", "export fn doppio(x) { return x * 2 }\n", "ModuleScript", "ReplicatedStorage"));
    for (int i = 0; i < count; ++i) world.instances.push_back(script("Script" + std::to_string(i), sources[i % kinds]));
    stress::Distribution steps;
    double baseline = 0;
    int errors = 0, outputs = 0;
    for (int round = 0; round < rounds; ++round) {
        std::vector<ld::OutputLine> lines;
        {
            ld::WorldSession session(world, ld::WorldSession::Mode::play, "Stress", 1, false, [&](const ld::OutputLine& line) {
                lines.push_back(line);
                watchdog.beat();
            });
            session.physics = round % 2 == 0;
            session.start({0, 1, 0});
            for (int frame = 0; frame < frames; ++frame) {
                double started = stress::now();
                session.place(std::sin(frame * 0.1) * 3, 1, std::cos(frame * 0.1) * 3, frame * 0.05);
                session.advance(1.0 / 60);
                steps.add(stress::now() - started);
                watchdog.beat();
            }
            session.stop();
            CHECK(session.running() == 0);
        }
        outputs += static_cast<int>(lines.size());
        errors += static_cast<int>(std::count_if(lines.begin(), lines.end(), [](const ld::OutputLine& l) { return l.error(); }));
        CHECK(std::any_of(lines.begin(), lines.end(), [](const ld::OutputLine& l) { return l.text == "42"; }));
        CHECK(std::any_of(lines.begin(), lines.end(), [](const ld::OutputLine& l) { return l.text == "vivo"; }));
        if (round == 0) baseline = stress::megabytes();
    }
    double growth = stress::megabytes() - baseline;
    stress::record("lim", "script per sessione", count + 1);
    stress::record("lim", "sessioni", rounds);
    stress::record("lim", "righe di output", outputs);
    stress::record("lim", "errori di script gestiti", errors);
    stress::record("lim", "passo della sessione", steps);
    stress::record("lim", "memoria", ld::Json::object({{"riferimento", baseline}, {"finale", stress::megabytes()}, {"crescita", growth}}));
    INFO("passo p50 " << steps.percentile(0.5) * 1000 << " ms, p99 " << steps.percentile(0.99) * 1000 << ", peggiore " << steps.worst() * 1000 << "; errori gestiti " << errors << "; crescita " << growth << " MB");
    CHECK(errors > 0);
    TIMED_CHECK(steps.worst() < 1.0);
    TIMED_CHECK(!stress::regressed("lim, passo p50", steps.percentile(0.5)));
    UNINSTRUMENTED_CHECK(growth < 60);
}

TEST_CASE("Stress: modifiche casuali nell'editor Lim", "[stress][editor]") {
    stress::Watchdog watchdog("Editor", 20);
    lim::TypeRegistry registry = lim::standardRegistry();
    lim::tooling::Context context{registry, {}};
    std::string corpus = "let limite = 16\nvar punti = [1, 2, 3]\nenum Stato { fermo, corsa }\nclass Arma {\n    var danno: Number = 25\n    fn colpisci(b: Number) -> Number { return b - danno }\n}\nfn somma(a: Number, b: Number = 0) -> Number { return a + b }\nvar stato: Stato = .fermo\nmatch stato {\n    .fermo { print(\"fermo\") }\n    else { print(\"altro\") }\n}\nif limite > 3 && punti.count != 0 { print(\"alto \\(limite)\") } else { punti[0] = somma(1, b: 2) }\nfor i in 0..<3 { print(i) }\nwhile limite < 0 { break }\nlet doppio = fn(x) { return x * 2 }\non Players.joined(player) { print(player.name) }\n";
    std::mt19937 random(20261002);
    const std::string fragments[] = {"", "(", ")", "{", "}", "[", "]", "=", "==", ".", ",", ":", "\"", "\\(", "match ", "fn ", "class ", "enum ", "on ", "if ", "else ", "for ", "in ", "while ", "await ", "?.", "??", "->", "\n", " ", "x", "1", "//", "/*", "*/", "@"};
    int iterations = stress::scaled(1500, 12000, 60000);
    std::string source = corpus;
    for (int i = 0; i < iterations; ++i) {
        if (i % 40 == 0) source = corpus;
        std::size_t position = source.empty() ? 0 : random() % (source.size() + 1);
        switch (random() % 4) {
        case 0: source.insert(position, fragments[random() % (sizeof fragments / sizeof fragments[0])]); break;
        case 1: if (position < source.size()) source.erase(position, 1 + random() % 6); break;
        case 2: source = source.substr(0, position); break;
        default: if (position < source.size()) source[position] = static_cast<char>(32 + random() % 95); break;
        }
        auto parsed = lim::parse(source, "stress");
        lim::analyze(parsed.program, registry, {});
        lim::compileSource(source, "Stress", registry, {});
        std::uint32_t offset = source.empty() ? 0 : static_cast<std::uint32_t>(random() % (source.size() + 1));
        lim::tooling::hover(source, offset, context);
        lim::tooling::complete(source, offset, context);
        lim::tooling::definition(source, offset, context);
        lim::tooling::references(source, offset, context);
        lim::tooling::highlight(source);
        if (i % 16 == 0) lim::tooling::format(source);
        if (i % 16 == 8) lim::tooling::rename(source, offset, "nuovo", context);
        watchdog.beat();
    }
    stress::record("editor", "modifiche analizzate", iterations);
    SUCCEED();
}

TEST_CASE("Stress: terreno sotto carico", "[stress][terrain]") {
    stress::prepare();
    stress::Watchdog watchdog("Terrain", 40);
    ld::Terrain terrain;
    terrain.generate({11, 4, 0, 1});
    ld::render::TerrainView scene;
    scene.sync(terrain);
    int strokes = stress::scaled(150, 1200, 6000);
    stress::Distribution edits, meshes;
    double baseline = stress::megabytes();
    std::mt19937 random(7);
    for (int i = 0; i < strokes; ++i) {
        ld::TerrainBrush brush;
        brush.radius = 2 + (i % 4);
        brush.strength = 0.6;
        brush.material = i % 2 ? ld::TerrainMaterial::grass : ld::TerrainMaterial::rock;
        ld::Vec3 point{static_cast<double>(static_cast<int>(random() % 80)) - 40, 2.0 + static_cast<double>(random() % 6), static_cast<double>(static_cast<int>(random() % 80)) - 40};
        double started = stress::now();
        terrain.apply(static_cast<ld::TerrainTool>(i % 6), point, brush);
        edits.add(stress::now() - started);
        started = stress::now();
        scene.sync(terrain);
        meshes.add(stress::now() - started);
        watchdog.beat();
    }
    auto encoded = terrain.json();
    auto decoded = ld::Terrain::fromJson(encoded);
    REQUIRE(decoded.ok());
    CHECK(*decoded == terrain);
    ld::CharacterDriver driver;
    std::vector<ld::WorldBlock> blocks;
    driver.reset(ld::motion::spawn(blocks, terrain, -30, -30));
    ld::CharacterInput input;
    input.forward = 1;
    int steps = stress::scaled(2400, 12000, 60000), jumps = 0, falls = 0;
    double previous = driver.state().y, shown = driver.pose(1.0 / 60).y, worst = 0, worstShown = 0;
    double climb = ld::motion::stepUp * 0.5;
    int pops = 0;
    std::string detail;
    for (int i = 0; i < steps; ++i) {
        double yaw = std::sin(i * 0.002) * 3.1;
        input.strafe = (i / 120) % 3 - 1;
        driver.advance(1.0 / 60, [&](const ld::CharacterState& state, double step) -> std::optional<ld::CharacterState> {
            return ld::motion::integrate(state, input, yaw, {}, terrain, step);
        });
        const auto& state = driver.state();
        REQUIRE(std::isfinite(state.x + state.y + state.z));
        double rise = state.y - previous;
        if (rise > ld::motion::stepUp + 0.05) ++jumps;
        worst = std::max(worst, std::fabs(rise));
        previous = state.y;
        double pose = driver.pose(1.0 / 60).y;
        if (pose - shown > climb) {
            ++pops;
            detail += " [passo " + std::to_string(i) + ": mostrata " + std::to_string(shown) + " -> " + std::to_string(pose) + ", simulazione " + std::to_string(driver.previous().y) + " -> " + std::to_string(state.y) + ", a terra " + std::to_string(driver.previous().grounded) + " -> " + std::to_string(state.grounded) + ", nuoto " + std::to_string(state.swimming) + ", verticale " + std::to_string(state.vertical) + ", alfa " + std::to_string(driver.alpha()) + "]";
        }
        worstShown = std::max(worstShown, pose - shown);
        shown = pose;
        if (state.y < ld::motion::voidHeight + 5) {
            driver.reset(ld::motion::spawn(blocks, terrain, 0, 0));
            previous = driver.state().y;
            shown = driver.pose(1.0 / 60).y;
            ++falls;
        }
        if (i % 500 == 0) watchdog.beat();
    }
    double growth = stress::megabytes() - baseline;
    stress::record("terrain", "pennellate", strokes);
    stress::record("terrain", "pennellata", edits);
    stress::record("terrain", "aggiornamento mesh", meshes);
    stress::record("terrain", "passi del personaggio", steps);
    stress::record("terrain", "salti verticali oltre il gradino", jumps);
    stress::record("terrain", "scatti della posa mostrata", pops);
    stress::record("terrain", "salita massima per frame", ld::Json::object({{"simulazione", worst}, {"posa mostrata", worstShown}, {"limite della posa", climb}}));
    stress::record("terrain", "cadute nel vuoto con rientro", falls);
    stress::record("terrain", "memoria", ld::Json::object({{"riferimento", baseline}, {"finale", stress::megabytes()}, {"crescita", growth}}));
    INFO("mesh p95 " << meshes.percentile(0.95) * 1000 << " ms, peggiore " << meshes.worst() * 1000 << "; salti " << jumps << "; scatti mostrati " << pops << "; variazione verticale massima " << worst << ", mostrata " << worstShown << " su limite " << climb << detail);
    CHECK(jumps == 0);
    CHECK(pops == 0);
    TIMED_CHECK(meshes.percentile(0.95) < 1.0 / 60);
    TIMED_CHECK(!stress::regressed("terrain, mesh p95", meshes.percentile(0.95)));
    UNINSTRUMENTED_CHECK(growth < 200);
}

TEST_CASE("Stress: scene di dimensione crescente", "[stress][objects]") {
    stress::prepare();
    stress::Watchdog watchdog("Oggetti", 60);
    ld::Json results = ld::Json::object();
    std::vector<int> sizes{100, 1000, 5000};
    if (stress::profile() != stress::Profile::quick) sizes.push_back(10000);
    if (stress::profile() == stress::Profile::extended) sizes.push_back(25000);
    int degraded = 0;
    for (int size : sizes) {
        ld::Experience world = ld::Experience::blank();
        for (int i = 1; i < size; ++i) {
            ld::WorldBlock part;
            part.id = ld::makeUuid();
            part.name = "Parte" + std::to_string(i);
            part.kind = i % 3 == 0 ? "sphere" : "cube";
            part.x = (i % 100) * 2 - 100;
            part.z = (i / 100) * 2 - 100;
            part.y = 0.5 + (i % 5);
            world.blocks.push_back(part);
        }
        double started = stress::now();
        std::string saved = world.json().dump();
        double saving = stress::now() - started;
        started = stress::now();
        auto loaded = ld::Experience::parse(saved);
        double loading = stress::now() - started;
        REQUIRE(loaded.ok());
        started = stress::now();
        stress::Distribution steps;
        {
            ld::WorldSession session(world, ld::WorldSession::Mode::play, "Stress", 1, false, [](const ld::OutputLine&) {});
            session.physics = false;
            session.start({0, 6, 0});
            for (int frame = 0; frame < 60; ++frame) {
                double begun = stress::now();
                session.place(frame * 0.1, 6, 0, 0);
                session.advance(1.0 / 60);
                steps.add(stress::now() - begun);
            }
            session.stop();
        }
        watchdog.beat();
        auto scene = ld::render::worldScene(world);
        auto camera = ld::render::Node::make("stress.camera");
        camera->camera = ld::render::Camera{};
        camera->camera->zFar = 600;
        camera->position = {0, 60, 160};
        camera->look({0, 0, 0});
        scene->scene.root->add(camera);
        for (double warm = stress::now(); stress::now() - warm < 0.5;) static_cast<void>(ld::render::Renderer::shared().snapshot(scene->scene, *camera, 1280, 800, 4, false));
        stress::Distribution frames;
        for (int frame = 0; frame < 40; ++frame) {
            camera->position = {std::sin(frame * 0.1) * 160, 60, std::cos(frame * 0.1) * 160};
            camera->look({0, 0, 0});
            double begun = stress::now();
            static_cast<void>(ld::render::Renderer::shared().snapshot(scene->scene, *camera, 1280, 800, 4, false));
            frames.add(stress::now() - begun);
            watchdog.beat();
        }
        bool fluid = frames.percentile(0.95) < 1.0 / 30 && steps.percentile(0.95) < 1.0 / 60;
        if (!fluid && !degraded) degraded = size;
        results.set(std::to_string(size), ld::Json::object({{"salvataggio ms", saving * 1000}, {"caricamento ms", loading * 1000}, {"passo sessione p95 ms", steps.percentile(0.95) * 1000}, {"frame p50 ms", frames.percentile(0.5) * 1000},
                                                             {"frame p95 ms", frames.percentile(0.95) * 1000}, {"fluido", fluid}, {"megabyte", stress::megabytes()}}));
    }
    stress::record("oggetti", "per dimensione", results);
    stress::record("oggetti", "limite del progetto", ld::rules::world::parts);
    stress::record("oggetti", "primo degrado a", degraded);
    INFO(results.dump(true));
    TIMED_CHECK((degraded == 0 || degraded > ld::rules::world::parts));
}
