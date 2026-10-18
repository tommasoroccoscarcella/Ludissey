#include "CursorAudit.hpp"
#include "Engine/Client/JoinCoordinator.hpp"
#include "Interface/Canvas.hpp"
#include "Interface/ClientView.hpp"
#include "Interface/Surface.hpp"
#include "Player/PlayerView.hpp"
#include "Sample.hpp"
#include "ServerHarness.hpp"

using namespace ld;

namespace {

class RefusingBackend final : public Backend {
public:
    std::shared_ptr<Backend> wrapped;
    std::string refused, held;
    bool resume = false, waiting = false;
    Async<Json> cloud(std::string name, Json parameters) override {
        if (name == refused) throw ServiceError("[ServerFull] Server pieno");
        Json response = co_await wrapped->cloud(name, parameters);
        if (name == held) {
            waiting = true;
            while (!resume) co_await sleepFor(0.005);
        }
        co_return response;
    }
    Async<Json> client(std::string name, Json parameters) override { co_return co_await wrapped->client(name, parameters); }
    std::shared_ptr<HttpTransport> storage() override { return wrapped->storage(); }
};

class FailingProvider final : public JoinSessionProvider {
public:
    JoinFailure failure = JoinFailure::none;
    Async<JoinTicket> requestSession(ExperienceId, std::optional<ServerId>) override {
        throw JoinFailureError(failure);
        co_return JoinTicket{};
    }
    Async<void> release(JoinTicket) override { co_return; }
};

struct NavigationFixture {
    harness::Fixture backend;
    std::shared_ptr<RefusingBackend> gate = std::make_shared<RefusingBackend>();
    std::shared_ptr<Store> store;
    std::unique_ptr<ui::ClientView> view;
    std::unique_ptr<player::PlayerShell> player;
    std::unique_ptr<ui::Surface> surface;
    double now = 0;
    NavigationFixture() {
        golden::registry();
        gate->wrapped = backend.world->registerPlayer("Pnavigation", "Giocatrice");
        store = std::make_shared<Store>(std::make_shared<PlayFab>(std::make_shared<MemoryStore>()), gate, nullptr, false);
        store->profile = PlayerProfile{"Giocatrice", "female", "2026-09-20T10:00:00Z"};
        store->playerId = "Pnavigation";
        store->avatar = AvatarConfiguration::standard("Giocatrice", "female");
        store->avatarDraft = store->avatar;
        view = std::make_unique<ui::ClientView>(store);
        player = std::make_unique<player::PlayerShell>(store);
        view->playerShell = [this] { return player->build(); };
        surface = std::make_unique<ui::Surface>([this] { return view->build(); });
        store->changed = [this] { surface->invalidate(); };
        view->invalidated = [this] { surface->invalidate(); };
        player->invalidated = [this] { surface->invalidate(); };
    }
    ~NavigationFixture() {
        store->changed = nullptr;
        LaunchRequest::shared().setTicket(std::nullopt);
    }
    ExperienceEntry entry() const { return ExperienceEntry{backend.experience, "Arena di prova", "", "Costruttrice", "Avventura", 20, std::nullopt, std::nullopt, {ExperienceScope::all}, true, std::nullopt, std::nullopt, std::nullopt, std::string()}; }
    void draw() {
        now += 1.0 / 30;
        player->tick(now, 1.0 / 30);
        surface->resize({1180, 760}, 1);
        ui::Canvas canvas(1180, 760, 1);
        surface->render(canvas);
    }
    bool until(const std::function<bool()>& done, double seconds = 15) {
        return harness::waitUntil([&] {
            draw();
            return done();
        }, seconds);
    }
    bool tap(const std::string& id) {
        draw();
        bool done = surface->tap(id);
        draw();
        return done;
    }
    void openExperience() {
        store->navigate(Route::home);
        store->showExperience(entry());
        draw();
    }
    void goHomeFromExperience() {
        REQUIRE(surface->find("experience.back"));
        REQUIRE(tap("topNavigation.home"));
        CHECK_FALSE(store->detail);
        CHECK(store->route == Route::home);
        CHECK(surface->find("home.headshot"));
    }
};

}

TEST_CASE("Navigazione: la barra chiude la pagina aperta", "[interface][client][navigation]") {
    NavigationFixture f;
    struct Step {
        std::string tap, page;
        Route route;
    };
    const std::vector<Step> steps{{"topNavigation.logo", "home.headshot", Route::home},          {"topNavigation.home", "home.headshot", Route::home},
                                  {"topNavigation.experiences", "shelf.featured", Route::experiences}, {"topNavigation.avatar", "avatar.preview.frame", Route::avatar},
                                  {"topNavigation.friends", "friends.tabs", Route::friends},     {"topNavigation.profile", "profile.back", Route::profile},
                                  {"account.menu.settings", "settings.page", Route::settings}};
    for (const auto& step : steps) {
        CAPTURE(step.tap);
        f.openExperience();
        REQUIRE(f.surface->find("experience.back"));
        if (step.tap == "account.menu.settings") REQUIRE(f.tap("topNavigation.settings"));
        REQUIRE(f.tap(step.tap));
        CHECK_FALSE(f.store->detail);
        CHECK(f.store->route == step.route);
        CHECK(f.surface->find(step.page));
        CHECK_FALSE(f.surface->find("experience.back"));
        if (step.route == Route::profile) CHECK((!f.store->history.empty() && f.store->history.back().detail.has_value()));
        else CHECK(f.store->history.empty());
    }
}

TEST_CASE("Navigazione: Indietro torna da dove sei arrivato", "[interface][client][navigation]") {
    NavigationFixture f;
    sample::populate(*f.store);
    auto first = sample::games().front().id;
    f.store->navigate(Route::home);
    f.draw();
    REQUIRE(f.tap("experience.tile." + first));
    REQUIRE(f.surface->find("experience.back"));
    CHECK(f.surface->find("experience.back")->label == "Home");
    REQUIRE(f.tap("experience.back"));
    CHECK(f.store->route == Route::home);
    CHECK(f.surface->find("home.headshot"));
    REQUIRE(f.tap("shelf.popular.all"));
    REQUIRE(f.surface->find("experiences.back"));
    CHECK(f.surface->find("experiences.back")->label == "Home");
    REQUIRE(f.tap("experience.row." + first));
    CHECK(f.surface->find("experience.back")->label == "Popolari");
    REQUIRE(f.tap("experience.back"));
    CHECK(f.surface->find("experiences.grid"));
    REQUIRE(f.tap("experiences.back"));
    CHECK(f.store->route == Route::home);
    CHECK_FALSE(f.store->listing);
    CHECK(f.store->history.empty());
    REQUIRE(f.tap("topNavigation.experiences"));
    REQUIRE(f.tap("shelf.popular.all"));
    CHECK(f.surface->find("experiences.back")->label == "Esperienze");
    REQUIRE(f.tap("topNavigation.experiences"));
    CHECK_FALSE(f.store->listing);
    CHECK(f.surface->find("shelf.featured"));
}

TEST_CASE("Navigazione: uscire dal gioco lascia la barra funzionante", "[interface][client][navigation][player]") {
    NavigationFixture f;
    for (int round = 0; round < 3; ++round) {
        CAPTURE(round);
        f.openExperience();
        f.store->play(f.entry());
        REQUIRE(f.until([&] { return f.player->gameplay && f.surface->find("player.menuButton").has_value(); }));
        REQUIRE(f.tap("player.menuButton"));
        REQUIRE(f.tap("player.leave"));
        REQUIRE(f.tap("player.confirmLeave"));
        REQUIRE(f.until([&] { return f.surface->find("experience.back").has_value(); }));
        f.goHomeFromExperience();
    }
    f.gate->held = "LudisseyReadySession";
    f.openExperience();
    f.store->play(f.entry());
    REQUIRE(f.until([&] { return f.surface->find("player.cancel").has_value(); }));
    REQUIRE(f.tap("player.cancel"));
    f.gate->resume = true;
    f.gate->held.clear();
    REQUIRE(f.until([&] { return f.surface->find("experience.back").has_value(); }));
    f.goHomeFromExperience();
    f.gate->refused = "LudisseyOpenSession";
    f.openExperience();
    f.store->play(f.entry());
    REQUIRE(f.until([&] { return f.surface->contains("Server pieno"); }));
    REQUIRE(f.surface->find("player.leave"));
    CHECK(f.surface->find("player.leave")->label == "Torna a Ludissey");
    CHECK(f.surface->find("player.play"));
    CHECK_FALSE(f.surface->find("player.retry"));
    CHECK(f.surface->find("player.leave")->frame.maxX() < f.surface->find("player.play")->frame.x);
    REQUIRE(f.tap("player.leave"));
    REQUIRE(f.until([&] { return f.surface->find("experience.back").has_value(); }));
    f.goHomeFromExperience();
}

TEST_CASE("Navigazione: mano e hover solo dove si clicca", "[interface][client][navigation][cursor]") {
    NavigationFixture f;
    sample::populate(*f.store);
    auto check = [&](const std::string& page) {
        f.draw();
        INFO(page);
        CHECK(audit::cursorProblems(*f.surface) == std::vector<std::string>{});
    };
    for (auto route : {Route::home, Route::experiences, Route::avatar, Route::friends, Route::settings}) {
        f.store->navigate(route);
        check(std::string(routeName(route)));
    }
    f.store->showExperience(f.store->experienceCatalog().front());
    check("esperienza");
    f.store->navigate(Route::home);
    f.store->showListing(Shelf::popular);
    check("elenco");
    f.store->navigate(Route::home);
    REQUIRE(f.tap("utility.notifications"));
    check("notifiche");
    f.store->avatarDraft->bodyColors["head"] = "000000";
    f.store->navigate(Route::avatar);
    check("avatar modificato");
    f.store->navigate(Route::home);
    f.openExperience();
    f.store->play(f.entry());
    REQUIRE(f.until([&] { return f.surface->find("player.topbar").has_value(); }));
    check("gioco");
    f.player->gameplay->chatOpen = true;
    check("gioco con la chat");
    f.player->gameplay->chatOpen = false;
    f.player->gameplay->menuOpen = true;
    check("menu di gioco");
    f.player->gameplay->menuOpen = false;
    f.store->profile.reset();
    check("accesso");
}

TEST_CASE("Navigazione: errori di ingresso con gli stessi pulsanti", "[interface][client][navigation][join]") {
    NavigationFixture f;
    auto failing = std::make_shared<FailingProvider>();
    f.store->join->provider = failing;
    const JoinFailure all[] = {JoinFailure::experienceUnavailable, JoinFailure::sessionFailure, JoinFailure::joinRejected, JoinFailure::joinExpired, JoinFailure::playerLaunchFailure, JoinFailure::connectionTimeout, JoinFailure::worldLoadFailure,
                               JoinFailure::assetLoadFailure, JoinFailure::versionMismatch, JoinFailure::networkFailure, JoinFailure::serverFull, JoinFailure::serverClosed, JoinFailure::serverBusy, JoinFailure::disconnected};
    for (auto failure : all) {
        CAPTURE(std::string(failureName(failure)));
        failing->failure = failure;
        f.openExperience();
        f.store->play(f.entry());
        REQUIRE(f.until([&] { return f.store->join->failure().has_value() && f.surface->find("join.cancel").has_value(); }, 5));
        CHECK(f.surface->contains(failureTitle(failure)));
        CHECK(f.surface->find("join.cancel")->label == "Chiudi");
        auto next = failureAction(failure);
        CHECK(f.surface->find("join.play").has_value() == (next == FailureAction::play));
        CHECK(f.surface->find("join.retry").has_value() == (next == FailureAction::retry));
        if (auto right = f.surface->find(next == FailureAction::play ? "join.play" : "join.retry")) CHECK(f.surface->find("join.cancel")->frame.maxX() < right->frame.x);
        REQUIRE(f.tap("join.cancel"));
        CHECK_FALSE(f.surface->find("join.overlay"));
    }
}

TEST_CASE("Navigazione: niente stati rimasti accesi", "[interface][client][navigation][state]") {
    NavigationFixture f;
    sample::populate(*f.store);
    f.store->navigate(Route::home);
    REQUIRE(f.tap("topNavigation.settings"));
    REQUIRE(f.surface->find("account.menu"));
    REQUIRE(f.tap("topNavigation.friends"));
    CHECK_FALSE(f.surface->find("account.menu"));
    REQUIRE(f.tap("utility.notifications"));
    REQUIRE(f.surface->find("notifications"));
    REQUIRE(f.tap("topNavigation.experiences"));
    CHECK_FALSE(f.surface->find("notifications"));
    f.gate->held = "LudisseyDiscover";
    auto pending = f.store->refresh();
    pending.start();
    REQUIRE(harness::waitUntil([&] { return f.gate->waiting; }));
    f.store->logout();
    f.gate->resume = true;
    harness::pumpFor(0.1);
    CHECK_FALSE(f.store->profile);
    CHECK(f.store->experienceCatalog().empty());
    f.draw();
    CHECK(f.surface->find("login.card"));
}

TEST_CASE("Navigazione: PlayFab non aggiornato blocca l'app", "[interface][client][navigation][backend]") {
    NavigationFixture f;
    f.store->serverMode = {BackendMode::Kind::outdated, "C’è una nuova versione di Ludissey. Aggiornala per continuare."};
    f.draw();
    REQUIRE(f.surface->find("server.blocked"));
    CHECK(f.surface->contains("Aggiorna Ludissey"));
    CHECK(f.surface->contains("C’è una nuova versione. Scaricala da ludissey.com per continuare."));
    CHECK(f.surface->find("server.download")->label == "Scarica");
    CHECK(f.surface->find("server.quit")->label == "Esci");
    CHECK(f.surface->find("server.quit")->frame.maxX() < f.surface->find("server.download")->frame.x);
    std::string opened;
    f.store->openUrl = [&](const std::string& url) { opened = url; };
    REQUIRE(f.tap("server.download"));
    CHECK(opened == "https://ludissey.com/#scarica");
    bool quitting = false;
    f.store->quit = [&] { quitting = true; };
    REQUIRE(f.tap("server.quit"));
    CHECK(quitting);
    CHECK_FALSE(f.surface->find("server.mode"));
    CHECK_FALSE(f.tap("topNavigation.friends"));
    CHECK(f.store->route == Route::home);
    f.store->serverMode = {BackendMode::Kind::unavailable, "I server di Ludissey non rispondono. Riprova tra poco."};
    f.draw();
    CHECK(f.surface->contains("Ludissey non è raggiungibile"));
    CHECK(f.surface->contains("Controlla la connessione o riprova tra qualche minuto."));
    CHECK(f.surface->find("server.retry"));
    CHECK_FALSE(f.surface->find("server.download"));
    f.store->serverMode = {BackendMode::Kind::live, ""};
    f.draw();
    CHECK_FALSE(f.surface->find("server.blocked"));
    CHECK_FALSE(f.surface->contains("Server locale"));
    f.store->error = "Connessione non riuscita. Controlla la rete e riprova.";
    f.draw();
    CHECK_FALSE(f.tap("topNavigation.friends"));
    CHECK(f.store->route == Route::home);
    f.store->error.reset();
    REQUIRE(f.tap("topNavigation.friends"));
    CHECK(f.store->route == Route::friends);
}

TEST_CASE("Profilo: si apre da ogni punto e Indietro torna alla provenienza", "[interface][client][navigation][profile]") {
    NavigationFixture f;
    auto& world = *f.backend.world;
    world.registerPlayer("Pamico", "Bruno");
    world.registerPlayer("Pnuovo", "Carla");
    world.invoke("Pamico", "LudisseyRequestFriend", Json::object({{"id", "Pnavigation"}}));
    world.invoke("Pnavigation", "LudisseyRespondFriend", Json::object({{"id", "Pamico"}, {"accept", true}}));
    world.invoke("Pnuovo", "LudisseyRequestFriend", Json::object({{"id", "Pnavigation"}}));
    harness::awaitVoid(f.store->refreshFriends());
    auto opened = [&](const std::string& id) {
        return f.until([&] { return f.store->route == Route::profile && f.store->profileId == id && f.store->profiles.count(id) && f.surface->find("profile.header").has_value(); });
    };
    auto back = [&](const std::string& title) {
        REQUIRE(f.surface->find("profile.back"));
        CHECK(f.surface->find("profile.back")->label == title);
        REQUIRE(f.tap("profile.back"));
    };
    f.store->navigate(Route::home);
    REQUIRE(f.tap("topNavigation.profile"));
    REQUIRE(opened("Pnavigation"));
    CHECK(f.surface->find("profile.own"));
    CHECK(f.surface->contains("Così vedono il tuo profilo gli altri giocatori."));
    CHECK(f.surface->find("profile.editProfile"));
    CHECK_FALSE(f.surface->find("profile.add"));
    back("Home");
    CHECK(f.store->route == Route::home);
    REQUIRE(f.tap("home.headshot"));
    REQUIRE(opened("Pnavigation"));
    back("Home");
    REQUIRE(f.tap("home.friend.Pamico"));
    REQUIRE(opened("Pamico"));
    CHECK_FALSE(f.surface->find("profile.own"));
    CHECK(f.surface->find("profile.remove"));
    CHECK(f.surface->find("profile.bio"));
    back("Home");
    f.store->navigate(Route::friends);
    f.draw();
    REQUIRE(f.tap("friends.open.Pamico"));
    REQUIRE(opened("Pamico"));
    REQUIRE(f.surface->find("profile.friend.Pnavigation"));
    REQUIRE(f.tap("profile.friend.Pnavigation"));
    REQUIRE(opened("Pnavigation"));
    back("Bruno");
    REQUIRE(opened("Pamico"));
    back("Amici");
    CHECK(f.store->route == Route::friends);
    REQUIRE(f.tap("friends.tabs.1"));
    REQUIRE(f.tap("friends.requestOpen.Pnuovo"));
    REQUIRE(opened("Pnuovo"));
    CHECK(f.surface->find("profile.accept"));
    CHECK(f.surface->find("profile.decline"));
    CHECK(f.surface->find("profile.private"));
    CHECK_FALSE(f.surface->find("profile.bio"));
    back("Amici");
    auto entry = f.entry();
    entry.owner = "Pamico";
    entry.author = "Bruno";
    f.store->navigate(Route::experiences);
    f.store->showExperience(entry);
    f.draw();
    REQUIRE(f.until([&] { return f.surface->find("experience.author").has_value() && f.store->details.count(entry.id); }));
    std::string owner = f.store->details[entry.id].experience.owner;
    REQUIRE(f.tap("experience.author"));
    REQUIRE(opened(owner));
    back("Arena di prova");
    CHECK(f.store->detail.has_value());
}

TEST_CASE("Profilo: privacy, amicizia e giocatore inesistente", "[interface][client][navigation][profile]") {
    NavigationFixture f;
    auto& world = *f.backend.world;
    world.registerPlayer("Pnuovo", "Carla");
    world.client("Pnuovo", "UpdateUserData", Json::object({{"Data", Json::object({{"LudisseyPreferences", Json::object({{"bio", "Costruisco castelli"}}).dump()}})}}));
    auto show = [&](const std::string& id, const std::string& name) {
        f.store->navigate(Route::home);
        f.store->profiles.clear();
        f.store->openProfile(id, name);
        return f.until([&] { return f.store->profiles.count(id) || f.store->profilesError.count(id); });
    };
    REQUIRE(show("Pnuovo", "Carla"));
    CHECK(f.surface->find("profile.private"));
    CHECK_FALSE(f.surface->contains("Costruisco castelli"));
    REQUIRE(f.surface->find("profile.add"));
    REQUIRE(f.tap("profile.add"));
    REQUIRE(f.until([&] { return f.surface->find("profile.requested").has_value(); }));
    CHECK_FALSE(f.surface->find("profile.requested")->enabled);
    world.client("Pnuovo", "UpdateUserData", Json::object({{"Data", Json::object({{"LudisseyPublicProfile", "{}"}})}, {"Permission", "Public"}}));
    REQUIRE(show("Pnuovo", "Carla"));
    CHECK(f.surface->contains("Costruisco castelli"));
    CHECK_FALSE(f.surface->find("profile.private"));
    REQUIRE(show("Pfantasma", "Fantasma"));
    CHECK(f.surface->find("profile.error"));
    CHECK(f.surface->contains("Giocatore non trovato"));
    REQUIRE(f.tap("profile.back"));
    CHECK(f.store->route == Route::home);
    REQUIRE(show("Pnavigation", "Giocatrice"));
    CHECK(f.surface->find("profile.own"));
    CHECK(f.surface->contains("Il tuo profilo è privato: chi non è tuo amico non vede la descrizione e gli amici."));
    CHECK(f.surface->find("profile.private"));
}
