#include "Engine/Client/Models.hpp"
#include "Engine/Core/LocalPreferences.hpp"
#include "Engine/Networking/Join.hpp"
#include "Interface/Canvas.hpp"
#include "Interface/ClientView.hpp"
#include "Interface/Surface.hpp"
#include "Localization/Localization.hpp"
#include "ServerHarness.hpp"
#include "Studio/StudioView.hpp"

#include <Catch2/catch_amalgamated.hpp>

using namespace ld;

namespace {

struct English {
    English() { localization::setLanguage(Language::english); }
    ~English() { localization::setLanguage(Language::italian); }
};

std::vector<std::string> leftovers(const ui::Surface& surface) {
    std::vector<std::string> found;
    auto check = [&](const std::string& text) {
        if (localization::untranslated(text)) found.push_back(text);
    };
    for (const auto& element : surface.elements()) check(element.label);
    for (const auto& field : surface.fields()) {
        check(field.prompt);
        for (const auto& option : field.options) check(option);
    }
    return found;
}

void paint(ui::Surface& surface, double width) {
    surface.invalidate();
    surface.resize({width, 760}, 1);
    ui::Canvas canvas(static_cast<int>(width), 760, 1);
    surface.render(canvas);
}

}

TEST_CASE("Localizzazione: formati e plurali", "[localization]") {
    CHECK(localization::parseLanguage("it") == std::optional<Language>(Language::italian));
    CHECK(localization::parseLanguage("it-IT") == std::optional<Language>(Language::italian));
    CHECK(localization::parseLanguage("en_GB") == std::optional<Language>(Language::english));
    CHECK_FALSE(localization::parseLanguage("fr"));
    CHECK_FALSE(localization::parseLanguage("italiano"));
    CHECK(localization::entries() > 1000);
    CHECK(tr("Salva") == "Salva");
    CHECK(tr("Annulla@@modifica") == "Annulla");
    CHECK(trn("{0} oggetto", "{0} oggetti", 1) == "1 oggetto");
    CHECK(trn("{0} oggetto", "{0} oggetti", 1200) == "1.200 oggetti");
    CHECK(localization::date("2026-09-20T10:00:00Z") == "20 set 2026");
    CHECK(localization::decimal(1234.5, 2) == "1.234,50");
    CHECK(tr("Lo script «{0}» supera {1} caratteri.", "Porta", 20000) == "Lo script «Porta» supera 20000 caratteri.");
    {
        English english;
        CHECK(localization::code() == "en");
        CHECK(tr("Salva") == "Save");
        CHECK(tr("Annulla") == "Cancel");
        CHECK(tr("Annulla@@modifica") == "Undo");
        CHECK(trn("{0} oggetto", "{0} oggetti", 1) == "1 object");
        CHECK(trn("{0} oggetto", "{0} oggetti", 1200) == "1,200 objects");
        CHECK(localization::date("2026-09-20") == "Sep 20, 2026");
        CHECK(localization::decimal(-1234.5, 1) == "-1,234.5");
        CHECK(tr("Una frase che non esiste nel catalogo") == "Una frase che non esiste nel catalogo");
        CHECK(localization::translateMessage("[ServerFull] Il server è pieno") == "[ServerFull] The server is full");
        CHECK(localization::translateMessage("Massimo 200 oggetti per esperienza") == "At most 200 objects per experience");
        CHECK(localization::translateMessage("[AssetMissing] Risorsa non caricata: abc.png") == "[AssetMissing] Resource not uploaded: abc.png");
        CHECK(localization::translateMessage("Messaggio libero del server") == "Messaggio libero del server");
        CHECK(failureMessage(JoinFailure::serverFull) == "Pick another server or press Play: we’ll put you where there’s room.");
        CHECK(failureTitle(JoinFailure::serverFull) == "Server full");
        CHECK(routeTitle(Route::experiences) == "Experiences");
        CHECK(localization::untranslated("Impostazioni"));
        CHECK_FALSE(localization::untranslated("Settings"));
        CHECK_FALSE(localization::untranslated("Studio"));
    }
    CHECK(tr("Salva") == "Salva");
}

TEST_CASE("Localizzazione: Ludissey in inglese", "[localization][interface][client]") {
    golden::registry();
    harness::Fixture backend;
    auto store = std::make_shared<Store>(std::make_shared<PlayFab>(std::make_shared<MemoryStore>()), backend.world->registerPlayer("Penglish", "Player"), nullptr, false);
    store->profile = PlayerProfile{"Player", "female", "2026-09-20T10:00:00Z"};
    store->avatar = AvatarConfiguration::standard("Player", "female");
    store->avatarDraft = store->avatar;
    Experience world;
    world.id = "7d0b6d2e-6f7a-4a52-9a53-2c1f1f6c0b11";
    world.title = "Sky Run";
    world.author = "Player";
    store->setWorlds({world});
    store->setDiscover({WorldSummary{world.id, world.title, "test", "Player", std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt}});
    store->friends = {{"online", "Friend", true, std::nullopt, "male"}};
    store->requests = {{"pending", "Newcomer", ""}};
    store->syncNotifications();
    ui::ClientView view(store);
    ui::Surface surface([&] { return view.build(); });
    view.invalidated = [&] { surface.invalidate(); };
    English english;
    std::vector<std::pair<std::string, std::function<void()>>> screens{
        {"home", [&] { store->navigate(Route::home); }},
        {"experiences", [&] { store->navigate(Route::experiences); }},
        {"details", [&] { store->showExperience(store->experienceCatalog().front()); }},
        {"servers", [&] { store->experienceTab = "Server"; }},
        {"avatar", [&] {
             store->detail.reset();
             store->navigate(Route::avatar);
         }},
        {"friends", [&] { store->navigate(Route::friends); }},
        {"settings", [&] { store->navigate(Route::settings); }},
        {"notifications", [&] {
             store->navigate(Route::home);
             store->notificationsOpen = true;
         }},
        {"login", [&] {
             store->notificationsOpen = false;
             store->profile.reset();
         }},
    };
    for (auto& [name, show] : screens) {
        show();
        for (double width : {860.0, 1180.0}) {
            INFO(name << " " << width);
            paint(surface, width);
            CHECK(leftovers(surface) == std::vector<std::string>{});
        }
    }
    CHECK(surface.contains("Sign In"));
}

TEST_CASE("Localizzazione: Ludissey Studio in inglese", "[localization][interface][studio]") {
    golden::registry();
    local_preferences::useMemory(true);
    harness::Fixture backend;
    auto store = std::make_shared<Store>(std::make_shared<PlayFab>(std::make_shared<MemoryStore>()), backend.world->registerPlayer("Pstudioen", "Creator"), nullptr, false);
    store->profile = PlayerProfile{"Creator", "male", "2026-09-20T10:00:00Z"};
    studio::StudioShell shell(store);
    ui::Surface surface([&] { return shell.build(); });
    shell.invalidated = [&] { surface.invalidate(); };
    English english;
    paint(surface, 1180);
    CHECK(leftovers(surface) == std::vector<std::string>{});
    REQUIRE(shell.document.open(Experience::blank()));
    REQUIRE(shell.document.addPart("cube"));
    auto script = shell.document.insert("Script", Experience::baseplate(shell.document.world.id).id);
    REQUIRE(script);
    std::vector<std::pair<std::string, std::function<void()>>> states{
        {"part", [] {}},
        {"output", [&] { shell.document.utilityCollapsed = false; shell.document.utilityTab = "output"; }},
        {"toolbox", [&] { shell.document.utilityTab = "toolbox"; }},
        {"terrain", [&] { shell.document.terrainEditing = true; shell.document.terrainMode = "generate"; }},
        {"script", [&] { shell.document.terrainEditing = false; shell.document.openScript(*script); }},
        {"lighting", [&] { shell.document.activeScript.reset(); shell.document.setSelection(StudioSelection::ofService("Lighting")); }},
        {"settings", [&] { shell.document.settingsOpen = true; }},
    };
    for (auto& [name, show] : states) {
        show();
        for (double width : {860.0, 1400.0}) {
            INFO(name << " " << width);
            paint(surface, width);
            CHECK(leftovers(surface) == std::vector<std::string>{});
        }
    }
    for (const auto& menu : {"command.more", "command.play.menu", "explorer.actions"}) {
        INFO(menu);
        shell.document.settingsOpen = false;
        paint(surface, 1400);
        auto items = surface.menu(menu);
        REQUIRE(items);
        for (const auto& item : *items) CHECK_FALSE(localization::untranslated(item.title));
    }
    CHECK(surface.contains("Properties"));
    local_preferences::useMemory(true);
}
