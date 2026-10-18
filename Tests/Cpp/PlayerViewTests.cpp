#include "Engine/Core/LocalPreferences.hpp"
#include "Interface/Canvas.hpp"
#include "Interface/Surface.hpp"
#include "Player/PlayerView.hpp"
#include "ServerHarness.hpp"

using namespace ld;

namespace {
class DelayedBackend final : public Backend {
public:
    std::shared_ptr<Backend> wrapped;
    std::string delayed;
    bool waiting = false;
    bool resume = false;
    Async<Json> cloud(std::string name, Json parameters) override {
        Json response = co_await wrapped->cloud(name, parameters);
        if (name == delayed) {
            waiting = true;
            while (!resume) co_await sleepFor(0.005);
        }
        co_return response;
    }
    Async<Json> client(std::string name, Json parameters) override { co_return co_await wrapped->client(name, parameters); }
    std::shared_ptr<HttpTransport> storage() override { return wrapped->storage(); }
};
}

TEST_CASE("Player: uscita durante l'handshake", "[player][join]") {
    golden::registry();
    for (const auto& phase : {"LudisseyOpenSession", "LudisseyReadySession"}) {
        CAPTURE(phase);
        harness::Fixture fixture;
        auto backend = std::make_shared<DelayedBackend>();
        backend->wrapped = fixture.world->registerPlayer("Pcancel", "Giocatore");
        backend->delayed = phase;
        BackendJoinSessionProvider provider(backend);
        auto ticket = harness::await(provider.requestSession(ExperienceId(fixture.experience), std::nullopt));
        auto session = std::make_shared<PlayerSession>(ticket, backend, std::make_shared<AssetService>(backend));
        session->start().start();
        REQUIRE(harness::waitUntil([&] { return backend->waiting; }));
        harness::awaitVoid(session->leave());
        backend->resume = true;
        harness::pumpFor(0.04);
        CHECK_FALSE(session->ready());
        auto slots = fixture.layout();
        for (const auto& slot : slots) CHECK(slot.players == 0);
    }
}

TEST_CASE("Player: chiusura durante il join", "[player][interface]") {
    golden::registry();
    harness::Fixture fixture;
    auto backend = std::make_shared<DelayedBackend>();
    backend->wrapped = fixture.world->registerPlayer("Pshell", "Giocatore");
    backend->delayed = "LudisseyReadySession";
    auto store = std::make_shared<Store>(std::make_shared<PlayFab>(std::make_shared<MemoryStore>()), backend, nullptr, false);
    BackendJoinSessionProvider provider(backend);
    LaunchRequest::shared().joinTicket = harness::await(provider.requestSession(ExperienceId(fixture.experience), std::nullopt));
    int notifications = 0;
    {
        player::PlayerShell player(store);
        player.invalidated = [&] { ++notifications; };
        player.build();
        REQUIRE(harness::waitUntil([&] { return backend->waiting; }));
    }
    int atClose = notifications;
    backend->resume = true;
    harness::pumpFor(0.04);
    CHECK(notifications == atClose);
    LaunchRequest::shared().joinTicket.reset();
}

TEST_CASE("Player: chat in partita", "[player][chat][interface]") {
    golden::registry();
    harness::Fixture fixture;
    auto backend = fixture.world->registerPlayer("Pchat", "Chatter");
    auto store = std::make_shared<Store>(std::make_shared<PlayFab>(std::make_shared<MemoryStore>()), backend, nullptr, false);
    store->profile = PlayerProfile{"Chatter", "male", "2026-10-08"};
    BackendJoinSessionProvider provider(backend);
    LaunchRequest::shared().joinTicket = harness::await(provider.requestSession(ExperienceId(fixture.experience), std::nullopt));
    std::string other = fixture.player(1);
    auto [otherSession, sequence] = fixture.join(other);
    player::PlayerShell player(store);
    ui::Surface surface([&] { return player.build(); });
    surface.resize({1180, 760}, 1);
    double now = 0;
    auto frame = [&] {
        now += 1.0 / 30;
        player.tick(now, 1.0 / 30);
        ui::Canvas canvas(1180, 760, 1);
        surface.render(canvas);
    };
    REQUIRE(harness::waitUntil([&] {
        frame();
        return surface.find("player.list").has_value() && surface.contains("Giocatore1");
    }, 15));
    fixture.world->invoke(other, "LudisseyChat", Json::object({{"experienceId", fixture.experience}, {"sessionId", otherSession}, {"message", "Ciao a tutti"}}));
    player.gameplay->chatOpen = true;
    REQUIRE(harness::waitUntil([&] {
        frame();
        return surface.contains("Giocatore1: Ciao a tutti");
    }, 10));
    REQUIRE(surface.tap("player.mute.Giocatore1"));
    frame();
    CHECK_FALSE(surface.contains("Giocatore1: Ciao a tutti"));
    CHECK_FALSE(surface.find("player.mute.Chatter"));
    REQUIRE(surface.tap("player.mute.Giocatore1"));
    frame();
    CHECK(surface.contains("Giocatore1: Ciao a tutti"));
    for (int i = 0; i < rules::chatBurst + 1; ++i) {
        auto field = surface.field("player.chatField");
        REQUIRE(field);
        field->changed("Messaggio " + std::to_string(i));
        field->commit("Messaggio " + std::to_string(i));
        REQUIRE(harness::waitUntil([&] {
            frame();
            return true;
        }, 1));
        harness::pumpFor(0.05);
        frame();
    }
    REQUIRE(harness::waitUntil([&] {
        frame();
        return surface.find("player.chatError").has_value();
    }, 5));
    CHECK(surface.contains("Piano! Aspetta qualche secondo prima di scrivere ancora."));
    local_preferences::setNumber(local_preferences::keys::chatVisible, 0);
    frame();
    CHECK_FALSE(surface.find("player.chatButton"));
    CHECK_FALSE(surface.find("player.chat"));
    CHECK_FALSE(player.gameplay->chatOpen);
    local_preferences::remove(local_preferences::keys::chatVisible);
    frame();
    CHECK(surface.find("player.chatButton"));
    auto shot = [&](const std::string& name) {
        ui::Canvas canvas(1180, 760, 1);
        canvas.clear(ui::Color::hex("7FA8D9"));
        surface.render(canvas);
        if (const char* folder = std::getenv("LUDISSEY_SHOTS")) {
            std::filesystem::create_directories(folder);
            canvas.writePng(std::filesystem::path(folder) / name);
        }
    };
    player.gameplay->chatOpen = true;
    player.gameplay->menuOpen = true;
    frame();
    shot("player-menu.png");
    CHECK(surface.find("player.topbar")->frame.height == 34);
    CHECK(surface.find("player.list")->frame.width == 170);
    for (const char* tab : {"Giocatori", "Impostazioni", "Console"}) REQUIRE(surface.find(std::string("player.menu.tab.") + tab));
    CHECK(surface.find("player.menu.tab.Giocatori")->frame.y < surface.find("player.resume")->frame.y);
    REQUIRE(surface.tap("player.menu.tab.Impostazioni"));
    frame();
    CHECK(surface.matching("player.sensitivity").size() + surface.matching("player.menu").size() > 0);
    REQUIRE(surface.tap("player.leave"));
    frame();
    shot("player-uscita.png");
    CHECK(surface.contains("Uscire da «Arena di prova»?"));
    CHECK(surface.contains("Resta"));
    CHECK(surface.contains("Esci"));
    CHECK_FALSE(surface.contains("Tornerai alla pagina dell’esperienza."));
    REQUIRE(surface.tap("player.cancelLeave"));
    frame();
    CHECK(surface.find("player.resume"));
    LaunchRequest::shared().joinTicket.reset();
}
