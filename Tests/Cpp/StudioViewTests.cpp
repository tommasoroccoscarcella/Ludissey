#include "CursorAudit.hpp"
#include "Engine/Backend/Assets.hpp"
#include "Engine/Backend/LocalServer.hpp"
#include "Engine/Core/LocalPreferences.hpp"
#include "Engine/Studio/MacArchive.hpp"
#include "ServerHarness.hpp"
#include "Studio/StudioView.hpp"

namespace {
struct RegistryLoaded {
    RegistryLoaded() { golden::registry(); }
};

struct StudioFixture {
    RegistryLoaded registry;
    harness::Fixture backend;
    std::shared_ptr<ld::Store> store = std::make_shared<ld::Store>(std::make_shared<ld::PlayFab>(std::make_shared<ld::MemoryStore>()), backend.world->registerPlayer("Pstudio", "Creatore"), nullptr, false);
    studio::StudioShell studio{store};
    ui::Surface surface{[this] { return studio.build(); }};
    StudioFixture() {
        golden::registry();
        studio.invalidated = [this] { surface.invalidate(); };
        store->changed = studio.invalidated;
    }
    ~StudioFixture() {
        store->changed = nullptr;
        ld::LaunchRequest::shared().studioProject.reset();
    }
    void draw() {
        surface.resize({1180, 760}, 1);
        ui::Canvas canvas(1180, 760, 1);
        surface.render(canvas);
        studio.rendered(surface);
    }
    void refresh() {
        surface.invalidate();
        draw();
    }
};
}

TEST_CASE("Studio: richieste con l'account occupato", "[studio][interface]") {
    StudioFixture f;
    auto world = ld::Experience::blank();
    f.store->setWorlds({world});
    ld::LaunchRequest::shared().studioProject = world.id;
    f.store->busy = true;
    f.draw();
    CHECK(ld::LaunchRequest::shared().studioProject == world.id);
    CHECK_FALSE(f.studio.document.opened);
    f.store->busy = false;
    f.store->restoringSession = true;
    f.draw();
    CHECK(ld::LaunchRequest::shared().studioProject == world.id);
    f.store->restoringSession = false;
    f.draw();
    CHECK_FALSE(ld::LaunchRequest::shared().studioProject);
    CHECK(f.studio.document.world.id == world.id);
    REQUIRE(f.studio.stage);
    CHECK_FALSE(f.studio.document.explorerFrames.empty());
    CHECK(f.studio.stage->size.width > 0);
    CHECK(f.studio.views(f.surface.viewports()).size() == 1);
}

TEST_CASE("Studio: pulsanti del livello del mare", "[studio][interface][water]") {
    StudioFixture f;
    f.studio.document.open(ld::Experience::blank());
    f.studio.document.terrainSettings.waterLevel = 4;
    f.studio.document.terrainRegion = ld::TerrainRegion::snapped({-8, ld::Terrain::origin.y, -8}, {8, 4, 8});
    f.studio.document.regionCustom = true;
    f.draw();
    REQUIRE(f.surface.tap("command.terrain"));
    f.refresh();
    REQUIRE(f.surface.find("studio.dock"));
    REQUIRE(f.surface.find("dock.tab.terrain"));
    CHECK(f.studio.document.dockTab == "terrain");
    REQUIRE(f.surface.tap("terrain.tool.sea"));
    f.refresh();
    REQUIRE(f.surface.find("terrain.sea"));
    REQUIRE(f.studio.stage);
    CHECK(f.studio.stage->regionShown());
    REQUIRE(f.surface.tap("terrain.water.create"));
    f.refresh();
    CHECK(f.studio.document.world.terrain.water(0, 2, 0));
    CHECK_FALSE(f.studio.document.world.terrain.water(12, 2, 0));
    CHECK(f.studio.document.undoLabel() == std::optional<std::string>("Crea acqua"));
    REQUIRE(f.surface.tap("terrain.water.remove"));
    f.refresh();
    CHECK(f.studio.document.world.terrain.empty());
    f.studio.document.run(ld::StudioCommand::undo);
    CHECK(f.studio.document.world.terrain.water(0, 2, 0));
    ui::KeyInput escape;
    escape.down = true;
    escape.keyCode = 53;
    CHECK(f.studio.key(escape));
    f.refresh();
    CHECK_FALSE(f.studio.document.terrainMode);
    CHECK_FALSE(f.studio.stage->regionShown());
}

TEST_CASE("Studio: campi e maniglie della regione", "[studio][interface][water]") {
    StudioFixture f;
    f.studio.document.open(ld::Experience::blank());
    f.studio.document.terrainEditing = true;
    f.studio.document.dockTab = "terrain";
    f.studio.document.terrainTab = "region";
    f.studio.document.regionEditing = true;
    f.studio.document.terrainRegion = ld::TerrainRegion::snapped({-8, -16, -8}, {8, 0, 8});
    f.refresh();
    REQUIRE(f.studio.stage);
    CHECK(f.studio.stage->regionShown());
    auto* width = f.surface.field("terrain.region.size.x");
    REQUIRE(width);
    CHECK(width->text == "16,00");
    width->changed("8,4");
    width->commit("8,4");
    f.refresh();
    CHECK(f.studio.document.terrainRegion.maximum.x - f.studio.document.terrainRegion.minimum.x == 8);
    auto* top = f.surface.field("terrain.region.position.y");
    REQUIRE(top);
    top->changed("-7,9");
    top->commit("-7,9");
    f.refresh();
    CHECK(std::fmod(f.studio.document.terrainRegion.maximum.y * 4, 1) == 0);
    auto views = f.surface.viewports();
    REQUIRE(views.size() == 1);
    ui::Size size{views[0].frame.width, views[0].frame.height};
    f.studio.stage->size = size;
    f.studio.stage->refresh();
    std::optional<ui::Point> grip;
    for (double y = 0; y < size.height && !grip; y += 2)
        for (double x = 0; x < size.width && !grip; x += 2)
            if (auto handle = f.studio.stage->regionHandleAt({x, y}); handle && std::get<0>(*handle) == 0 && std::get<1>(*handle) == 0 && std::get<2>(*handle) == 1) grip = ui::Point{x, y};
    REQUIRE(grip);
    double before = f.studio.document.terrainRegion.maximum.x;
    ui::PointerEvent press;
    press.phase = ui::PointerPhase::down;
    press.location = *grip;
    CHECK(f.studio.stage->pointer(press, size));
    ui::PointerEvent drag = press;
    drag.phase = ui::PointerPhase::dragged;
    drag.location = {grip->x + 60, grip->y};
    f.studio.stage->pointer(drag, size);
    ui::PointerEvent release = drag;
    release.phase = ui::PointerPhase::up;
    f.studio.stage->pointer(release, size);
    double after = f.studio.document.terrainRegion.maximum.x;
    CHECK(after != before);
    CHECK(std::fmod(after - ld::Terrain::origin.x, ld::Terrain::cellSize) == 0);
    REQUIRE(f.surface.tap("terrain.material.sand"));
    REQUIRE(f.surface.tap("terrain.region.fill"));
    CHECK(f.studio.document.world.terrain.solid(f.studio.document.terrainRegion.minimum.x + 1, -15, f.studio.document.terrainRegion.minimum.z + 1));
    REQUIRE(f.surface.tap("terrain.region.clear"));
    CHECK(f.studio.document.world.terrain.empty());
}

TEST_CASE("Studio: proprietà dell'acqua annullabili", "[studio][interface][water]") {
    StudioFixture f;
    f.studio.document.open(ld::Experience::blank());
    f.studio.document.perform(ld::StudioEdit::sea(ld::TerrainRegion::snapped({-8, -16, -8}, {8, 0, 8})));
    f.studio.document.expandedServices.insert("Workspace");
    f.refresh();
    auto row = f.surface.find("explorer.terrain");
    REQUIRE(row);
    ui::Point middle{row->frame.x + row->frame.width / 2, row->frame.y + row->frame.height / 2};
    f.surface.mouseDown(middle);
    f.surface.mouseUp(middle);
    f.refresh();
    CHECK(f.studio.document.selection() == std::optional<ld::StudioSelection>(ld::StudioSelection::ofService("Terrain")));
    REQUIRE(f.surface.find("terrain.form"));
    auto* transparency = f.surface.field("terrain.water.transparency");
    REQUIRE(transparency);
    CHECK(transparency->text == "0,35");
    transparency->changed("0,8");
    transparency->commit("0,8");
    f.refresh();
    CHECK(f.studio.document.world.terrain.waterProperties.transparency == 0.8);
    REQUIRE(f.surface.tap("terrain.water.swatch.1F8A9E"));
    f.refresh();
    CHECK(f.studio.document.world.terrain.waterProperties.color == "1F8A9E");
    auto saved = ld::Terrain::fromJson(f.studio.document.world.terrain.json());
    REQUIRE(saved);
    CHECK(saved->waterProperties == f.studio.document.world.terrain.waterProperties);
    f.studio.document.run(ld::StudioCommand::undo);
    CHECK(f.studio.document.world.terrain.waterProperties == ld::TerrainWater{});
    f.studio.document.run(ld::StudioCommand::redo);
    CHECK(f.studio.document.world.terrain.waterProperties.color == "1F8A9E");
}

TEST_CASE("Studio: modifiche, script e prova condivisi", "[studio][interface]") {
    StudioFixture f;
    f.draw();
    REQUIRE(f.surface.tap("projects.template.0"));
    f.draw();
    REQUIRE(f.studio.document.opened);
    auto initial = f.studio.document.world.blocks.size();
    f.studio.document.addPart("cube");
    ui::KeyInput undo;
    undo.command = true;
    undo.characters = "z";
    REQUIRE(f.studio.key(undo));
    CHECK(f.studio.document.world.blocks.size() == initial);
    undo.shift = true;
    REQUIRE(f.studio.key(undo));
    CHECK(f.studio.document.world.blocks.size() == initial + 1);
    auto script = f.studio.document.insert("Script", "ServerScriptService");
    REQUIRE(script);
    f.studio.document.activeScript = *script;
    f.draw();
    auto field = f.surface.field("studio.script." + *script);
    REQUIRE(field);
    auto edit = field->changed;
    edit("print(\"Studio C++\")");
    CHECK(f.studio.document.world.record(*script)->source() == "print(\"Studio C++\")");
    ui::KeyInput play;
    play.keyCode = 96;
    REQUIRE(f.studio.key(play));
    CHECK(f.studio.document.playing);
    f.studio.tick(1, 1.0 / 60);
    play.shift = true;
    REQUIRE(f.studio.key(play));
    CHECK_FALSE(f.studio.document.playing);
    CHECK(f.studio.document.world.blocks.size() == initial + 1);
}

TEST_CASE("Studio: foglio di inserimento", "[studio][interface]") {
    StudioFixture f;
    f.studio.document.open(ld::Experience::blank());
    bool visible = false;
    f.studio.showSheet = [&](bool value) { visible = value; };
    f.studio.document.insertTarget = "ServerScriptService";
    f.draw();
    CHECK(visible);
    ui::Surface sheet([&] { return f.studio.insertSheet(); });
    auto draw = [&] {
        sheet.resize({360, 390}, 1);
        ui::Canvas canvas(360, 390, 1);
        sheet.render(canvas);
    };
    draw();
    REQUIRE(sheet.tap("insert.class.Script"));
    draw();
    REQUIRE(sheet.tap("insert.confirm"));
    f.draw();
    CHECK_FALSE(visible);
    CHECK_FALSE(f.studio.document.insertTarget);
    auto selected = f.studio.document.selection();
    REQUIRE(selected);
    REQUIRE(selected->instanceId());
    CHECK(f.studio.document.world.record(*selected->instanceId())->className == "Script");
}

TEST_CASE("Studio: completamento e rinomina Lim", "[studio][interface][tooling]") {
    StudioFixture f;
    REQUIRE(f.studio.document.open(ld::Experience::blank()));
    auto id = f.studio.document.insert("Script", "ServerScriptService");
    REQUIRE(id);
    f.studio.document.activeScript = *id;
    std::string source = "let valore=1\nprint(valore)\n";
    f.studio.document.setSource(*id, source);
    f.draw();
    auto field = f.surface.field("studio.script." + *id);
    REQUIRE(field);
    REQUIRE(field->complete);
    auto completions = field->complete("let valore = 1\nval", 18);
    CHECK(std::find(completions.begin(), completions.end(), "valore") != completions.end());
    REQUIRE(field->inspect);
    CHECK(field->inspect(source, source.rfind("valore")).find("valore") != std::string::npos);
    REQUIRE(field->commands.size() == 5);
    auto formatted = field->commands[0].run(source, 0, "");
    REQUIRE(formatted.text);
    CHECK(formatted.text->find("valore = 1") != std::string::npos);
    auto target = field->commands[1].run(source, source.rfind("valore"), "");
    REQUIRE(target.selection);
    CHECK(target.selection->first == source.find("valore"));
    auto references = field->commands[2].run(source, source.rfind("valore"), "");
    CHECK(references.message.find("Riga 1") != std::string::npos);
    CHECK(references.message.find("Riga 2") != std::string::npos);
    auto rename = field->commands[3].run(source, source.rfind("valore"), "numero");
    REQUIRE(rename.text);
    CHECK(rename.text->find("valore") == std::string::npos);
    CHECK(rename.text->find("print(numero)") != std::string::npos);
    auto invalid = field->commands[3].run(source, source.rfind("valore"), "fn");
    CHECK_FALSE(invalid.text);
    CHECK_FALSE(invalid.message.empty());
    auto edit = field->changed;
    REQUIRE(field->editBoundary);
    auto boundary = field->editBoundary;
    boundary();
    edit(*rename.text);
    boundary();
    CHECK(f.studio.document.world.record(*id)->source() == *rename.text);
    f.studio.document.undo();
    CHECK(f.studio.document.world.record(*id)->source() == source);
}

TEST_CASE("Studio: debugger di Lim", "[studio][tooling][debugger]") {
    StudioFixture f;
    REQUIRE(f.studio.document.open(ld::Experience::blank()));
    auto id = f.studio.document.insert("Script", "ServerScriptService");
    REQUIRE(id);
    f.studio.document.setSource(*id, "let numero = 7\nprint(numero)\nprint(\"fine\")\n");
    auto original = f.studio.document.world;
    f.studio.document.toggleBreakpoint(*id, 2);
    f.studio.document.play();
    REQUIRE(f.studio.document.debugPaused());
    REQUIRE_FALSE(f.studio.document.debugStops.begin()->second.frames.empty());
    auto stop = f.studio.document.debugStops.begin()->second;
    CHECK(stop.frames.front().line == 2);
    CHECK(std::any_of(stop.frames.front().locals.begin(), stop.frames.front().locals.end(), [](const auto& v) { return v.name == "numero" && v.value == "7"; }));
    CHECK(f.studio.document.activeScript == id);
    f.draw();
    REQUIRE(f.surface.find("studio.debug.over"));
    REQUIRE(f.surface.tap("studio.debug.over"));
    REQUIRE(f.studio.document.debugPaused());
    CHECK(f.studio.document.debugStops.begin()->second.frames.front().line == 3);
    f.studio.document.tick(100);
    CHECK(f.studio.document.debugPaused());
    f.studio.document.resumeDebug(lim::StepMode::run);
    CHECK_FALSE(f.studio.document.debugPaused());
    CHECK(std::any_of(f.studio.document.output.begin(), f.studio.document.output.end(), [](const auto& line) { return line.text == "fine"; }));
    f.studio.document.stop();
    CHECK(f.studio.document.world == original);
    CHECK_FALSE(f.studio.document.debugPaused());
}

TEST_CASE("Studio: trascinamento nell'Esplora", "[studio][interface][explorer]") {
    StudioFixture f;
    auto world = ld::Experience::blank();
    ld::WorldBlock holder, nested, mover, wall, frozen;
    holder.kind = "model"; holder.name = "Contenitore";
    nested.kind = "model"; nested.name = "Interno";
    mover.name = "Trascinato";
    wall.name = "Muro"; wall.x = 5;
    frozen.name = "Fermo"; frozen.locked = true; frozen.x = -5;
    for (auto* b : {&holder, &nested, &mover, &wall, &frozen}) b->id = ld::makeUuid();
    nested.parent = holder.id;
    for (auto* b : {&holder, &nested, &mover, &wall, &frozen}) world.blocks.push_back(*b);
    REQUIRE(f.studio.document.open(world));
    f.studio.document.expandedServices.insert("Workspace");
    f.studio.document.expandedIds.insert(holder.id);
    f.studio.document.select({mover.id});
    f.refresh();
    auto parentOf = [&](const std::string& id) { return f.studio.document.world.block(id)->parent; };
    auto point = [&](const std::string& id) {
        auto frame = f.studio.document.explorerFrames.find(id);
        INFO("riga Explorer " << id << " (" << f.studio.document.explorerFrames.size() << " righe)");
        REQUIRE(frame != f.studio.document.explorerFrames.end());
        return ui::Point{frame->second.maxX() - 30, frame->second.y + frame->second.h / 2};
    };
    auto drag = [&](const std::string& source, const std::string& target) {
        ui::Point start = point(source);
        ui::Point end = source == target ? ui::Point{start.x - 60, start.y} : point(target);
        f.surface.mouseDown(start);
        for (int step = 1; step <= 6; ++step) {
            double t = step / 6.0;
            f.surface.mouseDragged({start.x + (end.x - start.x) * t, start.y + (end.y - start.y) * t});
        }
        auto seen = f.studio.document.explorerDrag;
        f.surface.mouseUp(end);
        f.refresh();
        return seen;
    };
    CHECK_FALSE(f.studio.document.undoLabel());
    drag(mover.id, holder.id);
    CHECK(parentOf(mover.id) == holder.id);
    CHECK(f.studio.document.undoLabel());
    CHECK(f.studio.document.explorerFrames.count(mover.id));
    auto visible = f.studio.document.blocks();
    CHECK(std::any_of(visible.begin(), visible.end(), [&](const ld::WorldBlock& b) { return b.id == mover.id; }));
    f.studio.document.undo();
    CHECK(parentOf(mover.id) == "Workspace");
    f.studio.document.redo();
    CHECK(parentOf(mover.id) == holder.id);
    f.refresh();
    drag(mover.id, "ReplicatedStorage");
    CHECK(parentOf(mover.id) == "ReplicatedStorage");
    visible = f.studio.document.blocks();
    CHECK_FALSE(std::any_of(visible.begin(), visible.end(), [&](const ld::WorldBlock& b) { return b.id == mover.id; }));
    f.studio.document.undo();
    f.refresh();
    auto steady = f.studio.document.world;
    auto entries = f.studio.document.undoLabel();
    auto self = drag(holder.id, holder.id);
    REQUIRE(self);
    CHECK(self->target == std::optional<std::string>(holder.id));
    CHECK_FALSE(self->valid);
    drag(holder.id, nested.id);
    drag(mover.id, wall.id);
    drag(frozen.id, holder.id);
    CHECK(f.studio.document.world == steady);
    CHECK(f.studio.document.undoLabel() == entries);
    ui::Point rename = point(wall.id);
    f.surface.mouseDown(rename);
    f.surface.mouseUp(rename);
    f.surface.mouseDown(rename, 2);
    f.surface.mouseUp(rename);
    CHECK(f.studio.document.renaming == std::optional<std::string>(wall.id));
    CHECK(f.studio.document.selection() == std::optional<ld::StudioSelection>(ld::StudioSelection::instance(wall.id)));
}

TEST_CASE("Studio: pannelli e viewport", "[studio][interface][layout]") {
    StudioFixture f;
    REQUIRE(f.studio.document.open(ld::Experience::blank()));
    for (auto [width, height] : std::vector<std::pair<double, double>>{{1400, 820}, {1180, 760}, {920, 700}, {860, 680}, {760, 640}}) {
        INFO(width << "x" << height);
        f.studio.document.dockWidth = 10000;
        f.studio.document.propertiesWidth = 10000;
        f.studio.document.utilityHeight = 10000;
        f.surface.invalidate();
        f.surface.resize({width, height}, 1);
        ui::Canvas canvas(static_cast<int>(width), static_cast<int>(height), 1);
        f.surface.render(canvas);
        auto views = f.surface.viewports();
        REQUIRE(views.size() == 1);
        CHECK(views[0].frame.width >= 280 - 1);
        CHECK(views[0].frame.height >= 100);
        auto explorer = f.studio.document.explorerFrames;
        for (const auto& [id, frame] : explorer) CHECK(frame.x >= views[0].frame.maxX() - 1);
        auto dock = f.surface.find("studio.dock");
        REQUIRE(dock);
        CHECK(dock->frame.maxX() <= views[0].frame.x + 1);
    }
    f.studio.document.panels.erase("properties");
    f.studio.document.panels.erase("explorer");
    f.studio.document.panels.erase("toolbox");
    f.surface.invalidate();
    f.surface.resize({920, 700}, 1);
    ui::Canvas canvas(920, 700, 1);
    f.surface.render(canvas);
    auto wide = f.surface.viewports();
    REQUIRE(wide.size() == 1);
    CHECK(wide[0].frame.maxX() >= 920 - 1);
}

TEST_CASE("Studio: pannello Prestazioni script", "[studio][interface][performance]") {
    StudioFixture f;
    REQUIRE(f.studio.document.open(ld::Experience::blank()));
    CHECK_FALSE(f.studio.document.panels.count("performance"));
    f.refresh();
    CHECK_FALSE(f.surface.matching("studio.performance").size());
    f.studio.togglePanel("performance");
    f.refresh();
    auto views = f.surface.viewports();
    REQUIRE(views.size() == 1);
    CHECK(views[0].frame.height >= 100);
    f.studio.document.playMode = ld::StudioPlayMode::camera;
    auto script = f.studio.document.insert("Script", std::string("ServerScriptService"));
    REQUIRE(script);
    f.studio.document.setSource(*script, "for i in 1...3 {\n    await Task.nextFrame()\n}\n");
    f.studio.document.play();
    for (int i = 1; i <= 10; ++i) f.studio.document.tick(i / 60.0);
    f.refresh();
    CHECK_FALSE(f.studio.document.scriptPerformance().scripts.empty());
    f.studio.document.stop();
    f.studio.document.activeScript.reset();
    for (auto [width, height] : std::vector<std::pair<double, double>>{{1180, 760}, {760, 640}}) {
        f.surface.invalidate();
        f.surface.resize({width, height}, 1);
        ui::Canvas canvas(static_cast<int>(width), static_cast<int>(height), 1);
        f.surface.render(canvas);
        auto slots = f.surface.viewports();
        INFO(width << "x" << height);
        REQUIRE(slots.size() == 1);
        CHECK(slots[0].frame.width >= 279);
        CHECK(slots[0].frame.height >= 140);
    }
}

TEST_CASE("Studio: browser delle API Lim", "[studio][interface][api-reference]") {
    StudioFixture f;
    REQUIRE(f.studio.document.open(ld::Experience::blank()));
    CHECK_FALSE(f.studio.document.panels.count("api"));
    f.refresh();
    CHECK_FALSE(f.surface.field("studio.api.search"));
    f.studio.togglePanel("api");
    f.refresh();
    REQUIRE(f.surface.field("studio.api.search"));
    CHECK(f.surface.contains("API Lim"));
    auto labelled = [&](const std::string& prefix) {
        return std::any_of(f.surface.elements().begin(), f.surface.elements().end(), [&](const auto& e) { return e.label.rfind(prefix, 0) == 0; });
    };
    f.studio.document.apiQuery = "send";
    f.refresh();
    CHECK(labelled("fn send("));
    CHECK(labelled("metodo di RemoteEvent"));
    f.studio.document.apiQuery = "fireServer";
    f.refresh();
    CHECK(f.surface.contains("metodo di RemoteEvent · client · deprecato, usa send"));
    f.studio.document.apiQuery = "Input";
    f.refresh();
    CHECK(f.surface.contains("globale · client"));
    f.studio.document.apiQuery = "zzz-nessuna-api";
    f.refresh();
    CHECK(f.surface.contains("Nessuna API corrisponde a «zzz-nessuna-api»."));
    for (auto [width, height] : std::vector<std::pair<double, double>>{{1180, 760}, {760, 640}}) {
        f.surface.invalidate();
        f.surface.resize({width, height}, 1);
        ui::Canvas canvas(static_cast<int>(width), static_cast<int>(height), 1);
        f.surface.render(canvas);
        auto slots = f.surface.viewports();
        INFO(width << "x" << height);
        REQUIRE(slots.size() == 1);
        CHECK(slots[0].frame.width >= 279);
        CHECK(slots[0].frame.height >= 140);
    }
}

TEST_CASE("Studio: disposizione dei pannelli", "[studio][interface][layout][v1]") {
    ld::local_preferences::useMemory(true);
    {
        StudioFixture f;
        REQUIRE(f.studio.document.open(ld::Experience::blank()));
        f.surface.resize({1440, 900}, 1);
        ui::Canvas canvas(1440, 900, 1);
        f.surface.render(canvas);
        auto views = f.surface.viewports();
        REQUIRE(views.size() == 1);
        auto utility = f.surface.find("studio.utility");
        REQUIRE(f.surface.find("explorer.actions"));
        REQUIRE(utility);
        double explorerShare = views[0].frame.x / 1440.0, centerShare = views[0].frame.width / 1440.0;
        auto explorerPanel = f.surface.find("explorer.actions");
        CHECK(explorerPanel->frame.x > views[0].frame.maxX());
        CHECK(f.surface.find("dock.tab.toolbox"));
        INFO("Casella strumenti " << explorerShare * 100 << "%, centro " << centerShare * 100 << "% x " << views[0].frame.height / 900 * 100 << "%, utility " << utility->frame.height / 900 * 100 << "%");
        CHECK(f.studio.document.utilityCollapsed);
        CHECK(explorerShare > 0.15);
        CHECK(explorerShare < 0.19);
        CHECK(centerShare > 0.57);
        CHECK(centerShare < 0.64);
        CHECK(views[0].frame.height / 900 > 0.75);
        CHECK(utility->frame.height / 900 < 0.05);
        CHECK_FALSE(f.surface.find("output.clear"));
        CHECK_FALSE(f.surface.find("utility.tab.toolbox"));
        CHECK(f.surface.find("utility.tab.output"));
        REQUIRE(f.surface.tap("utility.tab.output"));
        f.surface.invalidate();
        f.surface.render(canvas);
        utility = f.surface.find("studio.utility");
        REQUIRE(utility);
        CHECK(f.studio.document.utilityTab == "output");
        CHECK_FALSE(f.studio.document.utilityCollapsed);
        CHECK(f.surface.find("output.clear"));
        CHECK(utility->frame.height / 900 > 0.18);
        CHECK(utility->frame.height / 900 < 0.26);
        CHECK(utility->frame.y > f.surface.viewports()[0].frame.maxY());
        REQUIRE(f.surface.tap("utility.collapse"));
        f.surface.invalidate();
        f.surface.render(canvas);
        CHECK(f.studio.document.utilityCollapsed);
        CHECK_FALSE(f.surface.find("output.clear"));
        REQUIRE(f.surface.tap("utility.tab.output"));
        f.surface.invalidate();
        f.surface.render(canvas);
        CHECK_FALSE(f.studio.document.utilityCollapsed);
        f.studio.document.dockWidth = 260;
        f.studio.document.propertiesWidth = 320;
        f.studio.tick(10, 1.0 / 60);
        f.studio.tick(12, 1.0 / 60);
    }
    {
        StudioFixture f;
        CHECK(f.studio.document.dockWidth == 260);
        CHECK(f.studio.document.propertiesWidth == 320);
        CHECK_FALSE(f.studio.document.utilityCollapsed);
        CHECK(f.studio.document.utilityTab == "output");
        f.studio.togglePanel("explorer");
        CHECK_FALSE(f.studio.document.panels.count("explorer"));
        f.studio.resetLayout();
        CHECK(f.studio.document.dockWidth == 250);
        CHECK(f.studio.document.propertiesWidth == 300);
        CHECK(f.studio.document.panels.count("explorer"));
        CHECK(f.studio.document.utilityCollapsed);
        REQUIRE(f.studio.document.open(ld::Experience::blank()));
        for (auto [width, height] : std::vector<std::pair<double, double>>{{860, 680}, {760, 520}, {1920, 1080}}) {
            INFO(width << "x" << height);
            f.studio.document.dockWidth = 5000;
            f.studio.document.propertiesWidth = 5000;
            f.studio.document.utilityHeight = 5000;
            f.studio.document.utilityCollapsed = false;
            f.surface.invalidate();
            f.surface.resize({width, height}, 1);
            ui::Canvas small(static_cast<int>(width), static_cast<int>(height), 1);
            f.surface.render(small);
            auto views = f.surface.viewports();
            REQUIRE(views.size() == 1);
            CHECK(views[0].frame.width >= 340);
            CHECK(views[0].frame.height >= 150);
        }
    }
    ld::local_preferences::useMemory(true);
}

TEST_CASE("Studio: barra multifunzione da 860 a 1400", "[studio][interface][commandbar][responsive][v1]") {
    StudioFixture f;
    REQUIRE(f.studio.document.open(ld::Experience::blank()));
    f.draw();
    for (double width : {860.0, 920.0, 1180.0, 1400.0}) {
        INFO(width);
        REQUIRE(f.surface.tap("ribbon.tab.home"));
        f.surface.invalidate();
        f.surface.resize({width, 760}, 1);
        ui::Canvas canvas(static_cast<int>(width), 760, 1);
        f.surface.render(canvas);
        auto bar = f.surface.find("studio.commandBar");
        auto body = f.surface.find("ribbon.body");
        REQUIRE(bar);
        REQUIRE(body);
        CHECK(bar->frame.height == ui::theme::metrics::ribbonTabs + ui::theme::metrics::ribbonBody);
        CHECK(body->frame.height == ui::theme::metrics::ribbonBody);
        for (const char* tab : {"home", "model", "test", "view"}) {
            auto item = f.surface.find(std::string("ribbon.tab.") + tab);
            REQUIRE(item);
            CHECK(item->frame.height == ui::theme::metrics::segmented);
            CHECK(item->frame.maxY() <= body->frame.y);
        }
        double previous = -1;
        for (const std::string id : {"command.paste", "command.tool.select", "command.tool.move", "command.tool.scale", "command.tool.rotate", "command.terrain", "command.insert", "command.toolbox", "command.play", "command.stop"}) {
            INFO(id);
            auto element = f.surface.find(id);
            REQUIRE(element);
            CHECK(element->frame.x >= 0);
            CHECK(element->frame.maxX() <= width);
            CHECK(element->frame.y >= body->frame.y);
            CHECK(element->frame.maxY() <= body->frame.maxY());
            CHECK(element->frame.x > previous);
            previous = element->frame.x;
        }
        for (const char* group : {"clipboard", "tools", "terrain", "insert", "edit", "test"}) REQUIRE(f.surface.find(std::string("ribbon.group.") + group));
        CHECK(f.surface.find("ribbon.group.test")->frame.maxX() <= width);
        CHECK(f.surface.find("studio.publish")->frame.maxY() <= body->frame.y);
        CHECK(f.surface.find("studio.save")->frame.maxX() <= width);
        auto more = f.surface.menu("command.more");
        REQUIRE(more);
        bool closes = false, settings = false, shortcuts = false;
        for (const auto& item : *more) {
            closes = closes || item.title == "Chiudi progetto";
            settings = settings || item.title == "Impostazioni esperienza…";
            shortcuts = shortcuts || item.title == "Scorciatoie da tastiera";
        }
        CHECK(closes);
        CHECK(settings);
        CHECK(shortcuts);
        REQUIRE(f.surface.tap("ribbon.tab.model"));
        f.surface.render(canvas);
        REQUIRE(f.surface.field("command.snap.position"));
        REQUIRE(f.surface.find("command.space.1"));
        for (const char* id : {"command.part.cube", "command.import", "command.group"}) {
            INFO(id);
            auto element = f.surface.find(id);
            REQUIRE(element);
            CHECK(element->frame.maxX() <= width);
        }
        REQUIRE(f.surface.tap("ribbon.tab.test"));
        f.surface.render(canvas);
        for (const char* id : {"command.play", "command.playHere", "command.run", "command.stop"}) REQUIRE(f.surface.find(id));
        REQUIRE(f.surface.tap("ribbon.tab.view"));
        f.surface.render(canvas);
        for (const char* id : {"command.panel.explorer", "command.panel.properties", "command.panel.toolbox", "command.panel.output", "command.panel.performance", "command.panel.api", "command.grid"}) REQUIRE(f.surface.find(id));
    }
    REQUIRE(f.surface.tap("command.panel.explorer"));
    f.refresh();
    CHECK_FALSE(f.studio.document.panels.count("explorer"));
    REQUIRE(f.surface.tap("command.panel.explorer"));
    f.refresh();
    CHECK(f.studio.document.panels.count("explorer"));
    REQUIRE(f.surface.tap("command.grid"));
    CHECK_FALSE(f.studio.document.showGrid);
    REQUIRE(f.surface.tap("ribbon.tab.model"));
    f.refresh();
    REQUIRE(f.surface.tap("command.space.1"));
    CHECK(f.studio.document.moveSpace == ld::TransformSpace::local);
    CHECK(f.surface.contains("Globale"));
    CHECK_FALSE(f.surface.contains("Mondo"));
    std::size_t parts = f.studio.document.world.blocks.size();
    REQUIRE(f.surface.tap("command.part.sphere"));
    CHECK(f.studio.document.world.blocks.size() == parts + 1);
}

TEST_CASE("Studio: barra comandi, prova e terreno", "[studio][interface][commandbar][v1]") {
    StudioFixture f;
    REQUIRE(f.studio.document.open(ld::Experience::blank()));
    f.draw();
    REQUIRE(f.surface.find("ribbon.tab.home"));
    CHECK_FALSE(f.surface.find("command.stop")->enabled);
    CHECK(f.surface.find("command.stop")->label == "Ferma");
    CHECK(f.surface.find("command.play")->label == "Gioca");
    std::size_t parts = f.studio.document.world.blocks.size();
    REQUIRE(f.surface.tap("command.insert"));
    f.refresh();
    REQUIRE(f.surface.find("command.insert.menu"));
    REQUIRE(f.surface.tap("insert.cube"));
    f.refresh();
    CHECK(f.studio.document.world.blocks.size() == parts + 1);
    CHECK_FALSE(f.surface.find("command.insert.menu"));
    REQUIRE(f.surface.tap("command.tool.scale"));
    CHECK(f.studio.document.tool == ld::StudioTool::scale);
    CHECK_FALSE(f.surface.find("terrain.settings"));
    REQUIRE(f.surface.tap("command.terrain"));
    f.refresh();
    REQUIRE(f.surface.find("terrain.settings"));
    CHECK_FALSE(f.surface.find("terrain.brush"));
    REQUIRE(f.surface.tap("terrain.tool.raise"));
    CHECK(f.studio.document.terrainTool == std::optional<ld::TerrainTool>(ld::TerrainTool::raise));
    f.refresh();
    CHECK(f.surface.contains("Rigonfia: alza il terreno dolcemente."));
    REQUIRE(f.surface.find("terrain.brush"));
    auto viewport = f.surface.viewports();
    auto settings = f.surface.find("terrain.settings");
    REQUIRE(viewport.size() == 1);
    REQUIRE(settings);
    CHECK(settings->frame.maxX() <= viewport[0].frame.x + 1);
    REQUIRE(f.surface.tap("terrain.tool.generate"));
    f.refresh();
    CHECK_FALSE(f.studio.document.terrainTool);
    REQUIRE(f.surface.find("terrain.generation"));
    REQUIRE(f.surface.tap("dock.tab.toolbox"));
    f.refresh();
    CHECK_FALSE(f.surface.find("terrain.settings"));
    REQUIRE(f.surface.tap("dock.tab.terrain"));
    f.refresh();
    CHECK(f.surface.find("terrain.settings"));
    REQUIRE(f.surface.tap("command.tool.move"));
    f.refresh();
    CHECK_FALSE(f.studio.document.terrainEditing);
    CHECK_FALSE(f.studio.document.terrainTool);
    CHECK_FALSE(f.surface.find("terrain.settings"));
    auto menu = f.surface.menu("command.play.menu");
    REQUIRE(menu);
    std::vector<std::string> titles;
    for (const auto& item : *menu)
        if (!item.separator) titles.push_back(item.title);
    CHECK(titles == std::vector<std::string>{"Gioca (F5)", "Gioca da qui", "Esegui (F8)"});
    menu->back().action();
    f.refresh();
    CHECK(f.studio.document.playing);
    CHECK(f.studio.document.playMode == ld::StudioPlayMode::camera);
    CHECK(f.surface.find("command.playing"));
    CHECK(f.surface.find("command.stop")->enabled);
    CHECK_FALSE(f.surface.find("command.play")->enabled);
    CHECK(f.surface.contains("Script in esecuzione"));
    REQUIRE(f.surface.tap("command.stop"));
    f.refresh();
    CHECK_FALSE(f.studio.document.playing);
    CHECK_FALSE(f.surface.find("command.playing"));
    CHECK_FALSE(f.surface.find("command.stop")->enabled);
    CHECK(f.surface.contains("Modifica"));
}

TEST_CASE("Studio: conteggi dell'Output su una riga", "[studio][interface][output][v1]") {
    StudioFixture f;
    REQUIRE(f.studio.document.open(ld::Experience::blank()));
    f.studio.document.utilityCollapsed = false;
    f.studio.document.utilityTab = "output";
    for (auto [text, kind] : {std::pair{"Attenzione: Part senza Anchored", ld::OutputLine::Kind::warning}, std::pair{"Errore: riga 3, valore nil", ld::OutputLine::Kind::error}}) {
        ld::OutputLine line;
        line.text = text;
        line.kind = kind;
        f.studio.document.output.push_back(line);
    }
    for (double width : {860.0, 1180.0, 1400.0}) {
        INFO(width);
        f.surface.invalidate();
        f.surface.resize({width, 760}, 1);
        ui::Canvas canvas(static_cast<int>(width), 760, 1);
        f.surface.render(canvas);
        auto counts = f.surface.find("output.counts");
        REQUIRE(counts);
        CHECK(counts->frame.height <= 20);
        CHECK(counts->frame.x + counts->frame.width <= width);
    }
}

TEST_CASE("Studio: titolo della finestra", "[studio][interface][title]") {
    StudioFixture f;
    std::string title;
    f.studio.setTitle = [&](const std::string& value) { title = value; };
    f.draw();
    CHECK(title == "Ludissey Studio");
    auto world = ld::Experience::blank();
    world.title = "Isola Tranquilla";
    REQUIRE(f.studio.document.open(world));
    f.draw();
    CHECK(title == "Ludissey Studio: Isola Tranquilla");
    f.studio.document.configure("Torre dei Venti", std::nullopt, std::nullopt);
    f.draw();
    CHECK(title == "Ludissey Studio: Torre dei Venti");
    f.studio.document.close();
    f.draw();
    CHECK(title == "Ludissey Studio");
}

TEST_CASE("Studio: progetti rimasti su questo Mac", "[studio][interface][mac]") {
    StudioFixture f;
    auto root = std::filesystem::temp_directory_path() / ("Ludissey Mac " + ld::makeUuid());
    auto archive = root / "Server locale";
    ld::asset_cache::setFolder(root / "Cache");
    ld::mac_archive::setFolder(archive);
    f.store->profile = ld::PlayerProfile{"Creatore", "female", "2026-09-25T10:00:00Z"};
    f.draw();
    REQUIRE(f.surface.tap("projects.tabs.1"));
    f.refresh();
    CHECK_FALSE(f.surface.find("mac.group"));
    auto server = std::make_shared<ld::LocalServer>(archive);
    server->enrollNow(ld::PlayerId("Pmac"), ld::PlayerProfile{"Creatore", "female", "2026-09-25T10:00:00Z"});
    server->enrollNow(ld::PlayerId("Paltro"), ld::PlayerProfile{"Altro", "male", "2026-09-25T10:00:00Z"});
    auto world = ld::Experience::blank();
    world.title = "Porto del Mac";
    harness::await(ld::StudioSync(std::make_shared<ld::LocalBackend>(server, ld::PlayerId("Pmac"))).save(world, true));
    auto other = ld::Experience::blank();
    other.title = "Di qualcun altro";
    harness::await(ld::StudioSync(std::make_shared<ld::LocalBackend>(server, ld::PlayerId("Paltro"))).save(other, false));
    std::string picture = "immagine di prova";
    auto asset = ld::asset_cache::identifier(picture, "jpg");
    ld::asset_cache::writeFile(archive / "Risorse/ePmac" / asset, picture);
    REQUIRE(f.surface.tap("projects.tabs.0"));
    f.refresh();
    REQUIRE(f.surface.tap("projects.tabs.1"));
    f.refresh();
    auto shoot = [&](const std::string& name) {
        const char* shots = std::getenv("LUDISSEY_SHOTS");
        if (!shots) return;
        f.surface.resize({1180, 760}, 2);
        ui::Canvas canvas(1180, 760, 2);
        f.surface.render(canvas);
        std::filesystem::create_directories(shots);
        canvas.writePng(std::filesystem::path(shots) / ("studio " + name + ".png"));
    };
    REQUIRE(f.surface.find("mac.group"));
    shoot("su questo Mac");
    CHECK(f.surface.contains("Su questo Mac"));
    CHECK(f.surface.find("mac.project." + world.id));
    CHECK_FALSE(f.surface.contains("Di qualcun altro"));
    REQUIRE(f.surface.find("mac.upload." + world.id));
    CHECK(f.surface.find("mac.upload." + world.id)->label == "Carica sul tuo account");
    REQUIRE(f.surface.tap("mac.upload." + world.id));
    REQUIRE(harness::waitUntil([&] { return !f.store->busy; }, 10));
    CHECK_FALSE(f.store->error);
    REQUIRE(f.store->worlds().size() == 1);
    CHECK(f.store->worlds().front().id == world.id);
    CHECK(f.store->worlds().front().title == "Porto del Mac");
    CHECK_FALSE(f.store->discover().size());
    CHECK(ld::asset_cache::verified(asset));
    f.refresh();
    CHECK(f.surface.contains("Caricata"));
    shoot("su questo Mac caricata");
    CHECK_FALSE(f.surface.find("mac.upload." + world.id));
    REQUIRE(f.surface.tap("mac.remove." + world.id));
    REQUIRE(f.store->dialog);
    CHECK(f.store->dialog->title == "Eliminare «Porto del Mac» dal Mac?");
    f.store->dialog->buttons.back().action();
    f.store->dialog.reset();
    f.refresh();
    CHECK_FALSE(f.surface.find("mac.group"));
    CHECK(ld::mac_archive::projects("Creatore").empty());
    CHECK(ld::mac_archive::projects("Altro").size() == 1);
    CHECK(f.store->worlds().size() == 1);
    ld::mac_archive::setFolder(std::nullopt);
    ld::asset_cache::setFolder(std::nullopt);
    std::filesystem::remove_all(root);
}

TEST_CASE("Studio: mano e hover solo dove si clicca", "[studio][interface][cursor]") {
    StudioFixture f;
    f.store->profile = ld::PlayerProfile{"Creatore", "female", "2026-09-25T10:00:00Z"};
    auto check = [&](const std::string& page) {
        f.refresh();
        INFO(page);
        CHECK(audit::cursorProblems(f.surface) == std::vector<std::string>{});
    };
    check("progetti");
    REQUIRE(f.surface.tap("projects.tabs.1"));
    check("i miei progetti");
    REQUIRE(f.studio.document.open(ld::Experience::blank()));
    check("editor");
    f.studio.document.addPart("cube");
    check("parte selezionata");
    REQUIRE(f.surface.tap("command.insert"));
    check("menu Inserisci");
    REQUIRE(f.surface.tap("insert.cube"));
    REQUIRE(f.surface.tap("command.terrain"));
    check("terreno");
    f.studio.document.log("Attenzione: prova", false);
    f.studio.document.panels.insert("output");
    f.studio.document.utilityTab = "output";
    f.studio.document.utilityCollapsed = false;
    check("Output");
    f.studio.openSettings();
    check("impostazioni");
}
