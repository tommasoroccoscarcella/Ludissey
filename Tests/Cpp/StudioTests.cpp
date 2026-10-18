#include "Engine/Studio/StudioDocument.hpp"
#include "Golden.hpp"

#include <algorithm>

using namespace ld;

namespace {

struct Normalizer {
    std::map<std::string, std::string> ids;
    std::string operator()(const std::string& id) {
        if (id.size() != 36 || id == "Workspace" || id == "Lighting") return id;
        auto known = ids.find(id);
        if (known != ids.end()) return known->second;
        std::string next = "id" + std::to_string(ids.size() + 1);
        ids[id] = next;
        return next;
    }
};

Json snapshot(const StudioDocument& document, Normalizer& normal) {
    Json blocks = Json::array(), records = Json::array(), selection = Json::array();
    for (const auto& b : document.world.blocks) {
        Json d = b.json();
        d.set("id", normal(b.id));
        d.set("parent", normal(b.parent));
        blocks.push(d);
    }
    for (const auto& r : document.world.instances) {
        Json d = r.json();
        d.set("id", normal(r.id));
        d.set("parent", normal(r.parent));
        records.push(d);
    }
    for (const auto& id : document.selectedIds()) selection.push(normal(id));
    return Json::object({{"blocks", blocks}, {"instances", records}, {"selection", selection}, {"undo", document.undoLabel() ? Json(*document.undoLabel()) : Json()},
                         {"redo", document.redoLabel() ? Json(*document.redoLabel()) : Json()}, {"title", document.world.title}});
}

std::string findKind(const StudioDocument& d, const std::string& kind) {
    for (const auto& b : d.world.blocks)
        if (b.kind == kind) return b.id;
    return "";
}

}

TEST_CASE("Parità: documento di Studio, comandi e annulla", "[parity][studio]") {
    Json g = golden::load("studio");
    golden::registry();
    StudioDocument::clipboard() = {};
    StudioDocument document;
    Experience world = Experience::blank();
    world.id = "5a1f0c2e-7d3b-4c1a-8e2f-0000000000ee";
    world.blocks[0].id = "00000000-0000-4000-8000-0000000000ee";
    document.open(world);
    Normalizer normal;
    std::vector<std::pair<std::string, Json>> steps;
    auto record = [&](const std::string& label) { steps.emplace_back(label, snapshot(document, normal)); };
    record("aperto");
    document.addPart("cube");
    record("cubo");
    document.addPart("sphere");
    record("sfera");
    document.update(document.selectedPart()->id, [](WorldBlock& b) {
        b.x = 30;
        b.sizeY = 500;
        b.rotationY = 270;
    }, "Sposta");
    record("clamp");
    std::vector<std::string> nonBase;
    for (const auto& b : document.world.blocks)
        if (b.name != "Baseplate") nonBase.push_back(b.id);
    document.select(nonBase);
    record("selezione");
    document.run(StudioCommand::group);
    record("raggruppa");
    document.run(StudioCommand::duplicate);
    record("duplica");
    document.run(StudioCommand::undo);
    record("annulla");
    document.run(StudioCommand::redo);
    record("ripeti");
    document.select({findKind(document, "model")});
    document.run(StudioCommand::ungroup);
    record("separa");
    document.run(StudioCommand::rotate);
    record("ruota");
    document.run(StudioCommand::tilt);
    record("inclina");
    document.perform(StudioEdit::scaleGroup(0, 2, ScaleAnchor::face(1), "g1"));
    document.perform(StudioEdit::scaleGroup(0, 2.5, ScaleAnchor::face(1), "g1"));
    document.endGesture();
    record("scala");
    document.insert("Script", "Workspace");
    record("script");
    document.setSource(*document.selection()->instanceId(), "print(1)");
    record("sorgente");
    document.insert("Folder", "ReplicatedStorage");
    record("cartella");
    document.insert("Part", "Lighting");
    record("parte rifiutata");
    document.run(StudioCommand::copy);
    document.run(StudioCommand::paste);
    record("incolla");
    document.select({findKind(document, "sphere")});
    document.dropToFloor();
    record("al suolo");
    document.setLocked(true);
    document.run(StudioCommand::remove);
    record("bloccato");
    document.setLocked(false);
    document.run(StudioCommand::remove);
    record("elimina");
    document.configure("Nuovo titolo", "Corse", 99);
    record("impostazioni");
    document.setLighting("clockTime", [](WorldLighting& l) { l.clockTime = 30; });
    record("luce");
    document.rename(document.world.blocks.back().id, std::string(80, 'x'));
    record("rinomina");
    document.run(StudioCommand::selectAll);
    record("tutto");
    for (int i = 0; i < 5; ++i) document.run(StudioCommand::undo);
    record("cinque annulla");
    if (golden::writing()) {
        Json written = Json::array();
        for (const auto& [step, state] : steps) written.push(Json::object({{"step", step}, {"state", state}}));
        g.set("steps", written);
        golden::save("studio", g);
    }
    REQUIRE(steps.size() == g["steps"].size());
    for (std::size_t i = 0; i < steps.size(); ++i) {
        const Json& expected = g["steps"][i];
        INFO(steps[i].first);
        CHECK(steps[i].first == expected["step"].string());
        CHECK(golden::same(steps[i].second, expected["state"], golden::Options{1e-9, true}) == "");
    }
}

TEST_CASE("Studio: livello dell'acqua annullabile", "[studio][terrain][water]") {
    StudioDocument document;
    document.open(Experience::blank());
    auto before = document.world;
    auto create = StudioEdit::sea(TerrainRegion{{-8, -16, -8}, {8, 4, 8}});
    REQUIRE(document.perform(create) > 0);
    CHECK(document.world.blocks == before.blocks);
    CHECK(document.world.terrain.water(0, 2, 0));
    auto wet = document.world.terrain;
    document.run(StudioCommand::undo);
    CHECK(document.world.terrain == before.terrain);
    CHECK(document.redoLabel());
    CHECK(document.perform(StudioEdit::sea(TerrainRegion{{-8, -16, -8}, {8, 16, 8}}, true)) == 0);
    CHECK(document.redoLabel());
    document.run(StudioCommand::redo);
    CHECK(document.world.terrain == wet);
    CHECK(document.perform(create) == 0);
    REQUIRE(document.perform(StudioEdit::sea(TerrainRegion{{-8, -16, -8}, {8, 16, 8}}, true)) > 0);
    CHECK(document.world.terrain.empty());
    document.run(StudioCommand::undo);
    CHECK(document.world.terrain == wet);
}

TEST_CASE("Studio: inserimento e barra dei comandi", "[studio]") {
    golden::registry();
    StudioDocument document;
    document.open(Experience::blank());
    CHECK_FALSE(document.insert("Part", "ServerScriptService"));
    CHECK(document.output.back().error());
    CHECK_FALSE(document.insert("LocalScript", "Lighting"));
    CHECK_FALSE(document.insert("Frame", "Workspace"));
    auto script = document.insert("Script", "ServerScriptService");
    REQUIRE(script);
    document.setSource(*script, "let x: Number = \"testo\"\n");
    REQUIRE(document.scriptErrors().count(*script));
    CHECK(document.scriptErrors().at(*script).line == 1);
    document.setSource(*script, "print(\"ok\")\n");
    CHECK_FALSE(document.scriptErrors().count(*script));
    int changed = document.runCommand("let p = Part(parent: World, name: \"DaComando\")\np.position = Vec3(1, 2, 3)");
    CHECK(changed == 1);
    auto made = std::find_if(document.world.blocks.begin(), document.world.blocks.end(), [](const WorldBlock& b) { return b.name == "DaComando"; });
    REQUIRE(made != document.world.blocks.end());
    CHECK(made->x == 1);
    CHECK(document.undoLabel() == std::optional<std::string>("Barra dei comandi"));
    document.select({document.world.blocks.front().id});
    document.setLocked(true);
    document.runCommand("let base = World.findFirstChild(\"Baseplate\")\nif base is BasePart {\n    base.transparency = 1\n}");
    CHECK(document.world.blocks.front().transparency == 0);
    CHECK(std::any_of(document.output.begin(), document.output.end(), [](const OutputLine& l) { return l.text.find("è bloccato") != std::string::npos; }));
    document.run(StudioCommand::play);
    CHECK(document.playing);
    for (int i = 1; i <= 10; ++i) document.tick(i / 60.0);
    document.run(StudioCommand::stop);
    CHECK_FALSE(document.playing);
}

TEST_CASE("Studio: Terrain.fillSphere da comando e prova", "[studio][terrain][lim]") {
    golden::registry();
    StudioDocument document;
    document.open(Experience::blank());
    REQUIRE(document.world.terrain.empty());
    int changed = document.runCommand("print(Terrain.fillSphere(center: Vec3(0, 8, 0), radius: 6, material: .grass) > 0)");
    CHECK(changed == 1);
    CHECK_FALSE(document.world.terrain.empty());
    CHECK(document.world.terrain.solid(0, 8, 0));
    CHECK(document.undoLabel() == std::optional<std::string>("Barra dei comandi"));
    document.run(StudioCommand::undo);
    CHECK(document.world.terrain.empty());
    CHECK_FALSE(document.world.terrain.solid(0, 8, 0));
    document.run(StudioCommand::redo);
    CHECK(document.world.terrain.solid(0, 8, 0));
    document.run(StudioCommand::undo);
    REQUIRE(document.world.terrain.empty());

    CHECK(document.runCommand("Terrain.fillSphere(center: Vec3(0, 8, 0), radius: 0, material: .rock)") == 0);
    CHECK(document.output.back().error());
    CHECK(document.world.terrain.empty());

    auto script = document.insert("Script", "ServerScriptService");
    REQUIRE(script);
    document.setSource(*script, "Terrain.fillSphere(center: Vec3(0, 6, 0), radius: 4, material: .water)\n");
    REQUIRE_FALSE(document.scriptErrors().count(*script));
    Experience before = document.world;
    document.run(StudioCommand::play);
    REQUIRE(document.playing);
    for (int i = 1; i <= 10; ++i) document.tick(i / 60.0);
    CHECK(document.activeTerrain().water(0, 6, 0));
    CHECK_FALSE(document.world.terrain.water(0, 6, 0));
    document.run(StudioCommand::stop);
    CHECK_FALSE(document.playing);
    CHECK(document.activeTerrain().empty());
    CHECK(document.world.terrain == before.terrain);
}
