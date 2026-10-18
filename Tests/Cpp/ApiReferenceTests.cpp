#include "Engine/Core/Rules.hpp"
#include "Engine/Scripting/EngineTypes.hpp"
#include "Golden.hpp"
#include "Lim/Tooling/Reference.hpp"

#include <Catch2/catch_amalgamated.hpp>

#include <algorithm>

using namespace lim::tooling;

TEST_CASE("API Lim: catalogo e ricerca per pertinenza", "[api-reference]") {
    golden::registry();
    const auto& registry = ld::EngineTypes::shared().registry();
    auto catalog = apiCatalog(registry);
    std::size_t members = 0;
    for (const auto& entry : registry.classes()) {
        const auto& type = entry.second;
        members += type.statics.size() + type.constants.size() + (type.constructor ? 1 : 0);
        for (const auto& property : type.properties) members += type.base.empty() || !registry.property(type.base, property.first);
        for (const auto& method : type.methods) members += type.base.empty() || !registry.method(type.base, method.first);
        for (const auto& event : type.events) members += type.base.empty() || !registry.event(type.base, event.first);
        CHECK(std::any_of(catalog.begin(), catalog.end(), [&](const ApiEntry& e) { return e.kind == ApiKind::classType && e.name == entry.first; }));
    }
    CHECK(catalog.size() == members + registry.classes().size() + registry.globals().size() + registry.functions().size() + registry.enums().size());
    auto find = [&](std::string_view owner, std::string_view name) -> const ApiEntry* {
        for (const auto& entry : catalog)
            if (entry.owner == owner && entry.name == name) return &entry;
        return nullptr;
    };
    const ApiEntry* fire = find("RemoteEvent", "fireServer");
    REQUIRE(fire);
    CHECK(fire->deprecated);
    CHECK(fire->replacement == "send");
    const ApiEntry* send = find("RemoteEvent", "send");
    REQUIRE(send);
    CHECK_FALSE(send->deprecated);
    CHECK(send->signature.rfind("fn send(", 0) == 0);
    const ApiEntry* clicked = find("GuiButton", "clicked");
    if (!clicked) clicked = find("TextButton", "clicked");
    REQUIRE(clicked);
    CHECK(clicked->kind == ApiKind::event);
    CHECK(clicked->signature.rfind("on clicked", 0) == 0);
    auto results = searchApi(catalog, "send", 10);
    REQUIRE_FALSE(results.empty());
    CHECK(results.front()->name == "send");
    auto deprecatedLast = searchApi(catalog, "fire", 50);
    REQUIRE_FALSE(deprecatedLast.empty());
    bool seenDeprecated = false;
    for (const auto* entry : deprecatedLast) {
        if (entry->deprecated) seenDeprecated = true;
        else CHECK_FALSE(seenDeprecated);
    }
    CHECK(searchApi(catalog, "zzz-nessuna-api", 10).empty());
    CHECK(searchApi(catalog, "part", 5).size() == 5);
    CHECK(std::any_of(catalog.begin(), catalog.end(), [](const ApiEntry& e) { return e.kind == ApiKind::classType && e.name == "Part"; }));
    CHECK(std::any_of(catalog.begin(), catalog.end(), [](const ApiEntry& e) { return e.deprecated && e.replacement == "send"; }));
    CHECK(std::any_of(catalog.begin(), catalog.end(), [](const ApiEntry& e) { return e.kind == ApiKind::global && e.name == "Input"; }));
    CHECK(find("Part", "anchored") == nullptr);
    CHECK(find("BasePart", "anchored") != nullptr);
    for (const auto& entry : catalog)
        if (entry.kind == ApiKind::property) {
            const auto* type = registry.findClass(entry.owner);
            REQUIRE(type);
            CHECK((type->base.empty() || !registry.property(type->base, entry.name)));
        }
}
