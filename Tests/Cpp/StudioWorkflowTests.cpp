#include "Engine/Backend/Assets.hpp"
#include "Engine/Core/Png.hpp"
#include "Engine/Core/Rotation.hpp"
#include "Engine/Rendering/World.hpp"
#include "Engine/Studio/StudioDocument.hpp"
#include "Golden.hpp"
#include "Studio/StudioStage.hpp"
#include "Tests/Support/Sanitizers.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

using namespace ld;

namespace {

Experience project() {
    Experience e;
    e.id = makeUuid();
    e.title = "Torre dei Venti";
    return e;
}

const WorldBlock& at(const StudioDocument& d, const std::string& id) {
    for (const auto& b : d.world.blocks)
        if (b.id == id) return b;
    FAIL("blocco assente " << id);
    return d.world.blocks.front();
}

bool facing(const WorldBlock& block, double degrees) {
    Mat3 expected = rotation::axis(1, degrees), actual = block.rotationMatrix();
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            if (std::abs(actual[r][c] - expected[r][c]) > 1e-6) return false;
    return true;
}

double gap(const WorldBlock& a, const WorldBlock& b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z)); }

bool printed(const StudioDocument& d, const std::string& text, bool error = false) {
    return std::any_of(d.output.begin(), d.output.end(), [&](const OutputLine& l) { return l.text.find(text) != std::string::npos && (!error || l.error()); });
}

void advance(StudioDocument& d, double& clock, double seconds) {
    for (double t = 0; t < seconds; t += 1.0 / 60) d.tick(clock += 1.0 / 60);
}

}

TEST_CASE("Studio: inserimento e nomi univoci", "[studio][workflow]") {
    golden::registry();
    StudioDocument document;
    REQUIRE(document.open(project()));
    REQUIRE(document.world.blocks.empty());
    for (const char* kind : {"cube", "sphere", "cylinder", "cube"}) document.addPart(kind);
    REQUIRE(document.world.blocks.size() == 4);
    std::vector<std::string> names;
    for (const auto& b : document.world.blocks) names.push_back(b.displayName());
    CHECK(names == std::vector<std::string>{"Cubo", "Sfera", "Cilindro", "Cubo"});
    CHECK(document.selection() == std::optional<StudioSelection>(StudioSelection::instance(document.world.blocks[3].id)));
    document.duplicateSelection();
    REQUIRE(document.world.blocks.size() == 5);
    CHECK(document.world.blocks[4].displayName() == "Cubo");
    document.deleteSelection();
    CHECK(document.world.blocks.size() == 4);

    std::string first = document.world.blocks[0].id;
    document.update(first, [](WorldBlock& b) { b.x = 900; b.y = -500; b.setSize(9999); });
    CHECK(document.world.blocks[0].x == studio_limits::horizontal);
    CHECK(document.world.blocks[0].y == -studio_limits::vertical);
    CHECK(document.world.blocks[0].size() == studio_limits::maximumSize);
    document.update(first, [](WorldBlock& b) { b.setSize(0.001); });
    CHECK(document.world.blocks[0].size() == studio_limits::minimumSize);
    document.update(first, [](WorldBlock& b) { b.x = 0; b.y = 0.5; b.setSize(1); });
    auto saved = document.world.blocks;
    while (static_cast<int>(document.world.blocks.size()) < studio_limits::parts) {
        WorldBlock extra;
        extra.id = makeUuid();
        document.world.blocks.push_back(extra);
    }
    CHECK_FALSE(document.addPart("cube"));
    CHECK(static_cast<int>(document.world.blocks.size()) == studio_limits::parts);
    document.world.blocks.assign(saved.begin(), saved.begin() + 3);

    document.select({first});
    REQUIRE(document.addScript(first));
    auto script = document.world.instances.back().id;
    CHECK(document.world.instances.back().className == "Script");
    CHECK(document.world.parentOf(script) == std::optional<std::string>(first));
    CHECK(document.openScripts == std::vector<std::string>{script});
    CHECK(document.activeScript == std::optional<std::string>(script));
    document.closeScript(script);
    CHECK(document.openScripts.empty());
    CHECK_FALSE(document.activeScript);
    document.openScript(script);
    auto kept = document.world.instances;
    document.world.instances.clear();
    CHECK_FALSE(document.openScripts.empty());
    CHECK(document.visibleScripts().empty());
    document.world.instances = kept;
}

TEST_CASE("Studio: prova con camera e personaggio", "[studio][workflow][play]") {
    golden::registry();
    StudioDocument document;
    REQUIRE(document.open(project()));
    for (const char* kind : {"cube", "sphere", "cylinder"}) document.addPart(kind);
    std::string first = document.world.blocks[0].id;
    auto script = document.insert("Script", first);
    REQUIRE(script);
    document.setSource(*script, "let part = script.parent\nif part is BasePart {\n    part.position = Vec3(4, part.position.y, part.position.z)\n    print(\"posizione\", part.position.x)\n}\nawait Task.wait(0.05)\nprint(\"secondo passo\")\n");
    auto broken = document.insert("Script", document.world.blocks[1].id);
    REQUIRE(broken);
    document.rename(*broken, "Rotto");
    document.setSource(*broken, "let x = ");
    REQUIRE(document.scriptErrors().count(*broken));
    CHECK(document.scriptErrors().at(*broken).line == 1);
    CHECK_FALSE(document.scriptErrors().count(*script));
    document.activeScript.reset();
    document.world.blocks[2].anchored = false;
    document.world.blocks[2].y = 8;
    document.world.blocks.push_back(Experience::baseplate(document.world.id));
    Experience before = document.world;
    WorldBlock faller = document.world.blocks[2];

    double clock = 0;
    document.playMode = StudioPlayMode::camera;
    document.play();
    REQUIRE(document.playing);
    CHECK(document.playMode == StudioPlayMode::camera);
    advance(document, clock, 3);
    CHECK(printed(document, "posizione 4"));
    CHECK(printed(document, "secondo passo"));
    CHECK(printed(document, "Workspace.Sfera.Rotto", true));
    auto parts = document.blocks();
    auto found = std::find_if(parts.begin(), parts.end(), [&](const WorldBlock& b) { return b.id == first; });
    REQUIRE(found != parts.end());
    CHECK(found->x == 4);
    auto resting = std::find_if(parts.begin(), parts.end(), [&](const WorldBlock& b) { return b.id == faller.id; });
    REQUIRE(resting != parts.end());
    CHECK(std::abs(resting->y - faller.sizeY / 2) < 0.01);
    document.stop();
    CHECK_FALSE(document.playing);
    CHECK(document.world == before);

    document.playMode = StudioPlayMode::character;
    document.play();
    advance(document, clock, 0.4);
    REQUIRE(document.session());
    REQUIRE(document.session()->character());
    CHECK(document.session()->character()->child("Humanoid"));
    document.stop();
    CHECK(document.world == before);

    std::erase_if(document.world.blocks, [](const WorldBlock& b) { return b.name == rules::baseplate::name; });
    document.playMode = StudioPlayMode::camera;
    document.play();
    advance(document, clock, 4);
    parts = document.blocks();
    CHECK_FALSE(std::any_of(parts.begin(), parts.end(), [&](const WorldBlock& b) { return b.id == faller.id; }));
    CHECK(printed(document, "stato rimosso"));
    document.stop();

    auto encoded = document.world.json().dump();
    auto parsed = Experience::parse(encoded);
    REQUIRE(parsed);
    REQUIRE(document.open(*parsed));
    CHECK(document.world.instances.size() == 2);
    CHECK(document.world.instances.front().source().find("secondo passo") != std::string::npos);
    CHECK(document.world.blocks[0].displayName() == "Cubo");
    CHECK_FALSE(document.world.blocks[2].anchored);
    CHECK_FALSE(document.dirty());
}

TEST_CASE("Studio: Annulla e Ripeti delle trasformazioni", "[studio][workflow][undo]") {
    golden::registry();
    StudioDocument document;
    REQUIRE(document.open(project()));
    for (const char* kind : {"cube", "sphere"}) document.addPart(kind);
    auto count = document.world.blocks.size();
    document.addPart("cube");
    auto inserted = document.world.blocks.size();
    document.undo();
    CHECK(document.world.blocks.size() == count);
    document.redo();
    CHECK(document.world.blocks.size() == inserted);
    document.undo();
    std::string target = document.world.blocks[0].id;
    document.select({target});
    double originalX = document.world.blocks[0].x;
    for (double value : {1.0, 2.0, 3.0}) document.update(target, [&](WorldBlock& b) { b.x = value; }, std::string("Sposta"), std::string("position:0:") + target);
    document.endGesture();
    document.undo();
    CHECK(document.world.blocks[0].x == originalX);
    document.update(target, [](WorldBlock& b) { b.rotationY = 90; }, std::string("Ruota"));
    document.update(target, [](WorldBlock& b) { b.sizeZ = 4; }, std::string("Scala"));
    CHECK(document.world.blocks[0].rotationY == 90);
    CHECK(document.world.blocks[0].sizeZ == 4);
    document.undo();
    document.undo();
    CHECK(document.world.blocks[0].rotationY == 0);
    CHECK(document.world.blocks[0].sizeZ == 1);
    document.redo();
    document.redo();
    CHECK(document.world.blocks[0].rotationY == 90);
    CHECK(document.world.blocks[0].sizeZ == 4);
}

TEST_CASE("Studio: multiselezione e appunti", "[studio][workflow][model]") {
    golden::registry();
    StudioDocument document;
    REQUIRE(document.open(project()));
    for (const char* kind : {"cube", "sphere", "cylinder"}) document.addPart(kind);
    Experience baseline = document.world;
    std::string cube = document.world.blocks[0].id, sphere = document.world.blocks[1].id;
    document.select({cube});
    document.extendSelection(sphere);
    CHECK(document.selectedIds() == std::vector<std::string>{cube, sphere});
    CHECK(document.selection() == std::optional<StudioSelection>(StudioSelection::instance(sphere)));
    document.extendSelection(sphere);
    CHECK(document.selectedIds() == std::vector<std::string>{cube});
    document.extendSelection(sphere);
    CHECK(document.can(StudioCommand::group));
    CHECK_FALSE(document.can(StudioCommand::ungroup));
    document.run(StudioCommand::group);
    auto model = document.selectedPart();
    REQUIRE(model);
    REQUIRE(model->kind == "model");
    CHECK(document.world.childIds(model->id) == std::vector<std::string>{cube, sphere});
    auto parts = document.world.workspaceParts();
    CHECK(std::any_of(parts.begin(), parts.end(), [&](const WorldBlock& b) { return b.id == cube; }));
    CHECK_FALSE(std::any_of(parts.begin(), parts.end(), [&](const WorldBlock& b) { return b.id == model->id; }));
    CHECK_FALSE(document.reparent({model->id}, model->id));
    CHECK_FALSE(document.reparent({model->id}, cube));
    auto anchorBefore = document.anchor();
    REQUIRE(anchorBefore);
    BlockSet origins;
    for (const auto& id : document.transformTargets()) origins[id] = at(document, id);
    document.translate(origins, Vec3{2, 0, 0}, "verifica");
    document.endGesture();
    CHECK(std::abs(document.anchor()->x - anchorBefore->x - 2) < 0.001);
    CHECK(document.transformTargets().size() == 2);
    document.run(StudioCommand::copy);
    auto pasted = document.paste();
    REQUIRE(pasted.size() == 1);
    CHECK(document.world.childIds(pasted[0]).size() == 2);
    CHECK(pasted[0] != model->id);
    std::set<std::string> unique;
    for (const auto& b : document.world.blocks) unique.insert(b.id);
    CHECK(unique.size() == document.world.blocks.size());
    document.select({pasted[0]});
    document.run(StudioCommand::remove);
    CHECK_FALSE(std::any_of(document.world.blocks.begin(), document.world.blocks.end(), [&](const WorldBlock& b) { return b.id == pasted[0] || b.parent == pasted[0]; }));
    document.select({model->id});
    document.reparent({model->id}, "ReplicatedStorage");
    auto visible = document.blocks();
    CHECK_FALSE(std::any_of(visible.begin(), visible.end(), [&](const WorldBlock& b) { return b.id == cube; }));
    document.undo();
    visible = document.blocks();
    CHECK(std::any_of(visible.begin(), visible.end(), [&](const WorldBlock& b) { return b.id == cube; }));
    document.select({model->id});
    document.run(StudioCommand::lock);
    CHECK_FALSE(document.can(StudioCommand::remove));
    CHECK_FALSE(document.can(StudioCommand::cut));
    CHECK_FALSE(document.can(StudioCommand::group));
    document.run(StudioCommand::unlock);
    document.run(StudioCommand::ungroup);
    CHECK(at(document, cube).parent == "Workspace");
    CHECK_FALSE(document.world.contains(model->id));
    CHECK(document.selectedIds() == std::vector<std::string>{cube, sphere});
    document.run(StudioCommand::cut);
    CHECK_FALSE(document.world.contains(cube));
    CHECK(document.can(StudioCommand::paste));
    document.run(StudioCommand::paste);
    CHECK(document.world.blocks.size() == baseline.blocks.size());
    for (int i = 0; i < 200 && document.undoLabel() && !(document.world == baseline); ++i) document.undo();
    CHECK(document.world == baseline);
    document.selectAll();
    auto chosen = document.selectedIds();
    std::set<std::string> expected, actual(chosen.begin(), chosen.end());
    for (const auto& b : document.world.blocks)
        if (b.parent == "Workspace" && !b.locked) expected.insert(b.id);
    CHECK(actual == expected);
}

TEST_CASE("Studio: trasformazioni di gruppo", "[studio][workflow][transform]") {
    golden::registry();
    StudioDocument document;
    REQUIRE(document.open(project()));
    WorldBlock ga, gb, gc;
    ga.name = "GruppoA"; ga.x = -2; ga.y = 0.5; ga.z = 0;
    gb.name = "GruppoB"; gb.x = 2; gb.y = 0.5; gb.z = 0;
    gc.name = "GruppoC"; gc.x = 0; gc.y = 0.5; gc.z = 4; gc.rotationY = 90; gc.sizeZ = 2;
    for (auto* b : {&ga, &gb, &gc}) {
        b->id = makeUuid();
        document.world.blocks.push_back(*b);
    }
    document.select({ga.id, gb.id, gc.id});
    auto pivot = document.pivot();
    REQUIRE(pivot);
    CHECK(std::abs(pivot->x) < 1e-9);
    CHECK(std::abs(pivot->y - 0.5) < 1e-9);
    CHECK(std::abs(pivot->z - 2) < 1e-9);
    auto spacing = [&] { return std::array<double, 3>{gap(at(document, ga.id), at(document, gb.id)), gap(at(document, ga.id), at(document, gc.id)), gap(at(document, gb.id), at(document, gc.id))}; };
    auto before = spacing();
    Experience untouched = document.world;
    document.angleSnap = 15;
    document.perform(StudioEdit::rotateGroup(1, 37, std::nullopt));
    CHECK(facing(at(document, ga.id), 30));
    CHECK(facing(at(document, gc.id), 120));
    auto after = spacing();
    for (int i = 0; i < 3; ++i) CHECK(std::abs(after[i] - before[i]) < 1e-9);
    document.undo();
    CHECK(document.world == untouched);
    document.redo();
    CHECK(facing(at(document, ga.id), 30));
    document.undo();
    document.run(StudioCommand::rotate);
    CHECK(std::abs(at(document, ga.id).x + 2) < 1e-9);
    CHECK(std::abs(at(document, ga.id).z - 4) < 1e-9);
    CHECK(facing(at(document, ga.id), 90));
    CHECK(facing(at(document, gc.id), 180));
    document.undo();
    document.moveSnap = 0.25;
    document.perform(StudioEdit::scaleGroup(0, 2, ScaleAnchor::middle(), std::nullopt));
    CHECK(std::abs(at(document, ga.id).x + 4) < 1e-9);
    CHECK(std::abs(at(document, gb.id).x - 4) < 1e-9);
    CHECK(std::abs(at(document, ga.id).sizeX - 2) < 1e-9);
    CHECK(std::abs(at(document, gc.id).sizeZ - 4) < 1e-9);
    CHECK(std::abs(at(document, gc.id).x) < 1e-9);
    Experience scaled = document.world;
    auto reopened = Experience::parse(scaled.json().dump());
    REQUIRE(reopened);
    CHECK(*reopened == scaled);
    document.undo();
    CHECK(document.world == untouched);
    document.redo();
    CHECK(document.world == scaled);
    document.undo();
    document.select({gb.id});
    document.run(StudioCommand::lock);
    document.select({ga.id, gb.id, gc.id});
    auto targets = document.transformTargets();
    CHECK(std::find(targets.begin(), targets.end(), gb.id) == targets.end());
    WorldBlock locked = at(document, gb.id);
    document.run(StudioCommand::rotate);
    document.translate(BlockSet{{gb.id, locked}}, Vec3{5, 0, 0}, "verifica");
    document.update(gb.id, [](WorldBlock& b) { b.x = 99; });
    CHECK(at(document, gb.id) == locked);
    CHECK(facing(at(document, ga.id), 90));
}

TEST_CASE("Studio: barra dei comandi Lim", "[studio][workflow][command]") {
    golden::registry();
    StudioDocument document;
    REQUIRE(document.open(project()));
    for (const char* kind : {"cube", "sphere"}) document.addPart(kind);
    WorldBlock target = document.world.blocks[0], other = document.world.blocks[1];
    Experience before = document.world;
    document.select({target.id});
    CHECK(document.runCommand("let chosen = Selection.get()[0]\nif chosen is BasePart {\n    chosen.color = Color.rgb(224 / 255, 57 / 255, 43 / 255)\n}") == 1);
    CHECK(at(document, target.id).color == "E0392B");
    CHECK(document.undoLabel() == std::optional<std::string>("Barra dei comandi"));
    document.undo();
    CHECK(document.world == before);
    std::string move = "let p = World.find(\"" + other.displayName() + "\")\nif p is BasePart {\n    p.position = Vec3(VALUE, p.position.y, p.position.z)\n}";
    auto with = [&](const std::string& value) { std::string s = move; s.replace(s.find("VALUE"), 5, value); return s; };
    CHECK(document.runCommand(with("3")) == 1);
    CHECK(at(document, other.id).x == 3);
    document.undo();
    document.select({other.id});
    document.run(StudioCommand::lock);
    Experience locked = document.world;
    CHECK(document.runCommand(with("9")) == 0);
    CHECK(document.world == locked);
    CHECK(document.output.back().error());
    document.run(StudioCommand::unlock);
    Experience steady = document.world;
    CHECK(document.runCommand("let x = ") == 0);
    CHECK(document.world == steady);
    CHECK(document.output.back().error());
    document.world = before;
    document.run(StudioCommand::run);
    CHECK(document.playing);
    CHECK(document.playMode == StudioPlayMode::camera);
    document.run(StudioCommand::stop);
    studio::StudioStage stage(document);
    document.run(StudioCommand::playHere);
    CHECK(document.playing);
    CHECK(document.playMode == StudioPlayMode::character);
    CHECK(document.playSpawn);
    document.run(StudioCommand::stop);
}

TEST_CASE("Studio: servizi e cambio di genitore", "[studio][workflow][explorer]") {
    golden::registry();
    StudioDocument document;
    REQUIRE(document.open(project()));
    for (const char* kind : {"cube", "sphere"}) document.addPart(kind);
    auto workspaceCount = document.partsIn("Workspace").size();
    std::string moved = document.world.blocks[1].id;
    REQUIRE(document.reparent({moved}, "ReplicatedStorage"));
    CHECK(document.partsIn("Workspace").size() == workspaceCount - 1);
    CHECK(document.partsIn("ReplicatedStorage").size() == 1);
    document.play();
    double clock = 0;
    advance(document, clock, 0.2);
    auto simulated = document.blocks();
    CHECK_FALSE(std::any_of(simulated.begin(), simulated.end(), [&](const WorldBlock& b) { return b.id == moved; }));
    document.stop();
    document.undo();
    CHECK(document.partsIn("ReplicatedStorage").empty());
    document.rename(document.world.blocks[0].id, "Piattaforma");
    CHECK(document.world.blocks[0].displayName() == "Piattaforma");
    document.undo();
    CHECK(document.registry.services().size() == 12);
}

TEST_CASE("Studio: modelli importati", "[studio][workflow][assets]") {
    golden::registry();
    auto folder = std::filesystem::temp_directory_path() / ("ludissey-studio-" + makeUuid());
    std::filesystem::create_directories(folder);
    StudioDocument document;
    REQUIRE(document.open(project()));
    for (const char* kind : {"cube", "sphere", "cylinder"}) document.addPart(kind);
    auto model = folder / "Modello di verifica.obj";
    std::ofstream(model) << "v 0 0 0\nv 2 0 0\nv 1 0 2\nv 1 2 1\nf 1 2 3\nf 1 2 4\nf 2 3 4\nf 3 1 4\n";
    REQUIRE(document.importMesh(model));
    WorldBlock mesh = document.world.blocks.back();
    CHECK(mesh.kind == "mesh");
    CHECK(mesh.displayName() == model.stem().string());
    REQUIRE(mesh.meshAsset);
    auto publishable = document.publishable();
    REQUIRE(publishable);
    CHECK(publishable->blocks.back().kind == "mesh");
    CHECK(publishable->blocks.back().meshAsset == mesh.meshAsset);
    CHECK(publishable->json().dump().find(folder.string()) == std::string::npos);
    CHECK(publishable->schemaVersion == rules::schemaVersion);
    Experience keep = document.world;
    for (auto& b : document.world.blocks)
        if (b.kind == "mesh") b.meshAsset.reset();
    CHECK_FALSE(document.publishable());
    document.world = keep;
    document.update(mesh.id, [](WorldBlock& b) { b.sizeX = 3; b.sizeY = 1.5; b.sizeZ = 0.5; b.rotationY = 45; b.material = "metal"; b.transparency = 0.25; b.canCollide = false; b.locked = true; });
    WorldBlock transformed = document.world.blocks.back();
    auto again = document.publishable();
    REQUIRE(again);
    REQUIRE(document.open(*again));
    WorldBlock restored = document.world.blocks.back();
    CHECK(restored.kind == "mesh");
    CHECK(restored.meshAsset == mesh.meshAsset);
    CHECK(restored.sizeX == transformed.sizeX);
    CHECK(restored.sizeY == transformed.sizeY);
    CHECK(restored.sizeZ == transformed.sizeZ);
    CHECK(restored.rotationY == 45);
    CHECK(restored.material == "metal");
    CHECK(restored.transparency == 0.25);
    CHECK_FALSE(restored.canCollide);
    CHECK(restored.locked);
    auto unsupported = folder / "Modello di verifica.fbx";
    std::ofstream(unsupported) << "non un modello";
    auto count = document.world.blocks.size();
    CHECK_FALSE(document.importMesh(unsupported));
    CHECK(document.world.blocks.size() == count);
    document.loadAssets();
    CHECK_FALSE(document.assetsError);
    auto archived = std::find_if(document.assets.begin(), document.assets.end(), [](const StudioAsset& a) { return a.name.find("Modello di verifica") != std::string::npos; });
    REQUIRE(archived != document.assets.end());
    CHECK(document.insertAsset(*archived));
    CHECK(document.world.blocks.size() == count + 1);
    CHECK(document.world.blocks.back().meshAsset == std::optional<std::string>(archived->asset));
    document.undo();
    CHECK(document.world.blocks.size() == count);
    std::filesystem::remove_all(folder);
}

TEST_CASE("Studio: limiti globali e nodi incrementali", "[studio][workflow][rendering]") {
    WorldBlock slab;
    slab.sizeX = 8; slab.sizeY = 1; slab.sizeZ = 2;
    Vec3 straight = slab.halfExtents();
    CHECK(std::abs(straight.x - 4) < 0.001);
    CHECK(std::abs(straight.y - 0.5) < 0.001);
    CHECK(std::abs(straight.z - 1) < 0.001);
    slab.rotationY = 90;
    Vec3 turned = slab.halfExtents();
    CHECK(std::abs(turned.x - 1) < 0.01);
    CHECK(std::abs(turned.y - 0.5) < 0.01);
    CHECK(std::abs(turned.z - 4) < 0.01);
    slab.rotationY = 45;
    Vec3 diagonal = slab.halfExtents();
    CHECK(diagonal.x > 3);
    CHECK(diagonal.x < 3.6);
    CHECK(std::abs(diagonal.y - 0.5) < 0.01);
    WorldBlock sample;
    std::string structure = render::scene_factory::structure(sample);
    sample.rotationY = 30; sample.material = "metal"; sample.sizeY = 4; sample.transparency = 0.5;
    CHECK(render::scene_factory::structure(sample) == structure);
    auto node = render::scene_factory::block(WorldBlock());
    render::scene_factory::apply(sample, *node);
    CHECK(std::abs(node->scale.y - 1) < 0.001);
    REQUIRE(node->geometry);
    CHECK(std::abs(node->geometry->bounds().high.y - node->geometry->bounds().low.y - 4) < 0.001);
    WorldBlock meshed = sample;
    meshed.kind = "mesh";
    meshed.meshAsset = "modello.obj";
    CHECK(render::scene_factory::structure(meshed) != structure);
}

TEST_CASE("Studio: inserimento con ricerca", "[studio][workflow][registry]") {
    golden::registry();
    StudioDocument document;
    REQUIRE(document.open(Experience::blank()));
    std::string plate = document.world.blocks[0].id;
    CHECK_FALSE(document.insert("Part", std::string("ServerScriptService")));
    CHECK(document.output.back().error());
    CHECK_FALSE(document.insert("LocalScript", std::string("Lighting")));
    CHECK_FALSE(document.insert("Frame", std::string("Workspace")));
    auto script = document.insert("Script", std::string("ServerScriptService"));
    auto gui = document.insert("ScreenGui", std::string("StarterGui"));
    REQUIRE(script);
    REQUIRE(gui);
    auto frame = document.insert("Frame", *gui);
    auto folder = document.insert("Folder", std::string("Workspace"));
    REQUIRE(frame);
    REQUIRE(folder);
    CHECK(document.world.parentOf(*frame) == gui);
    CHECK(document.className(*frame) == std::optional<std::string>("Frame"));
    CHECK(std::find(document.openScripts.begin(), document.openScripts.end(), *script) != document.openScripts.end());
    document.insertQuery = "value";
    auto values = document.insertable("Workspace");
    REQUIRE_FALSE(values.empty());
    for (const auto* d : values) CHECK(lowercase(d->className).find("value") != std::string::npos);
    document.insertQuery.clear();
    CHECK_FALSE(document.canDrop({plate}, "StarterGui"));
    CHECK_FALSE(document.canDrop({*frame}, "Workspace"));
    CHECK(document.canDrop({*script}, *folder));
    document.select({*script});
    document.copySelection();
    document.select({*folder});
    auto pasted = document.pasteInto();
    REQUIRE(pasted.size() == 1);
    CHECK(document.world.parentOf(pasted[0]) == folder);
    CHECK(document.className(pasted[0]) == std::optional<std::string>("Script"));
    auto brick = document.insert("Part", std::string("Workspace"));
    REQUIRE(brick);
    document.reparent({pasted[0]}, "Workspace");
    document.select({pasted[0], *brick});
    document.group();
    auto model = document.selection()->instanceId().value_or("");
    CHECK(document.className(model) == std::optional<std::string>("Model"));
    auto children = document.world.childIds(model);
    CHECK(std::set<std::string>(children.begin(), children.end()) == std::set<std::string>{pasted[0], *brick});
    document.ungroup();
    CHECK_FALSE(document.exists(model));
    CHECK(document.world.parentOf(*brick) == std::optional<std::string>("Workspace"));
    CHECK(document.world.parentOf(pasted[0]) == std::optional<std::string>("Workspace"));
    document.select({plate});
    document.setLocked(true);
    document.group();
    CHECK(document.world.parentOf(plate) == std::optional<std::string>("Workspace"));
    document.setLocked(false);
    document.setSelection(StudioSelection::ofService("Workspace"));
    document.selectChildren();
    auto chosen = document.selectedIds();
    CHECK(std::find(chosen.begin(), chosen.end(), plate) != chosen.end());
    CHECK(std::find(chosen.begin(), chosen.end(), *folder) != chosen.end());
    document.rename(*folder, "Contenitore");
    CHECK(document.displayName(*folder) == "Contenitore");
    CHECK(document.undoLabel() == std::optional<std::string>("Rinomina"));
    document.undo();
    CHECK(document.displayName(*folder) == "Folder");
    Experience before = document.world;
    CHECK(document.runCommand("let f = Folder(parent: World)\nf.name = \"Creata\"") == 1);
    CHECK(std::any_of(document.world.instances.begin(), document.world.instances.end(), [](const InstanceRecord& r) { return r.name == "Creata" && r.className == "Folder"; }));
    CHECK(document.undoLabel() == std::optional<std::string>("Barra dei comandi"));
    document.undo();
    CHECK(document.world == before);
    InstanceRecord future;
    future.id = makeUuid();
    future.className = "ProximityPrompt";
    future.name = "Futuro";
    future.parent = "Workspace";
    document.world.instances.push_back(future);
    CHECK_FALSE(document.publishable());
    document.world = before;
    CHECK(document.path(*script) == "ServerScriptService.Script");
    document.setSource(*script, "print(\"ok\")\nlet = 3\n");
    REQUIRE(document.scriptErrors().count(*script));
    CHECK(document.scriptErrors().at(*script).line == 2);
    document.setSource(*script, "print(\"ok\")\n");
    CHECK_FALSE(document.scriptErrors().count(*script));
    OutputLine line;
    line.text = "ServerScriptService.Script:1: prova";
    line.kind = OutputLine::Kind::error;
    CHECK(document.reveal(line));
    CHECK(document.activeScript == script);
    CHECK(document.focusLine == std::optional<int>(1));
}

TEST_CASE("Studio: prestazioni degli script", "[studio][workflow][performance]") {
    golden::registry();
    StudioDocument document;
    REQUIRE(document.open(Experience::blank()));
    auto busy = document.insert("Script", std::string("ServerScriptService"));
    REQUIRE(busy);
    document.rename(*busy, "Calcolo");
    document.setSource(*busy, "var s = 0\nvar data: [Integer] = []\nfor i in 1..." + std::string(support::instrumented ? "20000" : "200000") + " {\n    s += i\n    if i % 20 == 0 {\n        data.append(i)\n    }\n}\nfor i in 1...5 {\n    await Task.nextFrame()\n}\nprint(s, data.count)\nawait Task.wait(10)\n");
    auto listener = document.insert("Script", std::string("ServerScriptService"));
    REQUIRE(listener);
    document.rename(*listener, "Ascolto");
    document.setSource(*listener, "let e = BindableEvent(parent: World)\non e.event() {\n    print(\"evento\")\n}\nfor i in 1...3 {\n    e.fire()\n}\n");
    CHECK(document.scriptPerformance().scripts.empty());
    document.playMode = StudioPlayMode::camera;
    document.play();
    double clock = 0;
    for (int i = 0; i < 30; ++i) document.tick(clock += 1.0 / 60);
    std::string transcript;
    for (const auto& line : document.output) transcript += line.text + "\n";
    INFO(transcript);
    auto live = document.scriptPerformance();
    auto find = [&](const ScriptPerformance& p, const std::string& name) {
        auto it = std::find_if(p.scripts.begin(), p.scripts.end(), [&](const ScriptMetric& m) { return m.script == "ServerScriptService." + name; });
        REQUIRE(it != p.scripts.end());
        return *it;
    };
    auto calcolo = find(live, "Calcolo");
    CHECK(calcolo.seconds > 0);
    CHECK(calcolo.frames == 5);
    CHECK(calcolo.context == ScriptContext::server);
    CHECK(find(live, "Ascolto").events == 3);
    CHECK(live.memory.count(ScriptContext::server));
    CHECK(live.memory.at(ScriptContext::server) > 0);
    CHECK(live.elapsed > 0);
    CHECK(live.scripts.front().script == "ServerScriptService.Calcolo");
    document.stop();
    auto kept = document.scriptPerformance();
    CHECK(find(kept, "Calcolo").frames == 5);
    REQUIRE(document.open(Experience::blank()));
    CHECK(document.scriptPerformance().scripts.empty());
}

TEST_CASE("Studio: import di immagini e audio", "[studio][workflow][assets][content]") {
    golden::registry();
    auto folder = std::filesystem::temp_directory_path() / ("ludissey-contenuti-" + makeUuid());
    std::filesystem::create_directories(folder);
    StudioDocument document;
    REQUIRE(document.open(project()));
    auto sound = document.insert("Sound", std::string("SoundService"));
    REQUIRE(sound);
    auto audio = folder / "Colonna sonora.wav", picture = folder / "Logo.png", text = folder / "Appunti.txt", empty = folder / "Vuoto.wav", huge = folder / "Enorme.wav";
    std::ofstream(audio, std::ios::binary) << "RIFF audio di prova";
    std::ofstream(picture, std::ios::binary) << "PNG di prova";
    std::ofstream(text) << "testo";
    std::ofstream(empty).flush();
    std::ofstream(huge, std::ios::binary) << std::string(static_cast<std::size_t>(rules::assets::megabytes) * 1024 * 1024 + 1, 'x');
    auto refused = [&](const std::filesystem::path& file, const std::string& property, const std::string& expected) {
        auto result = document.importContent(*sound, property, file);
        REQUIRE_FALSE(result.ok());
        INFO(result.error().message);
        CHECK(result.error().message.find(expected) != std::string::npos);
    };
    refused(text, "SoundId", "Ludissey non apre i file .txt");
    refused(picture, "SoundId", ".mp3 .wav .m4a .aiff");
    refused(audio, "Image", ".png .jpg .jpeg");
    refused(empty, "SoundId", "Il file è vuoto");
    refused(huge, "SoundId", "al massimo 8 MB");
    refused(folder / "Assente.wav", "SoundId", "File non leggibile");
    CHECK(asset_cache::content(document.world.record(*sound)->properties.at("SoundId")) == std::nullopt);
    REQUIRE(document.importContent(*sound, "SoundId", audio));
    auto stored = document.world.record(*sound)->properties.at("SoundId");
    auto asset = asset_cache::content(stored);
    REQUIRE(asset);
    CHECK(endsWith(*asset, ".wav"));
    CHECK(asset_cache::verified(*asset));
    CHECK(stored.text.find(folder.string()) == std::string::npos);
    auto publishable = document.publishable();
    REQUIRE(publishable);
    auto referenced = AssetService::references(*publishable);
    CHECK(std::count(referenced.begin(), referenced.end(), AssetId(*asset)) == 1);
    document.undo();
    CHECK(asset_cache::content(document.world.record(*sound)->properties.at("SoundId")) == std::nullopt);
    document.redo();
    CHECK(document.world.record(*sound)->properties.at("SoundId") == stored);
    document.playMode = StudioPlayMode::camera;
    document.play();
    refused(audio, "SoundId", "Ferma la prova");
    document.stop();
    std::filesystem::remove_all(folder);
}

TEST_CASE("Studio: script precedenti a Lim", "[studio][workflow][legacy]") {
    golden::registry();
    Experience modern = Experience::blank();
    REQUIRE_FALSE(modern.blocks.empty());
    InstanceRecord old;
    old.id = "vecchio";
    old.className = "Script";
    old.name = "Vecchio";
    old.parent = modern.blocks.front().id;
    old.properties["Source"] = PropertyValue::string("local n = 0\nprint(\"eseguito\")\n");
    old.properties["Disabled"] = PropertyValue::boolean(false);
    modern.instances.push_back(old);
    Json data = modern.json();
    data.set("schemaVersion", 4);
    auto legacy = Experience::fromJson(data);
    REQUIRE(legacy);
    REQUIRE(legacy->record("vecchio"));
    CHECK(legacy->record("vecchio")->source() == "local n = 0\nprint(\"eseguito\")\n");
    CHECK(legacy->legacyScripts() == std::vector<std::string>{"vecchio"});
    CHECK(legacy->record("vecchio")->properties.at("Disabled") == PropertyValue::boolean(true));
    StudioDocument document;
    REQUIRE(document.open(*legacy));
    CHECK(document.world.schemaVersion == rules::schemaVersion);
    CHECK(document.world.record("vecchio")->source() == "local n = 0\nprint(\"eseguito\")\n");
    CHECK(printed(document, "scritto per una versione vecchia", true));
    double clock = 0;
    document.playMode = StudioPlayMode::camera;
    document.play();
    REQUIRE(document.playing);
    advance(document, clock, 1);
    CHECK_FALSE(printed(document, "eseguito\n"));
    CHECK(std::none_of(document.output.begin(), document.output.end(), [](const OutputLine& l) { return l.text == "eseguito"; }));
    document.stop();
    WorldSession session(*legacy, WorldSession::Mode::play, "Prova", 1, false, [](const OutputLine& l) { CHECK(l.text != "eseguito"); });
    session.start({0, 6, 0});
    for (int i = 0; i < 30; ++i) session.advance(1.0 / 60);
    session.stop();
}

TEST_CASE("Studio: icona e miniatura dell'esperienza", "[studio][workflow][assets][images]") {
    golden::registry();
    auto folder = std::filesystem::temp_directory_path() / ("Ludissey Immagini " + makeUuid());
    std::filesystem::create_directories(folder);
    asset_cache::setFolder(folder / "cache");
    auto picture = [&](const std::string& name, int width, int height) {
        std::vector<std::uint8_t> rgba(static_cast<std::size_t>(width * height * 4), 200);
        auto png = encodePng(rgba, width, height);
        REQUIRE(png);
        auto file = folder / name;
        std::ofstream(file, std::ios::binary) << *png;
        return file;
    };
    StudioDocument document;
    REQUIRE(document.open(project()));
    CHECK_FALSE(document.experienceImage(ExperienceImage::icon));
    CHECK_FALSE(document.experienceImageFile(ExperienceImage::icon));
    auto icon = picture("icona.png", 256, 256), cover = picture("miniatura.png", 640, 360);
    REQUIRE(document.setExperienceImage(ExperienceImage::icon, icon).ok());
    REQUIRE(document.setExperienceImage(ExperienceImage::thumbnail, cover).ok());
    REQUIRE(document.world.icon);
    REQUIRE(document.world.thumbnail);
    CHECK(isImageAsset(*document.world.icon));
    auto stored = document.experienceImageFile(ExperienceImage::icon);
    REQUIRE(stored);
    CHECK(golden::readText(*stored) == golden::readText(icon));
    auto references = AssetService::references(document.world);
    CHECK(std::count(references.begin(), references.end(), AssetId(*document.world.icon)) == 1);
    CHECK(std::count(references.begin(), references.end(), AssetId(*document.world.thumbnail)) == 1);
    auto reparsed = Experience::parse(document.world.json().dump());
    REQUIRE(reparsed.ok());
    CHECK(reparsed->icon == document.world.icon);
    CHECK(reparsed->thumbnail == document.world.thumbnail);
    document.run(StudioCommand::undo);
    CHECK_FALSE(document.world.thumbnail);
    CHECK(document.world.icon);
    document.run(StudioCommand::redo);
    CHECK(document.world.thumbnail);
    std::ofstream(folder / "finta.png") << "non sono un'immagine";
    CHECK_FALSE(document.setExperienceImage(ExperienceImage::icon, folder / "finta.png").ok());
    CHECK_FALSE(document.setExperienceImage(ExperienceImage::icon, picture("piccola.png", 8, 8)).ok());
    std::ofstream(folder / "testo.txt") << "testo";
    CHECK_FALSE(document.setExperienceImage(ExperienceImage::icon, folder / "testo.txt").ok());
    CHECK(document.experienceImageFile(ExperienceImage::icon) == stored);
    document.clearExperienceImage(ExperienceImage::icon);
    CHECK_FALSE(document.world.icon);
    CHECK_FALSE(document.experienceImageFile(ExperienceImage::icon));
    auto forged = document.world.json();
    forged.set("icon", "../../segreto.png");
    auto rejected = Experience::fromJson(forged);
    REQUIRE(rejected.ok());
    CHECK_FALSE(rejected->icon);
    asset_cache::setFolder(std::nullopt);
    std::filesystem::remove_all(folder);
}

TEST_CASE("Studio: pennello sulla superficie del Baseplate", "[studio][workflow][terrain][placement]") {
    golden::registry();
    StudioDocument document;
    Experience world = Experience::blank();
    world.id = makeUuid();
    REQUIRE(document.open(world));
    studio::StudioStage stage(document);
    stage.size = {800, 600};
    stage.orient(0.4, 1.0, 30, {0, 0, 0});
    stage.refresh();
    const WorldBlock* baseplate = nullptr;
    for (const auto& b : document.world.blocks)
        if (b.name == rules::baseplate::name) baseplate = &b;
    REQUIRE(baseplate);
    double surfaceTop = baseplate->y + baseplate->sizeY / 2;
    document.terrainTool = TerrainTool::add;
    document.terrainBrush.placement = TerrainPlacement::surface;
    document.terrainBrush.radius = 5;
    document.terrainBrush.strength = 0.5;
    auto press = [&](double x, double y) {
        ui::PointerEvent e;
        e.phase = ui::PointerPhase::down;
        e.location = {x, y};
        stage.pointer(e, stage.size);
        e.phase = ui::PointerPhase::up;
        stage.pointer(e, stage.size);
    };
    press(400, 300);
    REQUIRE_FALSE(document.world.terrain.empty());
    std::vector<double> heights;
    std::optional<std::pair<int, int>> centre;
    double best = 1e9;
    for (int i = 0; i < Terrain::cellsX; ++i)
        for (int k = 0; k < Terrain::cellsZ; ++k)
            if (auto h = document.world.terrain.surfaceHeight(i, k)) {
                heights.push_back(*h);
                Vec3 c = Terrain::center(i, 0, k);
                if (std::hypot(c.x, c.z) < best) best = std::hypot(c.x, c.z), centre = std::pair{i, k};
            }
    REQUIRE(heights.size() > 10);
    double expected = surfaceTop + document.terrainBrush.strength * Terrain::cellSize;
    for (double h : heights) CHECK(h == Catch::Approx(expected).margin(Terrain::cellSize / Terrain::levels));
    CHECK(document.undoLabel());
    document.terrainTool = TerrainTool::subtract;
    document.terrainBrush.radius = 3;
    press(400, 300);
    REQUIRE(centre);
    auto dug = document.world.terrain.surfaceHeight(centre->first, centre->second);
    CHECK((!dug || *dug < expected - 0.5));
}

TEST_CASE("Studio: modifica senza effetto", "[studio][workflow][undo]") {
    golden::registry();
    StudioDocument document;
    Experience world = Experience::blank();
    world.id = makeUuid();
    REQUIRE(document.open(world));
    REQUIRE(document.addPart("cube"));
    std::string id = document.selectedIds().front();
    document.update(id, [](WorldBlock& b) { b.x = 4; }, std::string("Sposta"));
    Experience moved = document.world;
    document.undo();
    Experience original = document.world;
    REQUIRE(document.redoLabel());
    const WorldBlock* current = document.world.block(id);
    REQUIRE(current);
    double x = current->x;
    document.update(id, [x](WorldBlock& b) { b.x = x; }, std::string("Stesso valore"));
    document.update(id, [](WorldBlock& b) { b.x = rules::world::horizontal * 10; }, std::string("Oltre il limite"));
    document.update(id, [](WorldBlock& b) { b.x = rules::world::horizontal * 20; }, std::string("Ancora oltre"));
    CHECK(document.world.block(id)->x == rules::world::horizontal);
    document.undo();
    CHECK(document.world == original);
    CHECK(document.redoLabel() == std::optional<std::string>("Oltre il limite"));
    document.redo();
    document.update(id, [](WorldBlock& b) { b.x = rules::world::horizontal; }, std::string("Uguale al limite"));
    CHECK(document.undoLabel() == std::optional<std::string>("Oltre il limite"));
    document.undo();
    document.redo();
    CHECK(document.world.block(id)->x == rules::world::horizontal);
    CHECK(moved.block(id)->x == 4);
}
