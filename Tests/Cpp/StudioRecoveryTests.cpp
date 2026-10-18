#include "Engine/Studio/Recovery.hpp"
#include "Interface/Canvas.hpp"
#include "Interface/Surface.hpp"
#include "ServerHarness.hpp"
#include "Studio/StudioView.hpp"

#include <Catch2/catch_amalgamated.hpp>

#include <fstream>
#include <sys/stat.h>

using namespace ld;

namespace {

struct RecoveryFolder {
    std::filesystem::path path = std::filesystem::temp_directory_path() / ("Ludissey Recupero " + makeUuid());
    RecoveryFolder() { studio_recovery::setFolder(path); }
    ~RecoveryFolder() {
        studio_recovery::setFolder(std::nullopt);
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

struct Creator {
    harness::Fixture backend;
    std::shared_ptr<Store> store = std::make_shared<Store>(std::make_shared<PlayFab>(std::make_shared<MemoryStore>()), backend.world->registerPlayer("Pcreatrice", "Creatrice"), nullptr, false);
    std::unique_ptr<studio::StudioShell> shell;
    std::unique_ptr<ui::Surface> surface;
    int quits = 0;
    double clock = 1000;

    Creator() {
        golden::registry();
        store->profile = PlayerProfile{"Creatrice", "female", "2026-10-08T10:00:00Z"};
        build();
    }

    void build() {
        shell = std::make_unique<studio::StudioShell>(store);
        shell->quit = [this] { ++quits; };
        surface = std::make_unique<ui::Surface>([this] { return shell->build(); });
        surface->resize({1180, 760}, 1);
        draw();
    }

    void draw() {
        ui::Canvas canvas(1180, 760, 1);
        surface->render(canvas);
    }

    void tick(double seconds) {
        clock += seconds;
        shell->tick(clock, 1.0 / 60);
    }

    void settle() {
        REQUIRE(harness::waitUntil([this] { return !store->busy; }, 20));
        tick(0.1);
        draw();
    }

    bool tap(const std::string& id) {
        draw();
        bool tapped = surface->tap(id);
        draw();
        return tapped;
    }

    std::string part() {
        REQUIRE(shell->document.addPart("cube"));
        return shell->document.selectedIds().front();
    }
};

}

TEST_CASE("Studio: copie di recupero", "[studio][recovery]") {
    RecoveryFolder folder;
    Experience world = Experience::blank();
    world.title = "Da salvare";
    WorldBlock extra;
    extra.id = makeUuid();
    extra.name = "Extra";
    world.blocks.push_back(extra);
    REQUIRE(studio_recovery::write(world, "2026-10-08 10:00:00"));
    CHECK(studio_recovery::exists(world.id));
    struct stat info {};
    REQUIRE(::stat((folder.path / (world.id + ".json")).c_str(), &info) == 0);
    CHECK((info.st_mode & 0777) == 0600);
    auto listed = studio_recovery::list();
    REQUIRE(listed.size() == 1);
    CHECK(listed[0].id == world.id);
    CHECK(listed[0].title == "Da salvare");
    CHECK(listed[0].parts == 2);
    auto loaded = studio_recovery::load(world.id);
    REQUIRE(loaded);
    CHECK(*loaded == world);
    std::string other = makeUuid();
    std::ofstream(folder.path / (other + ".json")) << "{\"savedAt\":\"x\",\"experience\":{\"id\":";
    std::string wrong = makeUuid();
    std::ofstream(folder.path / (wrong + ".json")) << Json::object({{"savedAt", "x"}, {"experience", world.json()}}).dump();
    std::ofstream(folder.path / (world.id + ".json.nuova")) << "parziale";
    std::ofstream(folder.path / "../../fuori.json") << "{}";
    listed = studio_recovery::list();
    REQUIRE(listed.size() == 1);
    CHECK(listed[0].id == world.id);
    CHECK_FALSE(std::filesystem::exists(folder.path / (other + ".json")));
    CHECK_FALSE(std::filesystem::exists(folder.path / (wrong + ".json")));
    CHECK_FALSE(std::filesystem::exists(folder.path / (world.id + ".json.nuova")));
    CHECK_FALSE(studio_recovery::load("../../fuori"));
    Experience escaping = world;
    escaping.id = "../../fuori";
    CHECK_FALSE(studio_recovery::write(escaping, "x"));
    studio_recovery::discard(world.id);
    CHECK_FALSE(studio_recovery::exists(world.id));
    CHECK(studio_recovery::list().empty());
}

TEST_CASE("Studio: chiusura con modifiche", "[studio][recovery][quit]") {
    RecoveryFolder folder;
    Creator c;
    CHECK(c.shell->requestQuit());
    c.shell->newProject();
    REQUIRE(c.shell->document.opened);
    CHECK(c.shell->requestQuit());
    std::string first = c.part();
    REQUIRE(c.shell->document.dirty());
    CHECK_FALSE(c.shell->requestQuit());
    c.draw();
    REQUIRE(c.surface->find("dialog"));
    CHECK(c.tap("dialog.button.0"));
    CHECK_FALSE(c.surface->find("dialog"));
    CHECK(c.quits == 0);
    CHECK(c.shell->document.opened);
    CHECK(c.shell->document.world.block(first));

    CHECK_FALSE(c.shell->requestQuit());
    CHECK(c.tap("dialog.button.1"));
    CHECK(c.quits == 1);
    CHECK_FALSE(c.shell->document.opened);

    c.shell->newProject();
    std::string second = c.part();
    std::string id = c.shell->document.world.id;
    CHECK_FALSE(c.shell->requestQuit());
    CHECK(c.tap("dialog.button.2"));
    c.settle();
    CHECK(c.quits == 2);
    CHECK_FALSE(c.store->error);
    bool stored = false;
    for (const auto& world : c.store->worlds())
        if (world.id == id) stored = world.block(second) != nullptr;
    CHECK(stored);

    c.shell->newProject();
    c.part();
    c.shell->requestClose();
    c.draw();
    REQUIRE(c.surface->find("dialog"));
    CHECK(c.tap("dialog.button.1"));
    CHECK_FALSE(c.shell->document.opened);
    CHECK(c.quits == 2);

    c.shell->newProject();
    c.shell->requestClose();
    CHECK_FALSE(c.shell->document.opened);
}

TEST_CASE("Studio: salvataggio automatico di recupero", "[studio][recovery][autosave]") {
    RecoveryFolder folder;
    Creator c;
    c.shell->newProject();
    std::string id = c.shell->document.world.id;
    c.tick(1);
    CHECK_FALSE(studio_recovery::exists(id));
    c.part();
    c.tick(1);
    REQUIRE(studio_recovery::exists(id));
    CHECK(studio_recovery::load(id)->blocks.size() == 2);
    c.part();
    c.tick(10);
    CHECK(studio_recovery::load(id)->blocks.size() == 2);
    c.tick(25);
    CHECK(studio_recovery::load(id)->blocks.size() == 3);
    c.shell->save(false);
    c.settle();
    REQUIRE_FALSE(c.shell->document.dirty());
    c.tick(1);
    CHECK_FALSE(studio_recovery::exists(id));

    std::string lost = c.part();
    c.tick(31);
    REQUIRE(studio_recovery::exists(id));
    c.build();
    c.store->navigate(Route::experiences, ExperienceScope::mine);
    c.draw();
    REQUIRE(c.surface->find("recovery." + id));
    CHECK(c.tap("recovery.open." + id));
    REQUIRE(c.shell->document.opened);
    CHECK(c.shell->document.world.id == id);
    CHECK(c.shell->document.world.block(lost));
    CHECK(c.shell->document.dirty());
    c.shell->save(false);
    c.settle();
    CHECK_FALSE(c.shell->document.dirty());
    c.tick(1);
    CHECK_FALSE(studio_recovery::exists(id));

    c.part();
    c.tick(31);
    c.build();
    c.draw();
    REQUIRE(c.surface->find("recovery." + id));
    CHECK(c.tap("recovery.discard." + id));
    CHECK_FALSE(studio_recovery::exists(id));
    CHECK_FALSE(c.surface->find("recovery." + id));
}

TEST_CASE("Studio: Salva come", "[studio][recovery][saveas]") {
    RecoveryFolder folder;
    Creator c;
    c.shell->newProject();
    std::string original = c.shell->document.world.id;
    std::string plate = Experience::baseplate(original).id;
    auto script = c.shell->document.insert("Script", plate);
    REQUIRE(script);
    c.shell->save(false);
    c.settle();
    REQUIRE_FALSE(c.shell->document.dirty());
    Experience before = c.shell->document.world;
    c.shell->saveAs();
    c.settle();
    const auto& copy = c.shell->document.world;
    CHECK(copy.id != original);
    CHECK(copy.title == "Copia di " + before.title);
    CHECK(copy.revision > 0);
    CHECK_FALSE(c.shell->document.dirty());
    std::string newPlate = Experience::baseplate(copy.id).id;
    CHECK(copy.block(newPlate));
    CHECK_FALSE(copy.block(plate));
    const InstanceRecord* moved = copy.record(*script);
    REQUIRE(moved);
    CHECK(moved->parent == newPlate);
    int found = 0;
    for (const auto& world : c.store->worlds())
        if (world.id == original || world.id == copy.id) ++found;
    CHECK(found == 2);
    for (const auto& world : c.store->worlds())
        if (world.id == original) CHECK(world.block(plate));
}

TEST_CASE("Studio: pubblicazione e visibilità", "[studio][publish][visibility]") {
    RecoveryFolder folder;
    Creator c;
    bool edited = false;
    c.shell->setEdited = [&](bool value) { edited = value; };
    auto saveEnabled = [&] {
        c.draw();
        CHECK_FALSE(c.surface->find("studio.publishState"));
        REQUIRE(c.surface->find("studio.save"));
        return c.surface->find("studio.save")->enabled;
    };
    auto published = [&] {
        c.draw();
        auto found = c.store->details.find(c.shell->document.world.id);
        return found != c.store->details.end() && found->second.experience.published;
    };
    c.shell->newProject();
    c.part();
    CHECK(saveEnabled());
    CHECK(edited);
    c.shell->save(false);
    c.settle();
    CHECK_FALSE(saveEnabled());
    CHECK_FALSE(edited);
    std::string id = c.shell->document.world.id;
    auto visitor = c.backend.world->registerPlayer("Pvisitatore", "Visitatore");
    auto listedBefore = c.backend.world->invoke("Pvisitatore", "LudisseyDiscover")["worlds"].size();
    CHECK(harness::refused("ExperienceUnavailable", [&] { c.backend.world->invoke("Pvisitatore", "LudisseyExperience", Json::object({{"id", id}})); }));
    c.shell->save(true);
    c.draw();
    CHECK(c.surface->find("studio.publish")->label == "Pubblico…");
    CHECK_FALSE(c.surface->find("studio.publish")->enabled);
    c.settle();
    CHECK(c.surface->find("studio.publish")->label == "Pubblica");
    CHECK(published());
    CHECK_NOTHROW(c.backend.world->invoke("Pvisitatore", "LudisseyExperience", Json::object({{"id", id}})));
    c.part();
    c.shell->save(false);
    c.settle();
    CHECK_FALSE(saveEnabled());
    c.shell->openSettings();
    c.draw();
    REQUIRE(c.surface->find("settings.unpublish"));
    CHECK(c.tap("settings.unpublish"));
    REQUIRE(c.store->dialog);
    REQUIRE(c.store->dialog->buttons.size() == 2);
    auto confirm = c.store->dialog->buttons[1].action;
    c.store->dialog.reset();
    confirm();
    c.settle();
    INFO(c.store->error.value_or(""));
    CHECK_FALSE(c.store->error);
    CHECK_FALSE(published());
    CHECK(c.surface->find("settings.publish"));
    CHECK(harness::refused("ExperienceUnavailable", [&] { c.backend.world->invoke("Pvisitatore", "LudisseyExperience", Json::object({{"id", id}})); }));
    CHECK(c.backend.world->invoke("Pvisitatore", "LudisseyDiscover")["worlds"].size() == listedBefore);
    CHECK(c.tap("settings.publish"));
    c.settle();
    CHECK(published());
    CHECK(c.backend.world->invoke("Pvisitatore", "LudisseyDiscover")["worlds"].size() == listedBefore + 1);
}
