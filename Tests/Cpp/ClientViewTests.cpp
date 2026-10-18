#include "Engine/Audio/Audio.hpp"
#include "Engine/Backend/Assets.hpp"
#include "Engine/Core/LocalPreferences.hpp"
#include "Engine/Core/Png.hpp"
#include "Engine/Rendering/Quality.hpp"
#include "Interface/ClientMedia.hpp"
#include "Interface/ClientView.hpp"
#include "Interface/Surface.hpp"
#include "Localization/Localization.hpp"
#include "ServerHarness.hpp"

using namespace ld;

namespace {
struct ClientFixture {
    harness::Fixture backend;
    std::shared_ptr<Store> store = std::make_shared<Store>(std::make_shared<PlayFab>(std::make_shared<MemoryStore>()), backend.world->registerPlayer("Pscreen", "Giocatrice"), nullptr, false);
    ui::ClientView view{store};
    ui::Surface surface{[this] { return view.build(); }};
    ClientFixture() {
        golden::registry();
        store->profile = PlayerProfile{"Giocatrice", "female", "2026-09-20T10:00:00Z"};
        store->avatar = AvatarConfiguration::standard("Giocatrice", "female");
        store->avatarDraft = store->avatar;
        store->changed = [this] { surface.invalidate(); };
        view.invalidated = [this] { surface.invalidate(); };
        surface.resize({1180, 760}, 1);
    }
    ~ClientFixture() { store->changed = nullptr; }
    void draw(int width = 1180, const std::string& shot = "") {
        surface.resize({static_cast<double>(width), 760}, 1);
        ui::Canvas canvas(width, 760, 1);
        surface.render(canvas);
        if (const char* folder = std::getenv("LUDISSEY_SHOTS"); folder && !shot.empty()) {
            std::filesystem::create_directories(folder);
            REQUIRE(canvas.writePng(std::filesystem::path(folder) / shot));
        }
    }
    void edit(const std::string& id, const std::string& value) {
        auto f = surface.field(id);
        REQUIRE(f);
        REQUIRE(f->changed);
        auto changed = f->changed;
        changed(value);
        draw();
    }
};
}

TEST_CASE("Client: barra di navigazione e destinazioni", "[interface][client]") {
    ClientFixture f;
    f.store->profile->username = "UnNomeUtenteMoltoLungoPerVerificareLaNavigazione";
    f.store->requests = {{"Pfriend", "Leo", ""}};
    for (int width : {860, 920, 1180, 1400}) {
        CAPTURE(width);
        f.store->navigate(Route::home);
        f.draw(width, "client-home-" + std::to_string(width) + ".png");
        auto bar = f.surface.find("topNavigation");
        REQUIRE(bar);
        CHECK(bar->frame.height == ui::theme::metrics::navigationBar);
        CHECK(bar->frame.width == width);
        double previous = 0;
        for (const char* id : {"topNavigation.logo", "topNavigation.home", "topNavigation.experiences", "topNavigation.avatar", "topNavigation.friends", "topNavigation.create", "utility.notifications", "topNavigation.settings", "topNavigation.profile"}) {
            CAPTURE(id);
            auto item = f.surface.find(id);
            REQUIRE(item);
            CHECK(item->frame.x >= previous);
            CHECK(item->frame.maxX() <= width);
            CHECK(item->frame.maxY() <= ui::theme::metrics::navigationBar);
            previous = item->frame.maxX();
        }
        CHECK(f.surface.find("topNavigation.home")->label == "Home");
        CHECK(f.surface.find("topNavigation.profile")->label == "UnNomeUtenteMoltoLungoPerVerificareLaNavigazione");
        CHECK(f.surface.find("topNavigation.profile")->frame.width <= 220);
        CHECK(f.surface.find("utility.notifications")->frame.width == ui::theme::iconSize::bar + 2 * ui::theme::spacing::s4);
        CHECK(f.surface.find("topNavigation.settings")->frame.height == ui::theme::metrics::barButton);
        CHECK_FALSE(f.surface.find("utility.friends"));
        CHECK_FALSE(f.surface.find("shell.pullToRefresh"));
        auto search = f.surface.find("utility.searchField");
        if (width >= 980) {
            REQUIRE(search);
            CHECK(search->frame.height == 28);
            CHECK(search->frame.width == (width >= 1180 ? 260 : 200));
            CHECK(search->frame.maxX() <= f.surface.find("utility.notifications")->frame.x);
            auto field = f.surface.field("utility.search");
            REQUIRE(field);
            CHECK(field->prompt == "Cerca esperienze");
        } else {
            CHECK_FALSE(search);
            REQUIRE(f.surface.tap("utility.searchToggle"));
            f.draw(width);
            REQUIRE(f.surface.find("utility.searchField"));
            REQUIRE(f.surface.tap("utility.searchToggle"));
        }
        CHECK(f.surface.tap("topNavigation.experiences"));
        f.draw(width);
        CHECK(f.store->route == Route::experiences);
        CHECK(f.surface.find("topNavigation.experiences")->frame.height == ui::theme::metrics::barButton);
    }
    f.draw(1180);
    REQUIRE(f.surface.tap("topNavigation.settings"));
    f.draw(1180);
    REQUIRE(f.surface.find("account.menu"));
    CHECK(f.surface.find("account.menu.settings")->frame.height == ui::theme::metrics::listRow);
    REQUIRE(f.surface.tap("account.menu.settings"));
    f.draw(1180);
    CHECK(f.store->route == Route::settings);
    CHECK_FALSE(f.surface.find("account.menu"));
    bool launched = false;
    f.store->launchStudio = [&](std::optional<std::string> project) { launched = !project.has_value(); };
    REQUIRE(f.surface.tap("topNavigation.create"));
    CHECK(launched);
}

TEST_CASE("Client: validazione dell'accesso", "[interface][client]") {
    ClientFixture f;
    f.store->profile.reset();
    f.draw(1180, "client-login.png");
    CHECK(f.surface.find("login.bar")->frame.height == ui::theme::metrics::loginBar);
    CHECK(f.surface.find("login.card")->frame.width == 400);
    CHECK(f.surface.find("login.card")->frame.x > f.surface.find("login.brand")->frame.maxX());
    CHECK(f.surface.find("login.username")->frame.width == 160 - 16);
    CHECK(f.surface.find("login.forgot")->frame.maxY() <= ui::theme::metrics::loginBar);
    CHECK_FALSE(f.surface.find("login.mode.1"));
    std::vector<std::string> focused;
    f.view.focusField = [&](const std::string& id) { focused.push_back(id); };
    REQUIRE(f.surface.tap("login.submit"));
    f.draw();
    CHECK(f.surface.contains("Il nome utente deve avere da 3 a 20 caratteri."));
    CHECK(focused == std::vector<std::string>{"login.username"});
    REQUIRE(f.surface.tap("register.submit"));
    f.draw();
    CHECK(f.surface.find("login.error"));
    CHECK(focused.back() == "register.username");
    f.edit("register.username", "NuovaGiocatrice");
    REQUIRE(f.surface.tap("register.submit"));
    f.draw();
    CHECK(f.surface.contains("La password deve avere almeno 6 caratteri."));
    CHECK(focused.back() == "register.password");
    f.store->error.reset();
    CHECK(f.surface.contains("Registrati e gioca"));
    f.edit("register.username", "NuovaGiocatrice");
    f.edit("register.password", "secret-password");
    f.edit("register.confirm", "different-password");
    REQUIRE(f.surface.tap("register.gender.1"));
    f.draw();
    CHECK(f.surface.find("register.gender.1")->label == "Femminile");
    CHECK(f.surface.find("register.submit")->frame.height == ui::theme::metrics::buttonLarge);
    REQUIRE(f.surface.tap("register.submit"));
    f.draw();
    CHECK(f.store->error == std::optional<std::string>("Le password non coincidono"));
    CHECK_FALSE(f.surface.find("dialog"));
    auto inline_ = f.surface.find("login.error");
    auto card = f.surface.find("login.card");
    auto confirm = f.surface.field("register.confirm");
    REQUIRE(inline_);
    REQUIRE(card);
    REQUIRE(confirm);
    CHECK(card->frame.contains({inline_->frame.midX(), inline_->frame.midY()}));
    CHECK(inline_->frame.y >= confirm->full.maxY());
    CHECK(inline_->frame.y - confirm->full.maxY() < 32);
    CHECK(f.surface.contains("Le password non coincidono"));
    f.edit("register.confirm", "secret-password");
    CHECK_FALSE(f.store->error);
    CHECK_FALSE(f.surface.find("login.error"));
    f.draw(860, "client-login-860.png");
    CHECK(f.surface.find("login.card")->frame.maxX() <= 860);
    CHECK(f.surface.find("login.submit")->frame.maxX() <= 860);
    REQUIRE(f.surface.tap("login.forgot"));
    f.draw(1180, "client-login-recupero.png");
    CHECK(f.surface.contains("Recupera la password"));
    REQUIRE(f.surface.find("login.recovery.send"));
    REQUIRE(f.surface.tap("login.recovery.back"));
    f.draw();
    f.store->needsProfile = true;
    f.draw(1180, "client-login-profilo.png");
    CHECK(f.surface.find("login.finish"));
    CHECK_FALSE(f.surface.find("register.username"));
    CHECK_FALSE(f.surface.find("login.submit"));
}

TEST_CASE("Client: scelta del server conservata", "[interface][client]") {
    ClientFixture f;
    auto [session, sequence] = f.backend.join(f.backend.player(1));
    ExperienceEntry entry;
    entry.id = f.backend.experience;
    entry.title = "Arena di prova";
    entry.published = true;
    entry.serverSize = 20;
    f.store->showExperience(entry);
    harness::awaitVoid(f.store->loadDetails(entry.id));
    f.draw(1180, "client-experience.png");
    REQUIRE(f.surface.tap("experience.tabs.1"));
    f.draw(1180, "client-servers.png");
    auto server = f.store->details.at(entry.id).servers.front();
    REQUIRE(f.surface.tap("experience.join." + std::to_string(sequence)));
    CHECK(f.store->join->server() == std::optional<ld::ServerId>(ld::ServerId(server.id)));
    f.store->join->cancel();
    LaunchRequest::shared().joinTicket.reset();
}

TEST_CASE("Client: bozza dell'avatar fra le schede", "[interface][client]") {
    ClientFixture f;
    f.store->navigate(Route::avatar);
    f.draw(1180, "client-avatar.png");
    CHECK_FALSE(f.surface.tap("avatar.save"));
    REQUIRE(f.surface.tap("avatar.tabs.1"));
    f.draw();
    f.edit("avatar.name", "Nome modificato");
    CHECK(f.store->avatarDraft->displayName == "Nome modificato");
    REQUIRE(f.surface.tap("avatar.tabs.2"));
    f.draw();
    REQUIRE_FALSE(f.surface.tap("avatar.color.head.6730EE"));
    REQUIRE(f.surface.tap("avatar.color.head.6633EE"));
    f.draw();
    CHECK(f.store->avatarDraft->color(BodyPart::head) == "6633EE");
    CHECK(f.store->avatar->displayName == "Giocatrice");
    REQUIRE(f.surface.tap("topNavigation.home"));
    f.draw();
    CHECK(f.store->route == Route::avatar);
    REQUIRE(f.store->dialog);
    CHECK(f.store->dialog->title == "Hai modifiche non salvate. Uscire lo stesso?");
    REQUIRE(f.surface.tap("dialog.button.0"));
    f.draw();
    CHECK(f.store->route == Route::avatar);
    CHECK(f.store->avatarDraft->displayName == "Nome modificato");
    REQUIRE(f.surface.tap("avatar.tabs.1"));
    f.draw();
    REQUIRE(f.surface.tap("avatar.body.0"));
    f.draw();
    CHECK(f.store->avatarDraft->bodyPreset == "male");
    REQUIRE(f.surface.tap("avatar.revert"));
    CHECK(f.store->avatarDraft == f.store->avatar);
    f.draw();
    REQUIRE(f.surface.tap("topNavigation.home"));
    CHECK(f.store->route == Route::home);
}

TEST_CASE("Client: dialoghi e notifiche condivisi", "[interface][client]") {
    ClientFixture f;
    f.store->requests = {{"Pfriend", "Un amico", "2026-10-09T10:00:00Z"}};
    f.store->syncNotifications();
    f.draw();
    REQUIRE(f.surface.tap("utility.notifications"));
    f.draw(1180, "client-notifications.png");
    CHECK(f.surface.find("notifications")->frame.width == ui::theme::metrics::popoverWidth);
    REQUIRE(f.surface.find("notification.time.Pfriend"));
    CHECK(f.surface.contains("9 ott 2026"));
    CHECK(f.surface.find("notifications")->frame.y == 40);
    CHECK(f.store->unreadNotifications() == 1);
    REQUIRE(f.surface.tap("notifications.readAll"));
    f.draw();
    CHECK(f.store->unreadNotifications() == 0);
    REQUIRE(f.surface.tap("notification.dismiss.Pfriend"));
    f.draw();
    CHECK(f.store->notifications.empty());
    f.store->navigate(Route::friends);
    f.draw(1180, "client-friends.png");
    f.store->navigate(Route::settings);
    f.draw(1180, "client-settings.png");
    REQUIRE(f.surface.tap("settings.logout"));
    f.draw();
    auto nav = f.surface.find("topNavigation.home");
    REQUIRE(nav);
    f.surface.mouseDown({nav->frame.midX(), nav->frame.midY()});
    f.surface.mouseUp({nav->frame.midX(), nav->frame.midY()});
    CHECK(f.store->route == Route::settings);
    REQUIRE(f.surface.tap("dialog.button.0"));
    CHECK(f.store->profile.has_value());
}

TEST_CASE("Client: miniatura nel riquadro dei dettagli", "[interface][client][thumbnail]") {
    ClientFixture f;
    Experience world;
    world.id = "7d0b6d2e-6f7a-4a52-9a53-2c1f1f6c0b11";
    world.title = "Esperienza con miniatura";
    world.author = "Giocatrice";
    f.store->setWorlds({world});
    f.store->setDiscover({WorldSummary{world.id, world.title, "test", "Giocatrice", std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt}});
    for (auto [width, height] : std::vector<std::pair<int, int>>{{1600, 900}, {400, 900}, {1000, 1000}}) {
        CAPTURE(width, height);
        std::vector<std::uint8_t> rgba(static_cast<std::size_t>(width * height * 4));
        for (std::size_t i = 0; i < rgba.size(); i += 4) {
            rgba[i] = 230;
            rgba[i + 1] = 20;
            rgba[i + 2] = 200;
            rgba[i + 3] = 255;
        }
        ui::Image picture = ui::imageFromRgba(rgba, width, height);
        f.view.thumbnail = [picture](const std::string&) { return picture; };
        auto catalog = f.store->experienceCatalog();
        REQUIRE_FALSE(catalog.empty());
        f.store->showExperience(catalog.front());
        f.surface.resize({1180, 760}, 1);
        ui::Canvas canvas(1180, 760, 1);
        f.surface.render(canvas);
        auto cover = f.surface.find("experience.cover");
        REQUIRE(cover);
        CHECK(cover->frame.width == Catch::Approx(576));
        CHECK(cover->frame.height == Catch::Approx(324));
        auto magenta = [&](double x, double y) {
            ui::Color c = canvas.pixel(x, y);
            return c.r > 0.8 && c.g < 0.2 && c.b > 0.7;
        };
        CHECK(magenta(cover->frame.midX(), cover->frame.midY()));
        CHECK(magenta(cover->frame.x + 6, cover->frame.y + 6));
        int outside = 0;
        for (double y = cover->frame.y - 6; y < cover->frame.maxY() + 6; y += 2)
            for (double x : {cover->frame.x - 4, cover->frame.maxX() + 3, cover->frame.maxX() + 12})
                if (x >= 0 && x < 1180 && y >= 0 && y < 760 && magenta(x, y)) ++outside;
        for (double x = cover->frame.x; x < cover->frame.maxX(); x += 4)
            for (double y : {cover->frame.y - 4, cover->frame.maxY() + 3})
                if (y >= 0 && y < 760 && magenta(x, y)) ++outside;
        CHECK(outside == 0);
        f.store->detail.reset();
    }
}

TEST_CASE("Client: miniatura e icona dell'autore", "[interface][client][thumbnail][images]") {
    ClientFixture f;
    auto folder = std::filesystem::temp_directory_path() / ("Ludissey Miniature " + makeUuid());
    asset_cache::setFolder(folder);
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(64 * 36 * 4), 180);
    auto png = encodePng(rgba, 64, 36);
    REQUIRE(png);
    std::string cover = asset_cache::identifier(*png, "png");
    asset_cache::store(*png, cover);
    std::vector<std::uint8_t> square(static_cast<std::size_t>(32 * 32 * 4), 90);
    auto iconPng = encodePng(square, 32, 32);
    REQUIRE(iconPng);
    std::string icon = asset_cache::identifier(*iconPng, "png");
    asset_cache::store(*iconPng, icon);
    Experience world = Experience::blank();
    world.id = "4c2b1a0f-9e8d-47c6-b5a4-3f2e1d0c9b8a";
    world.title = "Con immagini";
    world.thumbnail = cover;
    world.icon = icon;
    f.store->setWorlds({world});
    ui::ClientMedia media(f.store);
    ui::Image thumbnail = media.thumbnail(world.id);
    REQUIRE(thumbnail.valid());
    CHECK(thumbnail.pixelsWide == 64);
    CHECK(thumbnail.pixelsHigh == 36);
    ui::Image badge = media.icon(world.id);
    REQUIRE(badge.valid());
    CHECK(badge.pixelsWide == 32);
    Experience plain = world;
    plain.thumbnail.reset();
    plain.icon.reset();
    f.store->setWorlds({plain});
    CHECK_FALSE(media.icon(world.id).valid());
    ui::Image generated = media.thumbnail(world.id);
    REQUIRE(generated.valid());
    CHECK(generated.pixelsWide != 64);
    asset_cache::setFolder(std::nullopt);
    std::filesystem::remove_all(folder);
}

namespace {
class MasterRecorder final : public audio::Output {
public:
    std::vector<double> masters;
    bool play(const std::string&, const audio::Playback&) override { return true; }
    void update(const std::string&, const audio::Playback&) override {}
    bool playing(const std::string&) override { return false; }
    void stop(const std::string&) override {}
    void setMaster(double volume) override { masters.push_back(volume); }
};
}

TEST_CASE("Client: impostazioni locali", "[interface][client][settings]") {
    ClientFixture f;
    auto recorder = std::make_shared<MasterRecorder>();
    audio::setOutput(recorder);
    std::vector<std::optional<int>> chosen;
    f.view.graphicsChanged = [&](std::optional<int> level) { chosen.push_back(level); };
    f.store->navigate(Route::settings);
    f.draw();
    CHECK_FALSE(f.surface.contains("Versione"));
    CHECK_FALSE(f.surface.find("settings.version"));
    CHECK(f.surface.find("settings.logout")->frame.width <= ui::theme::metrics::settings);
    REQUIRE(f.surface.tap("settings.tabs.3"));
    f.draw();
    auto volume = f.surface.find("settings.volume");
    REQUIRE(volume);
    REQUIRE(f.surface.drag("settings.volume", {volume->frame.maxX() - 2, volume->frame.midY()}, {volume->frame.midX(), volume->frame.midY()}));
    f.draw();
    CHECK(audio::masterVolume() == Catch::Approx(0.5));
    REQUIRE_FALSE(recorder->masters.empty());
    CHECK(recorder->masters.back() == Catch::Approx(0.5));
    CHECK(local_preferences::number(local_preferences::keys::masterVolume) == std::optional<double>(0.5));
    CHECK(f.surface.contains("50%"));
    volume = f.surface.find("settings.volume");
    REQUIRE(f.surface.drag("settings.volume", {volume->frame.midX(), volume->frame.midY()}, {volume->frame.x - 20, volume->frame.midY()}));
    CHECK(audio::masterVolume() == 0);
    f.draw();
    CHECK(f.surface.contains("Muto"));
    REQUIRE(f.surface.tap("settings.chat"));
    CHECK(local_preferences::number(local_preferences::keys::chatVisible) == std::optional<double>(0));
    f.draw();
    REQUIRE(f.surface.tap("settings.chat"));
    CHECK(local_preferences::number(local_preferences::keys::chatVisible) == std::optional<double>(1));
    auto graphics = f.surface.field("settings.graphics");
    REQUIRE(graphics);
    REQUIRE(graphics->choose);
    CHECK(graphics->options.size() == render::GraphicsLevel::ladder().size() + 1);
    graphics->choose(3);
    CHECK(chosen.back() == std::optional<int>(2));
    CHECK(local_preferences::number(local_preferences::keys::graphicsChoice) == std::optional<double>(2));
    f.draw();
    graphics = f.surface.field("settings.graphics");
    graphics->choose(0);
    CHECK(chosen.back() == std::nullopt);
    auto language = f.surface.field("settings.language");
    REQUIRE(language);
    CHECK(language->options.size() == 3);
    language->choose(2);
    CHECK(localization::language() == Language::english);
    CHECK(local_preferences::number(local_preferences::keys::language) == std::optional<double>(1));
    language->choose(1);
    CHECK(localization::language() == Language::italian);
    localization::setLanguage(Language::italian);
    CHECK(std::string(LUDISSEY_VERSION) == "1.0.0");
    audio::setMasterVolume(1);
    audio::setOutput(nullptr);
    local_preferences::useMemory(true);
}

TEST_CASE("Client: busto del proprio avatar", "[interface][client][rendering]") {
    ClientFixture f;
    ui::ClientMedia media(f.store);
    ui::Image bust = media.portrait("Giocatrice");
    REQUIRE(bust.valid());
    CHECK(bust.pixelsWide == 300);
    CHECK(bust.pixelsHigh == 300);
    CHECK_FALSE(media.portrait("Leo").valid());
    f.view.portrait = [&](const std::string& name) { return media.portrait(name); };
    f.draw(1180, "client-home-busto.png");
    auto headshot = f.surface.find("home.headshot");
    REQUIRE(headshot);
    CHECK(headshot->frame.width == ui::theme::metrics::headshot);
}

TEST_CASE("Client: nessun codice tecnico nei testi", "[interface][client]") {
    ClientFixture f;
    ExperienceEntry entry;
    entry.id = "5a1f0c2e-7d3b-4c1a-8e2f-00000000dead";
    entry.title = "Esperienza sparita";
    entry.published = true;
    f.store->showExperience(entry);
    harness::awaitVoid(f.store->loadDetails(entry.id));
    f.store->experienceTab = "Server";
    f.draw();
    CHECK(f.surface.contains("Questa esperienza non è disponibile in questo momento."));
    for (const auto& element : f.surface.elements()) {
        INFO(element.label);
        CHECK((element.label.empty() || element.label[0] != '['));
    }
    CHECK(readable("[ExperienceUnavailable] Esperienza non disponibile") == "Esperienza non disponibile");
    CHECK(messageCode("[ExperienceUnavailable] Esperienza non disponibile") == std::optional<std::string>("ExperienceUnavailable"));
}
