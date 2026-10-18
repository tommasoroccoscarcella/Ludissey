#include "Engine/Backend/LocalServer.hpp"
#include "Engine/Client/Store.hpp"
#include "Engine/Multiplayer/PlayerSession.hpp"
#include "Engine/Studio/StudioDocument.hpp"
#include "Engine/Studio/StudioSync.hpp"
#include "ServerHarness.hpp"

using namespace ld;

namespace {

const std::string experience = "11111111-2222-4333-8444-555555555555";

double wall() { return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count(); }

JoinTicket sample() {
    JoinTicket t;
    t.sessionId = SessionId("22222222-3333-4444-8555-666666666666");
    t.joinTicket = "ticket-1";
    t.expiresAt = std::floor(wall() + 120);
    t.playerProtocol = rules::playerProtocol;
    t.experienceId = ExperienceId(experience);
    t.nonce = "nonce-1";
    t.serverId = ServerId("33333333-4444-4555-8666-777777777777");
    t.serverSequence = 2;
    return t;
}

class FailingProvider final : public JoinSessionProvider {
public:
    explicit FailingProvider(JoinFailure f) : failure(f) {}
    Async<JoinTicket> requestSession(ExperienceId, std::optional<ServerId>) override {
        throw JoinFailureError(failure);
        co_return JoinTicket{};
    }
    Async<void> release(JoinTicket) override { co_return; }
    JoinFailure failure;
};

class RecordingProvider final : public JoinSessionProvider {
public:
    explicit RecordingProvider(JoinTicket t) : ticket(std::move(t)) {}
    Async<JoinTicket> requestSession(ExperienceId, std::optional<ServerId>) override { co_return ticket; }
    Async<void> release(JoinTicket t) override {
        released.push_back(t.sessionId);
        co_return;
    }
    JoinTicket ticket;
    std::vector<SessionId> released;
};

class SlowProvider final : public JoinSessionProvider {
public:
    explicit SlowProvider(JoinTicket t) : ticket(std::move(t)) {}
    Async<JoinTicket> requestSession(ExperienceId, std::optional<ServerId>) override {
        ++requests;
        co_await sleepFor(0.08);
        co_return ticket;
    }
    Async<void> release(JoinTicket t) override {
        released.push_back(t.sessionId);
        co_return;
    }
    JoinTicket ticket;
    int requests = 0;
    std::vector<SessionId> released;
};

class MalformedBackend final : public Backend {
public:
    Async<Json> cloud(std::string, Json) override { co_return Json::object({{"sessionId", 5}, {"joinTicket", Json::array({"non valido"})}}); }
    Async<Json> client(std::string, Json) override { co_return Json::object(); }
    std::shared_ptr<HttpTransport> storage() override { return standardTransport(); }
};

void settle(JoinCoordinator& join) {
    harness::pumpFor(0.02);
    harness::waitUntil([&] { return !phaseActive(join.phase()); }, 3);
}

ExperienceEntry entryFor(const std::string& id, int serverSize = 20) { return ExperienceEntry{id, "Città sospesa", "", "Autore", "Avventura", serverSize, std::nullopt, std::nullopt, {ExperienceScope::all}, true, std::nullopt, std::nullopt, std::nullopt, std::string()}; }

JoinTicket issued(const Json& response, double expires) {
    JoinTicket t;
    t.sessionId = SessionId(response["sessionId"].string());
    t.joinTicket = response["joinTicket"].string();
    t.expiresAt = expires;
    t.playerProtocol = 1;
    t.experienceId = ExperienceId(experience);
    t.nonce = response["nonce"].string();
    t.serverId = ServerId(response["serverId"].string());
    t.serverSequence = static_cast<int>(response["serverSequence"].integer());
    return t;
}

}

TEST_CASE("Join: contratto del ticket, URL e round trip", "[join]") {
    JoinTicket ticket = sample();
    CHECK(ticket.validate(wall()) == JoinFailure::none);
    std::string url = ticket.launchUrl();
    CHECK(url.rfind("ludissey-player://join?", 0) == 0);
    auto parsed = JoinTicket::parse(url);
    REQUIRE(parsed);
    CHECK(*parsed == ticket);
    CHECK_FALSE(JoinTicket::parse("ludissey-player://join?session=a"));
    CHECK_FALSE(JoinTicket::parse("https://esempio.test/join?session=a&ticket=b&experience=c&nonce=d&protocol=1&expires=99&server=s&sequence=1"));
    JoinTicket expired = ticket;
    expired.expiresAt = wall() - 1;
    CHECK(expired.validate(wall()) != JoinFailure::none);
    JoinTicket orphan = ticket;
    orphan.serverId = ServerId();
    CHECK(orphan.validate(wall()) != JoinFailure::none);
    JoinTicket old = ticket;
    old.playerProtocol += 1;
    CHECK(old.validate(wall()) == JoinFailure::versionMismatch);
    CHECK_FALSE(failureRetryable(JoinFailure::versionMismatch));
    CHECK(failureRetryable(JoinFailure::serverFull));
    for (auto f : allFailures()) CHECK_FALSE(failureMessage(f).empty());
    CHECK(studioProjectFromUrl("ludissey-studio://open?project=" + experience) == std::optional<std::string>(experience));
    CHECK_FALSE(studioProjectFromUrl("ludissey-studio://open?project=../altro"));
    auto& request = LaunchRequest::shared();
    request.accept(url);
    CHECK(request.joinTicket == std::optional<JoinTicket>(ticket));
    request.joinTicket.reset();
}

TEST_CASE("Join: macchina a stati del coordinatore", "[join]") {
    golden::registry();
    auto entry = entryFor(experience);
    auto world = std::make_shared<harness::ScriptWorld>();
    world->registerPlayer("Powner", "Autore");
    Experience blank = Experience::blank();
    blank.id = experience;
    world->invoke("Powner", "LudisseyPublishWorld", Json::object({{"world", blank.json()}}));
    auto backend = world->registerPlayer("Pplayer", "Giocatore");
    auto store = std::make_shared<Store>(std::make_shared<PlayFab>(std::make_shared<MemoryStore>()), backend, nullptr, false);
    store->profile = PlayerProfile{"Giocatore", "male", "2026-09-20T10:00:00Z"};
    LaunchRequest::shared().joinTicket.reset();
    store->play(entry);
    settle(*store->join);
    CHECK_FALSE(store->join->failure());
    REQUIRE(LaunchRequest::shared().joinTicket);
    CHECK(LaunchRequest::shared().joinTicket->serverSequence == 1);
    CHECK(std::find(backend->calls.begin(), backend->calls.end(), "LudisseyRequestSession") != backend->calls.end());
    LaunchRequest::shared().joinTicket.reset();
    JoinCoordinator rejected(std::make_shared<FailingProvider>(JoinFailure::experienceUnavailable));
    rejected.play(entry);
    settle(rejected);
    CHECK(rejected.failure() == std::optional<JoinFailure>(JoinFailure::experienceUnavailable));
    CHECK(rejected.phase() == JoinPhase::ready);
    JoinCoordinator busy(std::make_shared<FailingProvider>(JoinFailure::serverBusy));
    busy.play(entry);
    settle(busy);
    CHECK(busy.failure() == std::optional<JoinFailure>(JoinFailure::serverBusy));
    JoinTicket stale = sample();
    stale.expiresAt = wall() - 5;
    auto staleProvider = std::make_shared<RecordingProvider>(stale);
    JoinCoordinator expiring(staleProvider);
    expiring.play(entry);
    settle(expiring);
    CHECK(expiring.failure() == std::optional<JoinFailure>(JoinFailure::joinExpired));
    CHECK(staleProvider->released == std::vector<SessionId>{stale.sessionId});
    JoinTicket mismatch = sample();
    mismatch.playerProtocol += 1;
    JoinCoordinator old(std::make_shared<RecordingProvider>(mismatch));
    old.play(entry);
    settle(old);
    CHECK(old.failure() == std::optional<JoinFailure>(JoinFailure::versionMismatch));
    old.cancel();
    CHECK(old.phase() == JoinPhase::ready);
    CHECK_FALSE(old.failure());
    JoinStateMachine machine;
    CHECK_FALSE(machine.move(JoinPhase::playing));
    CHECK(machine.phase() == JoinPhase::ready);
    for (auto p : {JoinPhase::resolvingExperience, JoinPhase::requestingSession, JoinPhase::launchingPlayer, JoinPhase::ready}) CHECK(machine.move(p));
    CHECK(machine.history() == std::vector<JoinPhase>{JoinPhase::ready, JoinPhase::resolvingExperience, JoinPhase::requestingSession, JoinPhase::launchingPlayer, JoinPhase::ready});
    auto slow = std::make_shared<SlowProvider>(sample());
    JoinCoordinator clicker(slow);
    clicker.play(entry);
    clicker.play(entry);
    clicker.play(entry);
    settle(clicker);
    CHECK(slow->requests == 1);
    CHECK(slow->released.empty());
    CHECK_FALSE(clicker.failure());
    LaunchRequest::shared().joinTicket.reset();
    auto cancelled = std::make_shared<SlowProvider>(sample());
    JoinCoordinator aborting(cancelled);
    aborting.play(entry);
    harness::pumpFor(0.02);
    CHECK(aborting.phase() == JoinPhase::requestingSession);
    aborting.cancel();
    settle(aborting);
    harness::pumpFor(0.12);
    CHECK(aborting.phase() == JoinPhase::ready);
    CHECK_FALSE(aborting.failure());
    CHECK_FALSE(LaunchRequest::shared().joinTicket);
    CHECK(cancelled->released == std::vector<SessionId>{sample().sessionId});
}

TEST_CASE("Join: matrice dei fallimenti", "[join]") {
    golden::registry();
    auto entry = entryFor(experience, 1);
    auto world = std::make_shared<harness::ScriptWorld>();
    double reservation = rules::reservationSeconds;
    world->registerPlayer("Powner", "Autore");
    Experience blank = Experience::blank();
    blank.id = experience;
    blank.serverSize = 1;
    world->invoke("Powner", "LudisseyPublishWorld", Json::object({{"world", blank.json()}}));
    auto visits = [&] { return static_cast<int>(world->number("emulator.entities['ePowner'].objects['" + replaceAll(experience, "-", "") + "'].visits")); };
    JoinCoordinator missing(std::make_shared<BackendJoinSessionProvider>(world->registerPlayer("Pmissing", "Assente")));
    missing.play(entryFor("99999999-2222-4333-8444-555555555555", 1));
    settle(missing);
    CHECK(missing.failure() == std::optional<JoinFailure>(JoinFailure::experienceUnavailable));
    auto late = world->registerPlayer("Plate", "Ritardo");
    auto expiring = world->invoke("Plate", "LudisseyRequestSession", Json::object({{"experienceId", experience}, {"playerProtocol", 1}}));
    world->advance(reservation + 5);
    auto expired = std::make_shared<PlayerSession>(issued(expiring, wall() + 60), late, std::make_shared<AssetService>(late));
    harness::awaitVoid(expired->start());
    CHECK(expired->failure() == std::optional<JoinFailure>(JoinFailure::joinExpired));
    CHECK_FALSE(expired->ready());
    auto first = world->registerPlayer("Pfirst", "Primo");
    auto seat = world->invoke("Pfirst", "LudisseyRequestSession", Json::object({{"experienceId", experience}, {"playerProtocol", 1}}));
    auto seated = std::make_shared<PlayerSession>(issued(seat, wall() + 60), first, std::make_shared<AssetService>(first));
    harness::awaitVoid(seated->start());
    CHECK(seated->ready());
    JoinCoordinator crowd(std::make_shared<BackendJoinSessionProvider>(world->registerPlayer("Psecond", "Secondo")));
    LaunchRequest::shared().joinTicket.reset();
    crowd.play(entry, ServerId(seat["serverId"].string()));
    settle(crowd);
    CHECK(crowd.failure() == std::optional<JoinFailure>(JoinFailure::serverFull));
    crowd.retry();
    settle(crowd);
    CHECK_FALSE(crowd.failure());
    REQUIRE(LaunchRequest::shared().joinTicket);
    CHECK(LaunchRequest::shared().joinTicket->serverSequence == 2);
    LaunchRequest::shared().joinTicket.reset();
    int counted = visits();
    auto saboteur = world->registerPlayer("Pcorrupt", "Corrotto");
    auto corrupt = world->invoke("Pcorrupt", "LudisseyRequestSession", Json::object({{"experienceId", experience}, {"playerProtocol", 1}}));
    world->evaluate("(function() { var p = emulator.players['Powner'].readOnly; Object.keys(p).forEach(function(k) { if (k.indexOf('PublishedWorld_') === 0 || k.indexOf('World_') === 0) { var text = typeof p[k] === 'string'; var w = text ? JSON.parse(p[k]) : p[k]; w.blocks = 'rotto'; p[k] = text ? JSON.stringify(w) : w; } }); })()");
    auto broken = std::make_shared<PlayerSession>(issued(corrupt, wall() + 60), saboteur, std::make_shared<AssetService>(saboteur));
    harness::awaitVoid(broken->start());
    CHECK(broken->failure() == std::optional<JoinFailure>(JoinFailure::worldLoadFailure));
    CHECK_FALSE(broken->ready());
    CHECK(visits() == counted);
    CHECK(harness::refused("WorldCorrupt", [&] { world->invoke("Powner", "LudisseyExperience", Json::object({{"id", experience}})); }));
    CHECK(world->invoke("Powner", "LudisseyWorlds")["worlds"].size() == 0);
    JoinCoordinator timeout(std::make_shared<FailingProvider>(JoinFailure::connectionTimeout));
    timeout.play(entry);
    settle(timeout);
    CHECK(timeout.failure() == std::optional<JoinFailure>(JoinFailure::connectionTimeout));
    CHECK(failureRetryable(JoinFailure::connectionTimeout));
    JoinCoordinator garbled(std::make_shared<BackendJoinSessionProvider>(std::make_shared<MalformedBackend>()));
    garbled.play(entry);
    settle(garbled);
    CHECK(garbled.failure() == std::optional<JoinFailure>(JoinFailure::sessionFailure));
    CHECK_FALSE(LaunchRequest::shared().joinTicket);
    JoinTicket unlaunchable = sample();
    unlaunchable.nonce = "";
    auto launcher = std::make_shared<RecordingProvider>(unlaunchable);
    JoinCoordinator refusedJoin(launcher);
    refusedJoin.play(entry);
    settle(refusedJoin);
    CHECK(refusedJoin.failure() == std::optional<JoinFailure>(JoinFailure::joinRejected));
    CHECK(launcher->released == std::vector<SessionId>{unlaunchable.sessionId});
    CHECK_FALSE(LaunchRequest::shared().joinTicket);
}

TEST_CASE("Join: negoziazione del backend", "[join][local]") {
    golden::registry();
    auto root = std::filesystem::temp_directory_path() / ("Ludissey Server Locale " + makeUuid());
    auto folder = root / "Archivio";
    PlayerProfile profile{"Giocatrice", "female", "2026-09-25T10:00:00Z"};
    auto current = std::make_shared<harness::ScriptWorld>();
    RoutedBackend upToDate(current->registerPlayer("Pvivo", "Vivo"));
    CHECK(harness::await(upToDate.negotiate()).kind == BackendMode::Kind::live);
    auto outdated = std::make_shared<harness::ScriptWorld>();
    outdated->evaluate("handlers.LudisseyServerInfo = function() { return {schemaVersion:1, playerProtocol:1, handlers:['LudisseyBootstrap']}; }");
    RoutedBackend older(outdated->registerPlayer("Pvecchio", "Vecchio"));
    auto olderMode = harness::await(older.negotiate());
    CHECK(olderMode.kind == BackendMode::Kind::outdated);
    CHECK(olderMode.reason.find("nuova versione di Ludissey") != std::string::npos);
    auto absent = std::make_shared<harness::ScriptWorld>();
    absent->evaluate("delete handlers.LudisseyServerInfo; delete handlers.LudisseyReadySession;");
    auto routed = std::make_shared<RoutedBackend>(absent->registerPlayer("Passente", "Assente"));
    auto blocked = std::make_shared<Store>(std::make_shared<PlayFab>(std::make_shared<MemoryStore>()), routed, nullptr, false);
    blocked->profile = profile;
    harness::awaitVoid(blocked->connectBackend());
    CHECK(blocked->serverMode.kind == BackendMode::Kind::unavailable);
    CHECK_FALSE(routed->onThisMac());
    auto engine = std::make_shared<LocalServer>(folder);
    harness::awaitVoid(engine->enroll(PlayerId("Pgiocatrice"), profile));
    auto store = std::make_shared<Store>(std::make_shared<PlayFab>(std::make_shared<MemoryStore>()), std::make_shared<LocalBackend>(engine, PlayerId("Pgiocatrice")), nullptr, false);
    store->profile = profile;
    std::filesystem::create_directories(root);
    auto obj = root / "Piramide.obj";
    asset_cache::writeFile(obj, "v 0 0 0\nv 1 0 0\nv 0 1 0\nv 0 0 1\nf 1 2 3\nf 1 2 4\nf 1 3 4\nf 2 3 4\n");
    asset_cache::setFolder(root / "Cache autore");
    auto studioEngine = std::make_shared<LocalServer>(folder);
    harness::awaitVoid(studioEngine->enroll(PlayerId("Pautore"), PlayerProfile{"Autrice", "female", "2026-09-25T10:00:00Z"}));
    auto author = std::make_shared<LocalBackend>(studioEngine, PlayerId("Pautore"));
    StudioDocument document;
    document.open(Experience::blank());
    document.configure(std::nullopt, std::nullopt, 4);
    REQUIRE(document.importMesh(obj));
    std::string asset = *document.world.blocks.back().meshAsset;
    auto published = harness::await(StudioSync(author).save(*document.publishable(), true));
    REQUIRE(published.details);
    CHECK(published.details->experience.published);
    CHECK(std::filesystem::exists(folder / "Risorse/ePautore" / asset));
    std::string id = document.world.id;
    ExperienceEntry entry{id, document.world.title, "", "Autrice", document.world.genre, 4, std::nullopt, std::nullopt, {ExperienceScope::all}, true, std::nullopt, std::nullopt, std::nullopt, std::string()};
    harness::awaitVoid(store->refreshWorlds());
    harness::awaitVoid(store->loadDetails(id));
    CHECK(store->details[id].experience.author == "Autrice");
    asset_cache::setFolder(root / "Cache giocatrice");
    std::vector<JoinPhase> canonical{JoinPhase::connecting, JoinPhase::loadingWorld, JoinPhase::loadingAssets, JoinPhase::enteringExperience, JoinPhase::playing};
    int joined = 0, histories = 0;
    for (int i = 0; i < 10; ++i) {
        LaunchRequest::shared().joinTicket.reset();
        store->play(entry);
        store->play(entry);
        settle(*store->join);
        REQUIRE(LaunchRequest::shared().joinTicket);
        REQUIRE_FALSE(store->join->failure());
        auto session = std::make_shared<PlayerSession>(*LaunchRequest::shared().joinTicket, store->backend, std::make_shared<AssetService>(store->backend));
        harness::awaitVoid(session->start());
        if (session->ready() && std::any_of(session->world()->blocks.begin(), session->world()->blocks.end(), [&](const WorldBlock& b) { return b.meshAsset == std::optional<std::string>(asset); })) ++joined;
        harness::awaitVoid(session->leave());
        const auto& history = session->state().history();
        if (history.size() >= 5 && std::equal(canonical.begin(), canonical.end(), history.begin())) ++histories;
    }
    LaunchRequest::shared().joinTicket.reset();
    harness::awaitVoid(store->loadDetails(id));
    int remaining = 0;
    for (const auto& s : store->details[id].servers) remaining += s.players;
    CHECK(joined == 10);
    CHECK(histories == 10);
    CHECK(store->details[id].experience.visits == 10);
    CHECK(remaining == 0);
    auto second = std::make_shared<LocalServer>(folder);
    std::vector<std::string> crowd;
    for (int i = 0; i < 16; ++i) crowd.push_back("Pfolla" + std::to_string(i));
    for (const auto& name : crowd) harness::awaitVoid((endsWith(name, "1") ? second : engine)->enroll(PlayerId(name), PlayerProfile{name, "male", "2026-09-25T10:00:00Z"}));
    std::vector<int> outcomes(crowd.size(), -1);
    auto attempt = [](std::shared_ptr<LocalServer> server, std::string name, std::string id, int* outcome) -> Async<void> {
        try {
            Json ticket = co_await server->invoke(PlayerId(name), "LudisseyRequestSession", Json::object({{"experienceId", id}, {"playerProtocol", 1}}));
            std::string session = ticket["sessionId"].stringOr("");
            co_await server->invoke(PlayerId(name), "LudisseyOpenSession", Json::object({{"experienceId", id}, {"sessionId", session}, {"joinTicket", ticket["joinTicket"].stringOr("")}}));
            co_await server->invoke(PlayerId(name), "LudisseyReadySession", Json::object({{"experienceId", id}, {"sessionId", session}}));
            *outcome = 1;
        } catch (const std::exception&) {
            *outcome = 0;
        }
    };
    for (std::size_t i = 0; i < crowd.size(); ++i) attempt(i % 2 == 0 ? engine : second, crowd[i], id, &outcomes[i]).start();
    REQUIRE(harness::waitUntil([&] { return std::none_of(outcomes.begin(), outcomes.end(), [](int o) { return o < 0; }); }, 60));
    auto details = *ExperienceDetails::fromJson(harness::await(second->invoke(PlayerId("Pautore"), "LudisseyExperience", Json::object({{"id", id}}))));
    int seatedCount = 0, busyServers = 0;
    for (const auto& s : details.servers) {
        seatedCount += s.players;
        if (s.players > 0) ++busyServers;
        CHECK(s.players <= 4);
        CHECK(s.region == "local");
        CHECK(s.observedAt);
        CHECK(s.state == (s.players == s.capacity ? ServerLifecycle::full : ServerLifecycle::joinable));
    }
    CHECK(std::count(outcomes.begin(), outcomes.end(), 1) == 16);
    CHECK(seatedCount == 16);
    CHECK(busyServers == 4);
    CHECK(details.experience.visits == 26);
    auto reopened = std::make_shared<LocalServer>(folder);
    auto persisted = *ExperienceDetails::fromJson(harness::await(reopened->invoke(PlayerId("Pautore"), "LudisseyExperience", Json::object({{"id", id}}))));
    int persistedPlayers = 0;
    for (const auto& s : persisted.servers) persistedPlayers += s.players;
    CHECK(persisted.experience.visits == 26);
    CHECK(persistedPlayers == 16);
    asset_cache::setFolder(std::nullopt);
    std::filesystem::remove_all(root);
}
