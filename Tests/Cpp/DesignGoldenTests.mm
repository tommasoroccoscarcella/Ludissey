#import <AppKit/AppKit.h>

#include "Engine/Client/JoinCoordinator.hpp"
#include "Engine/Core/Resources.hpp"
#include "Golden.hpp"
#include "Interface/ClientMedia.hpp"
#include "Interface/ClientView.hpp"
#include "Interface/Theme.hpp"
#include "Localization/Localization.hpp"
#include "Pixels.hpp"
#include "Platform/Mac/Platform.hpp"
#include "Sample.hpp"
#include "ServerHarness.hpp"
#include "Studio/StudioView.hpp"

#include <Catch2/catch_amalgamated.hpp>

#include <algorithm>
#include <cstdlib>
#include <fstream>

using namespace pixels;

namespace {

struct Camera {
    platform::Window& window;
    std::string title;
    int height;

    Pixels shoot(int width) {
        NSWindow* target = nil;
        NSString* wanted = [NSString stringWithUTF8String:title.c_str()];
        for (NSWindow* candidate in NSApp.windows)
            if ([candidate.title isEqualToString:wanted]) target = candidate;
        REQUIRE(target != nil);
        [target setContentSize:NSMakeSize(width, height)];
        window.invalidate();
        window.show();
        [[NSRunLoop mainRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.3]];
        window.show();
        [[NSRunLoop mainRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.05]];
        CGImageRef image = CGWindowListCreateImage(CGRectNull, kCGWindowListOptionIncludingWindow, static_cast<CGWindowID>(target.windowNumber), kCGWindowImageBoundsIgnoreFraming | kCGWindowImageBestResolution);
        REQUIRE(image != nullptr);
        double scale = target.backingScaleFactor;
        int w = static_cast<int>(width * scale), h = static_cast<int>(height * scale);
        Pixels actual = draw(image, w, h, static_cast<int>(CGImageGetHeight(image)) - h);
        CGImageRelease(image);
        return actual;
    }

    void check(const std::string& name, int width) {
        Pixels actual = shoot(width);
        auto reference = golden::root() / "Tests/Golden/Design" / (name + ".png");
        const char* mode = std::getenv("LUDISSEY_GOLDEN");
        if (mode && std::string(mode) == "write") {
            std::filesystem::create_directories(reference.parent_path());
            save(actual, reference);
        }
        if (const char* shots = std::getenv("LUDISSEY_SHOTS")) {
            std::filesystem::create_directories(shots);
            save(actual, std::filesystem::path(shots) / ("design " + name + ".png"));
        }
        INFO(name);
        REQUIRE(std::filesystem::exists(reference));
        Pixels heat;
        Difference d = compare(actual, load(reference), &heat);
        INFO("differenza media " << d.mean << ", pixel diversi " << d.changed * 100 << "%");
        WARN(name << ": media " << d.mean << " · diversi " << d.changed * 100 << "%");
        CHECK(d.mean < 0.25);
        CHECK(d.changed < 0.002);
    }
};

class RefusingProvider final : public ld::JoinSessionProvider {
public:
    ld::Async<ld::JoinTicket> requestSession(ld::ExperienceId, std::optional<ld::ServerId>) override {
        throw ld::JoinFailureError(ld::JoinFailure::serverFull);
        co_return ld::JoinTicket{};
    }
    ld::Async<void> release(ld::JoinTicket) override { co_return; }
};

}

TEST_CASE("Golden: schermate di Ludissey Studio", "[platform][design][screens][studio]") {
    @autoreleasepool {
        [NSApplication sharedApplication];
        platform::installAdapters();
        golden::registry();
        harness::Fixture fixture;
        auto store = std::make_shared<ld::Store>(std::make_shared<ld::PlayFab>(std::make_shared<ld::MemoryStore>()), fixture.world->registerPlayer("Pdesign", sample::player), nullptr, false);
        store->profile = ld::PlayerProfile{sample::player, "female", "2026-09-20T10:00:00Z"};
        studio::StudioShell studio(store);
        ld::Experience project;
        project.id = "5f0c2d8e-1b7a-4c52-9d13-2a4f6b8c0e21";
        project.title = sample::games().front().title;
        project.genre = sample::games().front().genre;
        REQUIRE(studio.document.open(project));
        auto& document = studio.document;
        for (const char* kind : {"cube", "sphere", "cylinder"}) document.addPart(kind);
        std::string cube = document.world.blocks[0].id;
        document.update(cube, [](ld::WorldBlock& b) {
            b.x = 0;
            b.y = 0.5;
            b.z = 0;
        });
        auto script = document.insert("Script", cube);
        REQUIRE(script);
        document.setSource(*script, "let part = script.parent\nif part is BasePart {\n    part.color = Color(224 / 255, 57 / 255, 43 / 255)\n    print(\"pronto\")\n    await Task.wait(0.1)\n}\n");
        document.expandedIds.clear();
        document.openScripts.clear();
        document.activeScript.reset();
        document.tool = ld::StudioTool::move;
        document.setSelection(std::nullopt);
        document.output.clear();
        document.markSaved();
        platform::Window window({"Verifica design Studio", 1280, 800, 860, 680}, [&] { return studio.build(); });
        studio.invalidated = [&] { window.invalidate(); };
        window.rendered = [&](const auto& surface) { studio.rendered(surface); };
        Camera camera{window, "Verifica design Studio", 800};

        camera.check("studio predefinito", 1280);
        camera.check("studio predefinito 920", 920);
        camera.check("studio predefinito 860", 860);

        document.select({cube});
        camera.check("studio part selezionata", 1280);
        camera.check("studio part selezionata 920", 920);

        for (auto [text, kind] : std::vector<std::pair<std::string, ld::OutputLine::Kind>>{{"Play · 3 oggetti", ld::OutputLine::Kind::info}, {"Attenzione: Part senza Anchored", ld::OutputLine::Kind::warning}, {"Errore: riga 3, valore nil", ld::OutputLine::Kind::error}, {"Stop", ld::OutputLine::Kind::info}}) {
            ld::OutputLine line;
            line.text = text;
            line.kind = kind;
            line.time = 1790000000;
            document.output.push_back(line);
        }
        camera.check("studio output avvisi ed errori", 1280);

        document.utilityTab = "output";
        document.utilityCollapsed = false;
        camera.check("studio scheda output", 1280);
        camera.check("studio scheda output 920", 920);

        document.utilityCollapsed = true;
        document.terrainEditing = true;
        document.dockTab = "terrain";
        document.terrainTool = ld::TerrainTool::add;
        camera.check("studio terreno", 1280);
        camera.check("studio terreno 920", 920);

        document.terrainTool.reset();
        document.terrainMode = "generate";
        camera.check("studio terreno genera", 1280);

        document.terrainMode = "sea";
        document.terrainRegion = ld::TerrainRegion::snapped({-12, -16, -12}, {12, 0, 12});
        camera.check("studio terreno livello del mare", 1280);

        document.closeTerrainTool();
        document.terrainTab = "region";
        document.regionEditing = true;
        camera.check("studio terreno regione", 1280);

        document.closeTerrainTool();
        document.terrainTab = "create";
        document.terrainEditing = false;
        document.perform(ld::StudioEdit::sea(ld::TerrainRegion::snapped({-12, -16, -12}, {12, 0, 12})));
        document.expandedServices.insert("Workspace");
        document.setSelection(ld::StudioSelection::ofService("Terrain"));
        camera.check("studio proprieta acqua", 1280);
        document.run(ld::StudioCommand::undo);
        document.openScript(*script);
        camera.check("studio editor di script", 1280);
        camera.check("studio editor di script 920", 920);

        document.activeScript.reset();
        document.select({cube});
        document.settingsOpen = true;
        camera.check("studio impostazioni", 1280);
        camera.check("studio impostazioni 920", 920);

        document.settingsOpen = false;
        studio.togglePanel("explorer");
        studio.togglePanel("properties");
        camera.check("studio pannelli nascosti", 1280);
        studio.resetLayout();

        ld::localization::setLanguage(ld::Language::english);
        document.select({cube});
        document.utilityTab = "output";
        document.utilityCollapsed = false;
        camera.check("studio part selezionata inglese", 1280);
        document.utilityCollapsed = true;
        ld::localization::setLanguage(ld::Language::italian);

        document.settingsOpen = false;
        document.configure(std::string("Progetto modificato"), std::nullopt, std::nullopt);
        window.invalidate();
        window.show();
        [[NSRunLoop mainRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.1]];
        auto more = window.surface().menu("command.more");
        REQUIRE(more);
        bool closed = false;
        for (const auto& item : *more)
            if (item.title == "Chiudi progetto") {
                item.action();
                closed = true;
            }
        REQUIRE(closed);
        camera.check("studio dialogo di chiusura", 1280);

        window.invalidate();
        studio.invalidated = [&] { window.invalidate(); };
        ld::localization::setLanguage(ld::Language::english);
        camera.check("studio dialogo di chiusura inglese", 1280);
        ld::localization::setLanguage(ld::Language::italian);

        studio.invalidated = nullptr;
    }
}

TEST_CASE("Golden: schermate di Ludissey", "[platform][design][screens]") {
    @autoreleasepool {
        [NSApplication sharedApplication];
        platform::installAdapters();
        golden::registry();
        harness::Fixture fixture;
        auto store = std::make_shared<ld::Store>(std::make_shared<ld::PlayFab>(std::make_shared<ld::MemoryStore>()), fixture.world->registerPlayer("Pdesign", sample::player), nullptr, false);
        sample::populate(*store);
        store->friends = sample::friends();
        ld::Experience world = sample::own();
        ui::ClientView screen(store);
        ui::ClientMedia media(store);
        screen.portrait = [&](const std::string& name) { return media.portrait(name); };
        screen.experienceIcon = [&](const std::string& id) { return media.icon(id); };
        screen.figure = [&](const std::string& id) { return media.figure(id); };
        platform::Window window({"Verifica design client", 1180, 760, 860, 680}, [&] { return screen.build(); });
        window.views = [&](const auto& slots) { return media.views(slots); };
        screen.invalidated = [&] { window.invalidate(); };
        store->changed = [&] { window.invalidate(); };
        Camera camera{window, "Verifica design client", 760};

        store->navigate(ld::Route::home);
        camera.check("client home", 1180);
        camera.check("client home 860", 860);
        camera.check("client home 1400", 1400);

        auto catalog = store->experienceCatalog();
        std::stable_partition(catalog.begin(), catalog.end(), [&](const auto& entry) { return entry.id == world.id; });
        REQUIRE_FALSE(catalog.empty());
        store->showExperience(catalog.front());
        harness::waitUntil([&] { return store->details.count(world.id) > 0 || store->detailsError.count(world.id) > 0; }, 5);
        camera.check("client dettagli esperienza", 1180);
        camera.check("client dettagli esperienza 860", 860);
        store->experienceTab = "Server";
        camera.check("client server", 1180);
        store->experienceTab = "Informazioni";

        store->join->provider = std::make_shared<RefusingProvider>();
        store->play(catalog.front());
        REQUIRE(harness::waitUntil([&] {
            [[NSRunLoop mainRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.02]];
            return store->join->failure().has_value();
        }));
        camera.check("client accesso al server rifiutato", 1180);
        store->join->cancel();

        store->detail.reset();
        store->navigate(ld::Route::avatar);
        store->avatarCollection.catalog.clear();
        camera.check("client avatar e catalogo vuoto", 1180);
        camera.check("client avatar 920", 920);

        store->navigate(ld::Route::settings);
        camera.check("client impostazioni", 1180);
        camera.check("client impostazioni 1400", 1400);

        store->navigate(ld::Route::friends);
        camera.check("client amici", 1180);

        store->navigate(ld::Route::home);
        store->notificationsOpen = true;
        camera.check("client notifiche aperte", 1180);
        store->notificationsOpen = false;

        store->error = "Connessione non riuscita. Controlla la rete e riprova.";
        camera.check("client errore", 1180);
        store->error.reset();

        auto people = sample::friends();
        std::vector<ld::WorldSummary> made;
        for (const auto& entry : store->experienceCatalog())
            if (entry.published && entry.categories.count(ld::ExperienceScope::mine)) made.push_back({entry.id, entry.title, "Pdesign", entry.author, entry.genre, entry.serverSize, entry.createdAt, entry.updatedAt, entry.icon, entry.thumbnail});
        auto profileShot = [&](const std::string& name, ld::PlayerPage page) {
            store->navigate(ld::Route::home);
            store->profiles[page.id] = page;
            store->route = ld::Route::profile;
            store->profileId = page.id;
            store->profileName = page.username;
            store->history = {ld::PageState{}};
            camera.check(name, 1180);
            camera.check(name + " 860", 860);
            store->navigate(ld::Route::home);
        };
        ld::PlayerPage own;
        own.id = "Pdesign";
        own.username = sample::player;
        own.gender = "female";
        own.createdAt = "2026-09-20T10:00:00Z";
        own.self = true;
        own.publicProfile = true;
        own.online = true;
        own.friends = static_cast<int>(people.size());
        own.bio = "Costruisco piste e torri. Il sabato provo le esperienze degli amici.";
        own.friendList = std::vector<ld::Friend>(people.begin(), people.begin() + std::min<std::size_t>(people.size(), 6));
        own.experiences = made;
        profileShot("client profilo personale", own);
        ld::PlayerPage friendly;
        friendly.id = people.front().id;
        friendly.username = people.front().username;
        friendly.gender = people.front().gender;
        friendly.createdAt = "2026-08-02T09:30:00Z";
        friendly.isFriend = true;
        friendly.online = people.front().online;
        friendly.friends = 2;
        friendly.bio = "Mi piacciono le corse e i puzzle.";
        friendly.friendList = std::vector<ld::Friend>{{own.id, own.username, true, std::nullopt, own.gender}, people.back()};
        profileShot("client profilo amico", friendly);
        ld::PlayerPage hidden;
        hidden.id = "Psconosciuto";
        hidden.username = "Teodoro";
        hidden.createdAt = "2026-10-01T18:00:00Z";
        hidden.friends = 5;
        profileShot("client profilo privato", hidden);

        ld::localization::setLanguage(ld::Language::english);
        camera.check("client home inglese", 1180);
        store->showExperience(catalog.front());
        camera.check("client dettagli esperienza inglese 860", 860);
        store->detail.reset();
        ld::localization::setLanguage(ld::Language::italian);

        store->changed = nullptr;
    }
}

TEST_CASE("Ritratti: busto e figura per entrambi i volti", "[platform][design][face]") {
    @autoreleasepool {
        [NSApplication sharedApplication];
        platform::installAdapters();
        ld::setResourceFolder(golden::root() / "Resources");
        auto store = std::make_shared<ld::Store>(std::make_shared<ld::PlayFab>(std::make_shared<ld::MemoryStore>()), nullptr, nullptr, false);
        ui::ClientMedia media(store);
        std::map<std::string, std::string> busts;
        for (const char* body : {"male", "female"}) {
            auto images = media.portraitImages(ld::AvatarConfiguration::standard("", body));
            REQUIRE(images);
            CHECK(images->bust.size() > 1000);
            CHECK(images->figure.size() > 1000);
            busts[body] = images->bust;
            auto standard = golden::root() / "Resources/Portraits" / (std::string(body) == "male" ? "Male.png" : "Female.png");
            if (golden::writing()) {
                std::filesystem::create_directories(standard.parent_path());
                std::ofstream(standard, std::ios::binary) << images->bust;
            }
            {
                std::ofstream(std::filesystem::temp_directory_path() / "ludissey-ritratto.png", std::ios::binary) << images->bust;
                auto fresh = pixels::load(std::filesystem::temp_directory_path() / "ludissey-ritratto.png");
                auto stored = pixels::load(standard);
                REQUIRE(fresh.width == stored.width);
                REQUIRE(fresh.height == stored.height);
                double total = 0;
                for (std::size_t i = 0; i < fresh.rgba.size(); ++i) total += std::abs(static_cast<int>(fresh.rgba[i]) - static_cast<int>(stored.rgba[i]));
                CHECK(total / fresh.rgba.size() < 2.0);
            }
            if (const char* shots = std::getenv("LUDISSEY_SHOTS")) {
                std::string name = std::string(body) == "male" ? "maschile" : "femminile";
                std::ofstream(std::filesystem::path(shots) / ("ritratto busto " + name + ".png"), std::ios::binary) << images->bust;
                std::ofstream(std::filesystem::path(shots) / ("ritratto figura " + name + ".png"), std::ios::binary) << images->figure;
            }
        }
        CHECK(busts["male"] != busts["female"]);
    }
}
