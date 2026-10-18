#include "Engine/Core/Json.hpp"
#include "Engine/DataModel/DataModel.hpp"
#include "Engine/Experience/Experience.hpp"
#include "Engine/Studio/StudioDocument.hpp"
#include "Golden.hpp"

#include <Catch2/catch_amalgamated.hpp>

#include <chrono>
#include <functional>
#include <random>

using namespace ld;

namespace {

Json sample() {
    golden::registry();
    StudioDocument document;
    Experience start;
    start.id = "1b2c3d4e-5f60-4718-8a9b-0c1d2e3f4a5b";
    start.title = "Archivio di verifica";
    REQUIRE(document.open(start));
    for (const char* kind : {"cube", "sphere", "cylinder", "cube"}) document.addPart(kind);
    auto script = document.insert("Script", document.world.blocks[1].id);
    REQUIRE(script);
    document.setSource(*script, "let p = script.parent\nprint(p.name)\n");
    auto folder = document.insert("Folder", std::string("Workspace"));
    REQUIRE(folder);
    document.perform(StudioEdit::generate(document.terrainSettings));
    return document.world.json();
}

void exercise(const Experience& world) {
    golden::registry();
    StudioDocument document;
    if (!document.open(world)) return;
    std::size_t total = 0;
    for (const auto& block : document.world.blocks) total += document.path(block.id).size() + document.children(block.id).size();
    for (const auto& record : document.world.instances) total += document.path(record.id).size() + document.children(record.id).size();
    for (const char* service : {"Workspace", "ServerScriptService", "StarterGui"}) total += document.children(service).size();
    DataModel model(document.world);
    document.playMode = StudioPlayMode::camera;
    document.play();
    for (int i = 0; i < 3; ++i) document.tick(i / 60.0);
    document.stop();
    auto reparsed = Experience::parse(document.world.json().dump());
    CHECK(reparsed.ok());
    CHECK(total < std::size_t(1) << 40);
}

}

TEST_CASE("Fuzz: archivi dei progetti", "[serialization][fuzz][studio]") {
    Json base = sample();
    std::string text = base.dump();
    REQUIRE(Experience::parse(text).ok());
    auto started = std::chrono::steady_clock::now();
    int accepted = 0, rejected = 0;
    auto attempt = [&](const std::string& candidate) {
        auto parsed = Experience::parse(candidate);
        if (!parsed) {
            ++rejected;
            return;
        }
        ++accepted;
        exercise(*parsed);
    };
    for (std::size_t length = 0; length < text.size(); length += std::max<std::size_t>(1, text.size() / 300)) attempt(text.substr(0, length));
    std::mt19937 random(20261003);
    const std::string replacements = "{}[]\",:0-1e9 nulltruefalse\\";
    for (int i = 0; i < 1500; ++i) {
        std::string mutated = text;
        int edits = 1 + static_cast<int>(random() % 4);
        for (int e = 0; e < edits && !mutated.empty(); ++e) {
            std::size_t at = random() % mutated.size();
            switch (random() % 3) {
            case 0: mutated[at] = replacements[random() % replacements.size()]; break;
            case 1: mutated.erase(at, 1 + random() % 8); break;
            default: mutated.insert(at, 1, replacements[random() % replacements.size()]); break;
            }
        }
        attempt(mutated);
    }
    auto structural = [&](const std::function<void(Json&)>& change) {
        Json copy = base;
        change(copy);
        attempt(copy.dump());
    };
    auto& blocks = base["blocks"].items();
    auto& instances = base["instances"].items();
    REQUIRE(blocks.size() >= 4);
    REQUIRE(!instances.empty());
    std::string first = blocks[1]["id"].string(), second = blocks[2]["id"].string();
    structural([&](Json& w) { w["blocks"].items()[2].set("id", first); });
    structural([&](Json& w) { w["blocks"].items()[1].set("parent", "00000000-0000-4000-8000-00000000dead"); });
    structural([&](Json& w) {
        w["blocks"].items()[1].set("parent", second);
        w["blocks"].items()[2].set("parent", first);
    });
    structural([&](Json& w) { w["blocks"].items()[1].set("parent", first); });
    structural([&](Json& w) { w["instances"].items()[0].set("parent", w["instances"].items()[0]["id"].string()); });
    structural([&](Json& w) { w["instances"].items()[0].set("id", first); });
    structural([&](Json& w) {
        w["blocks"].items()[1].set("sizeX", 1e300);
        w["blocks"].items()[2].set("x", -1e300);
    });
    structural([&](Json& w) { w.set("campoSconosciuto", Json::object({{"annidato", Json::array({1, 2, 3})}})); });
    structural([&](Json& w) { w.set("schemaVersion", 999999); });
    structural([&](Json& w) { w.set("serverSize", -5); });
    double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    INFO("accettati " << accepted << ", rifiutati " << rejected << " in " << seconds << " s");
    CHECK(accepted > 0);
    CHECK(rejected > 0);
}
