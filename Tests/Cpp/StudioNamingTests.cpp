#include "Engine/Studio/StudioDocument.hpp"
#include "Golden.hpp"

using namespace ld;

namespace {

std::string nameOf(const StudioDocument& document, const std::string& id) {
    if (auto block = document.world.block(id)) return block->name;
    for (const auto& record : document.world.instances)
        if (record.id == id) return record.name;
    return "";
}

std::string selected(const StudioDocument& document) { return document.selectedIds().empty() ? std::string() : document.selectedIds().front(); }

}

TEST_CASE("Studio: nomi degli oggetti senza numeri", "[studio][naming]") {
    golden::registry();
    StudioDocument::clipboard() = {};
    StudioDocument document;
    document.open(Experience::blank());
    for (const auto& [kind, name] : std::vector<std::pair<std::string, std::string>>{{"cube", "Cubo"}, {"sphere", "Sfera"}, {"cylinder", "Cilindro"}, {"cube", "Cubo"}}) {
        CAPTURE(kind);
        REQUIRE(document.addPart(kind));
        CHECK(nameOf(document, selected(document)) == name);
    }
    for (const auto& [type, name] : std::vector<std::pair<std::string, std::string>>{{"Part", "Cubo"}, {"WedgePart", "Cuneo"}, {"SpawnLocation", "Punto di partenza"}, {"Seat", "Sedile"}, {"Model", "Modello"}}) {
        CAPTURE(type);
        auto made = document.insert(type, "Workspace");
        REQUIRE(made);
        CHECK(nameOf(document, *made) == name);
    }
    auto script = document.insert("Script", "ServerScriptService");
    REQUIRE(script);
    CHECK(nameOf(document, *script) == "Script");
    auto folder = document.insert("Folder", "ReplicatedStorage");
    REQUIRE(folder);
    CHECK(nameOf(document, *folder) == "Folder");
    REQUIRE(document.addPart("sphere"));
    std::string ball = selected(document);
    document.rename(ball, "Palla");
    document.select({ball});
    document.duplicateSelection();
    CHECK(nameOf(document, selected(document)) == "Palla");
    document.select({ball});
    document.copySelection();
    auto pasted = document.paste();
    REQUIRE(pasted.size() == 1);
    CHECK(nameOf(document, pasted.front()) == "Palla");
    document.select({ball});
    document.group();
    CHECK(nameOf(document, selected(document)) == "Modello");
    document.meshAvailable = [](const std::string&) { return true; };
    REQUIRE(document.insertMesh(StudioAsset{"a1b2", "Casa del porto.obj", "obj"}).ok());
    CHECK(nameOf(document, selected(document)) == "Casa del porto");
}

TEST_CASE("Studio: il numero compare solo rinominando con un nome già usato", "[studio][naming]") {
    golden::registry();
    StudioDocument document;
    document.open(Experience::blank());
    std::vector<std::string> cubes;
    for (int i = 0; i < 3; ++i) {
        REQUIRE(document.addPart("cube"));
        cubes.push_back(selected(document));
    }
    document.rename(cubes[0], "Muro");
    CHECK(nameOf(document, cubes[0]) == "Muro");
    document.rename(cubes[1], "Muro");
    CHECK(nameOf(document, cubes[1]) == "Muro 2");
    document.rename(cubes[2], "Muro");
    CHECK(nameOf(document, cubes[2]) == "Muro 3");
    document.rename(cubes[0], "Muro");
    CHECK(nameOf(document, cubes[0]) == "Muro");
    auto first = document.insert("Folder", "ReplicatedStorage"), second = document.insert("Folder", "ReplicatedStorage");
    REQUIRE(first);
    REQUIRE(second);
    document.rename(*second, "Cartella");
    document.rename(*second, "Folder");
    CHECK(nameOf(document, *second) == "Folder 2");
    auto elsewhere = document.insert("Folder", "ServerStorage");
    REQUIRE(elsewhere);
    document.rename(*elsewhere, "Folder");
    CHECK(nameOf(document, *elsewhere) == "Folder");
}
