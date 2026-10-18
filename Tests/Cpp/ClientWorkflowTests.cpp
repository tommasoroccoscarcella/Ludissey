#include "Engine/Client/Store.hpp"
#include "Engine/Core/Png.hpp"
#include "Engine/Core/Resources.hpp"
#include "Engine/Rendering/World.hpp"
#include "Golden.hpp"
#include "Interface/Canvas.hpp"
#include "Interface/ClientView.hpp"
#include "Interface/Surface.hpp"
#include "ServerHarness.hpp"

#include <fstream>

using namespace ld;

namespace {

class FakeAvatars final : public AvatarRepository {
public:
    AvatarCollection collection;
    std::optional<AvatarConfiguration> stored;
    int saves = 0;
    bool failing = false;

    Async<AvatarLoad> load() override {
        if (failing) throw ServiceError("rete non disponibile");
        co_return AvatarLoad{stored, collection};
    }
    Async<AvatarConfiguration> save(AvatarConfiguration avatar) override {
        if (failing) throw ServiceError("rete non disponibile");
        auto owned = collection.owned();
        auto index = collection.index();
        stored = avatar.validated(avatar.displayName, avatar.bodyPreset, &owned, &index);
        ++saves;
        co_return *stored;
    }
    Async<AvatarCollection> acquire(std::string item) override {
        if (failing) throw ServiceError("rete non disponibile");
        collection.inventory.push_back(InventoryEntry{makeUuid(), item, "2026-10-01T00:00:00.000Z"});
        co_return collection;
    }
};

bool settled(Store& store) { return harness::waitUntil([&] { return !store.busy; }); }

}

TEST_CASE("Client: avatar, catalogo e salvataggio", "[client][avatar][workflow]") {
    golden::registry();
    auto folder = std::filesystem::temp_directory_path() / ("ludissey-avatar-" + makeUuid());
    std::filesystem::create_directories(folder);
    asset_cache::setFolder(folder / "Risorse");
    auto hairFile = folder / "Acconciature di prova.obj";
    std::ofstream(hairFile) << "v 0 0 0\nv 1 0 0\nv 0 1 0\nv 0 0 1\nf 1 2 3\nf 1 2 4\nf 1 3 4\nf 2 3 4\n";
    std::string hairAsset = asset_cache::adopt(hairFile);
    AvatarItem hair{"test-hair", "TestHair", *AvatarCategory::named("hair"), "Martina", hairAsset, std::nullopt, true};
    std::vector<AvatarItem> hats;
    for (int i = 1; i <= 4; ++i) hats.push_back(AvatarItem{"test-hat-" + std::to_string(i), "TestHat " + std::to_string(i), *AvatarCategory::named("hat"), "Martina", std::nullopt, std::nullopt, true});
    auto avatars = std::make_shared<FakeAvatars>();
    avatars->collection.catalog = {hair};
    avatars->collection.catalog.insert(avatars->collection.catalog.end(), hats.begin(), hats.end());
    auto store = std::make_shared<Store>(std::make_shared<PlayFab>(std::make_shared<MemoryStore>()), nullptr, avatars, false);
    CHECK_FALSE(store->profile);
    store->profile = PlayerProfile{"Martina", "male", "2026-09-20T10:00:00Z"};

    harness::awaitVoid(store->loadAvatar());
    CHECK(store->avatar == std::optional<AvatarConfiguration>(AvatarConfiguration::standard("Martina", "male")));
    CHECK(store->avatarDraft == store->avatar);
    CHECK(store->avatarCollection.inventory.empty());
    CHECK(store->items(*AvatarCategory::named("hair")).empty());
    CHECK(store->avatarCollection.catalog.size() == 5);

    store->avatarDraft->bodyColors["torso"] = "E0392B";
    store->avatarDraft->displayName = "  Eroina  ";
    store->saveAvatar();
    REQUIRE(settled(*store));
    CHECK(store->avatar->color(BodyPart::torso) == "E0392B");
    CHECK(store->avatar->displayName == "Eroina");
    CHECK(avatars->stored == store->avatar);
    CHECK(avatars->saves == 1);
    CHECK(store->avatarCache.load("Martina") == store->avatar);
    CHECK_FALSE(store->dialog);
    CHECK(store->notice == "Avatar salvato!");
    store->notice.clear();

    store->avatarDraft->bodyColors["torso"] = "123456";
    store->avatarDraft->equipped = {EquippedAvatarItem{"test-hair", *AvatarCategory::named("hair")}};
    store->avatarDraft->displayName = "x";
    store->saveAvatar();
    REQUIRE(settled(*store));
    CHECK(store->avatar->color(BodyPart::torso) == AvatarConfiguration::standard("Martina", "male").color(BodyPart::torso));
    CHECK(store->avatar->equipped.empty());
    CHECK(store->avatar->displayName == "Martina");
    store->dialog.reset();

    avatars->failing = true;
    store->avatarDraft->bodyColors["leftLeg"] = "2FA84F";
    store->saveAvatar();
    REQUIRE(settled(*store));
    CHECK(store->avatar->color(BodyPart::leftLeg) == "2FA84F");
    CHECK(store->avatarCache.load("Martina")->color(BodyPart::leftLeg) == "2FA84F");
    CHECK(store->notice.find("questo Mac") != std::string::npos);
    CHECK_FALSE(store->dialog);
    avatars->failing = false;

    avatars->stored->bodyColors["head"] = "F2C230";
    harness::awaitVoid(store->loadAvatar());
    CHECK(store->avatar->color(BodyPart::head) == "F2C230");
    store->avatarDraft->bodyColors["head"] = "6E4630";
    store->revertAvatar();
    CHECK(store->avatarDraft == store->avatar);

    store->acquire(hair);
    REQUIRE(settled(*store));
    auto owned = store->items(*AvatarCategory::named("hair"));
    REQUIRE(owned.size() == 1);
    CHECK(owned[0].id == "test-hair");
    store->toggle(hair);
    CHECK(store->avatarDraft->wearing("test-hair"));
    for (const auto& hat : hats) {
        store->acquire(hat);
        REQUIRE(settled(*store));
    }
    for (const auto& hat : hats) store->toggle(hat);
    CHECK(store->avatarDraft->items(*AvatarCategory::named("hat")).size() == 3);
    REQUIRE(store->avatarRefusal);
    CHECK(store->avatarRefusal->find("3") != std::string::npos);
    store->saveAvatar();
    REQUIRE(settled(*store));
    store->dialog.reset();
    CHECK(store->avatar->wearing("test-hair"));
    CHECK(store->avatar->items(*AvatarCategory::named("hat")).size() == 3);

    ld::setResourceFolder(golden::root() / "Resources");
    auto rendered = render::scene_factory::avatar(*store->avatar);
    REQUIRE(rendered);
    CHECK(rendered->node->child("accessory.test-hair", true));
    store->toggle(hair);
    CHECK_FALSE(store->avatarDraft->wearing("test-hair"));
    store->revertAvatar();
    asset_cache::setFolder(std::nullopt);
    std::filesystem::remove_all(folder);
}

TEST_CASE("Client: catalogo, join, notifiche e logout", "[client][workflow]") {
    golden::registry();
    auto avatars = std::make_shared<FakeAvatars>();
    std::vector<AvatarItem> hats;
    for (int i = 1; i <= 4; ++i) hats.push_back(AvatarItem{"test-hat-" + std::to_string(i), "TestHat " + std::to_string(i), *AvatarCategory::named("hat"), "Martina", std::nullopt, std::nullopt, true});
    avatars->collection.catalog = hats;
    for (const auto& hat : hats) avatars->collection.inventory.push_back(InventoryEntry{makeUuid(), hat.id, "2026-10-01T00:00:00.000Z"});
    auto store = std::make_shared<Store>(std::make_shared<PlayFab>(std::make_shared<MemoryStore>()), nullptr, avatars, false);
    store->profile = PlayerProfile{"Martina", "male", "2026-09-20T10:00:00Z"};
    harness::awaitVoid(store->loadAvatar());

    Experience world;
    world.id = "test-own";
    world.title = "Torre dei Venti";
    world.author = "Martina";
    store->setWorlds({world});
    store->setDiscover({WorldSummary{world.id, world.title, "test", "Martina", std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt}, WorldSummary{"test-community", "Corsa sui Tetti", "other", "Leo", std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt}});
    ExperienceMetadata facts;
    facts.description = "Sali fino in cima prima che cali il sole.";
    facts.featured = true;
    facts.weeklyPlays = 10;
    store->setExperienceMetadata({{world.id, facts}});
    const auto& catalog = store->experienceCatalog();
    CHECK(catalog.size() == 2);
    CHECK(std::count_if(catalog.begin(), catalog.end(), [](const ExperienceEntry& e) { return e.categories.count(ExperienceScope::mine); }) == 1);
    CHECK(std::count_if(catalog.begin(), catalog.end(), [](const ExperienceEntry& e) { return e.categories.count(ExperienceScope::featured); }) == 1);
    auto entry = *std::find_if(catalog.begin(), catalog.end(), [&](const ExperienceEntry& e) { return e.id == world.id; });
    store->showExperience(entry);
    REQUIRE(store->detail);
    CHECK(store->detail->id == entry.id);
    store->detail.reset();

    store->friends = {Friend{"leo", "Leo", true, std::nullopt, "male"}, Friend{"pietro", "Pietro", false, std::nullopt, "male"}};
    store->requests = {FriendRequest{"pending", "Giulia", ""}};
    store->syncNotifications();
    CHECK(store->unreadNotifications() == 1);
    CHECK(store->requests.size() == 1);
    store->readNotifications.insert("pending");
    store->syncNotifications();
    CHECK(store->unreadNotifications() == 0);
    CHECK(store->notifications.size() == 1);
    store->dismissedNotifications.insert("pending");
    store->syncNotifications();
    CHECK(store->notifications.empty());
    CHECK(store->requests.size() == 1);
    store->dismissedNotifications.clear();
    store->readNotifications.clear();
    store->syncNotifications();

    store->navigate(Route::experiences, ExperienceScope::all);
    store->searchQuery = "  TETTI  ";
    std::vector<std::string> matched;
    for (const auto& e : store->experienceCatalog())
        if (store->matchesSearch(e.title)) matched.push_back(e.id);
    CHECK(store->route == Route::experiences);
    CHECK(matched == std::vector<std::string>{"test-community"});
    store->searchQuery = "   ";
    for (const auto& e : store->experienceCatalog()) CHECK(store->matchesSearch(e.title));
    store->searchQuery = "online";
    store->navigate(Route::friends);
    CHECK(store->searchQuery.empty());
    store->searchQuery = "leo";
    std::vector<std::string> friends;
    for (const auto& f : store->friends)
        if (store->matchesSearch(f.username)) friends.push_back(f.id);
    CHECK(friends == std::vector<std::string>{"leo"});
    store->searchQuery = "Giulia";
    CHECK(std::count_if(store->requests.begin(), store->requests.end(), [&](const FriendRequest& r) { return store->matchesSearch(r.username); }) == 1);
    CHECK(std::none_of(store->friends.begin(), store->friends.end(), [&](const Friend& f) { return store->matchesSearch(f.username); }));
    store->navigate(Route::avatar);
    CHECK(store->items(*AvatarCategory::named("hat")).size() == 4);
    store->avatarCategory = *AvatarCategory::named("hat");
    store->navigate(Route::avatar);
    CHECK(store->avatarCategory.name == "hat");
    store->searchQuery = "TESTHAT 2";
    auto filtered = store->items(store->avatarCategory);
    REQUIRE(filtered.size() == 1);
    CHECK(filtered[0].id == "test-hat-2");
    store->searchQuery.clear();

    store->confirmLogout();
    REQUIRE(store->dialog);
    CHECK(store->profile);
    auto cancel = store->dialog->buttons.front().action;
    store->dialog.reset();
    cancel();
    CHECK(store->profile);
    store->confirmLogout();
    REQUIRE(store->dialog);
    auto confirm = store->dialog->buttons.back().action;
    store->dialog.reset();
    confirm();
    REQUIRE(harness::waitUntil([&] { return !store->profile; }));
    CHECK_FALSE(store->avatar);
    CHECK_FALSE(store->avatarDraft);
    CHECK(store->notifications.empty());
    CHECK(store->avatarCollection.catalog.empty());
    CHECK(store->avatarCollection.inventory.empty());
}

namespace {

class ScriptedTransport final : public HttpTransport {
public:
    std::function<HttpResponse(const HttpRequest&)> reply;
    int calls = 0;
    HttpResponse perform(const HttpRequest& request) override {
        ++calls;
        return reply(request);
    }
};

}

TEST_CASE("Sessione: archivio, ripristino e token", "[client][session]") {
    golden::registry();
    auto folder = std::filesystem::temp_directory_path() / ("Ludissey Sessione " + makeUuid());
    SessionFile vault(folder / "Sessione");
    CHECK_FALSE(vault.load());
    vault.save("ticket-di-prova");
    CHECK(vault.load() == std::optional<std::string>("ticket-di-prova"));
    auto mode = std::filesystem::status(folder / "Sessione").permissions();
    CHECK((mode & (std::filesystem::perms::group_all | std::filesystem::perms::others_all)) == std::filesystem::perms::none);
    std::filesystem::permissions(folder / "Sessione", std::filesystem::perms::owner_all | std::filesystem::perms::group_read | std::filesystem::perms::others_read);
    CHECK(vault.load() == std::optional<std::string>("ticket-di-prova"));
    mode = std::filesystem::status(folder / "Sessione").permissions();
    CHECK((mode & (std::filesystem::perms::group_all | std::filesystem::perms::others_all)) == std::filesystem::perms::none);
    vault.save("secondo");
    CHECK(SessionFile(folder / "Sessione").load() == std::optional<std::string>("secondo"));
    CHECK_FALSE(std::filesystem::exists(folder / "Sessione.nuova"));
    vault.remove();
    CHECK_FALSE(vault.load());
    vault.remove();
    std::filesystem::remove_all(folder);

    auto run = [](std::shared_ptr<SecretStore> vault, std::shared_ptr<ScriptedTransport> transport) {
        auto store = std::make_shared<Store>(std::make_shared<PlayFab>(vault, transport), nullptr, nullptr, true);
        harness::awaitVoid(store->restoreSession());
        CHECK_FALSE(store->restoringSession);
        return store;
    };
    auto transport = std::make_shared<ScriptedTransport>();
    transport->reply = [](const HttpRequest&) { return HttpResponse{500, "{}", {}}; };

    auto empty = std::make_shared<MemoryStore>();
    auto fresh = run(empty, transport);
    CHECK_FALSE(fresh->profile);
    CHECK_FALSE(fresh->needsProfile);
    CHECK(transport->calls == 0);

    auto expired = std::make_shared<MemoryStore>();
    expired->save("ticket-scaduto");
    transport->reply = [](const HttpRequest&) { return HttpResponse{401, R"({"code":401,"status":"Unauthorized","error":"InvalidSessionTicket","errorMessage":"scaduto"})", {}}; };
    auto refused = run(expired, transport);
    CHECK_FALSE(refused->profile);
    CHECK_FALSE(expired->load());

    auto offline = std::make_shared<MemoryStore>();
    offline->save("ticket-valido");
    transport->reply = [](const HttpRequest&) -> HttpResponse { throw NetworkError("rete assente", false); };
    auto unreachable = run(offline, transport);
    CHECK_FALSE(unreachable->profile);
    CHECK(offline->load() == std::optional<std::string>("ticket-valido"));
}

TEST_CASE("Sessione: scadenza durante l'uso", "[client][session][expired]") {
    golden::registry();
    auto vault = std::make_shared<MemoryStore>();
    vault->save("ticket-in-uso");
    auto transport = std::make_shared<ScriptedTransport>();
    transport->reply = [](const HttpRequest&) { return HttpResponse{500, "{}", {}}; };
    auto store = std::make_shared<Store>(std::make_shared<PlayFab>(vault, transport), nullptr, nullptr, false);
    store->profile = PlayerProfile{"Giocatrice", "female", "2026-10-08"};
    store->navigate(Route::avatar);
    store->perform([]() -> Async<void> {
        throw PlayFabRequestError("InsufficientFunds", "Saldo insufficiente");
        co_return;
    });
    REQUIRE(harness::waitUntil([&] { return !store->busy; }));
    CHECK(store->profile);
    CHECK(store->error == std::optional<std::string>("Saldo insufficiente"));
    CHECK(vault->load());
    store->error.reset();
    store->perform([]() -> Async<void> {
        throw PlayFabRequestError("InvalidSessionTicket", "La sessione è scaduta: accedi nuovamente");
        co_return;
    });
    REQUIRE(harness::waitUntil([&] { return !store->busy; }));
    CHECK_FALSE(store->profile);
    CHECK_FALSE(vault->load());
    CHECK(store->route == Route::home);
    REQUIRE(store->error);
    CHECK(store->error->find("accedi di nuovo") != std::string::npos);
}

TEST_CASE("Accesso: recupero della password", "[client][session][recovery]") {
    golden::registry();
    auto transport = std::make_shared<ScriptedTransport>();
    std::vector<std::pair<std::string, Json>> requests;
    transport->reply = [&](const HttpRequest& request) {
        requests.push_back({request.url.substr(request.url.rfind('/') + 1), Json::parse(request.body).valueOr(Json::object())});
        bool authorized = std::any_of(request.headers.begin(), request.headers.end(), [](const auto& h) { return h.first == "X-Authorization"; });
        CHECK_FALSE(authorized);
        return HttpResponse{200, R"({"code":200,"data":{}})", {}};
    };
    auto store = std::make_shared<Store>(std::make_shared<PlayFab>(std::make_shared<MemoryStore>(), transport), nullptr, nullptr, false);
    ui::ClientView view(store);
    ui::Surface surface([&] { return view.build(); });
    surface.resize({1180, 760}, 1);
    auto draw = [&] {
        ui::Canvas canvas(1180, 760, 1);
        surface.render(canvas);
    };
    draw();
    REQUIRE(surface.tap("login.forgot"));
    draw();
    auto email = surface.field("login.recovery.email");
    REQUIRE(email);
    auto send = surface.find("login.recovery.send");
    REQUIRE(send);
    CHECK(send->enabled);
    REQUIRE(surface.tap("login.recovery.send"));
    draw();
    CHECK(surface.contains("Scrivi un’email valida."));
    email->changed("giocatrice@esempio.it");
    draw();
    REQUIRE(surface.tap("login.recovery.send"));
    REQUIRE(harness::waitUntil([&] { return !store->busy; }));
    draw();
    REQUIRE(requests.size() == 1);
    CHECK(requests[0].first == "SendAccountRecoveryEmail");
    CHECK(requests[0].second["Email"] == Json("giocatrice@esempio.it"));
    CHECK(requests[0].second["TitleId"] == Json(PlayFab::titleId));
    CHECK(surface.contains("Se l’email è giusta, tra poco ti arriva il link."));
    REQUIRE(surface.tap("login.recovery.back"));
    draw();
    CHECK(surface.find("login.submit"));
    CHECK_FALSE(surface.find("login.recovery.email"));
}

TEST_CASE("Client: busti caricati e visti dagli amici", "[client][avatar][portraits]") {
    golden::registry();
    harness::Fixture fixture;
    auto first = std::filesystem::temp_directory_path() / ("ludissey-busti-" + makeUuid()), second = std::filesystem::temp_directory_path() / ("ludissey-busti-" + makeUuid());
    asset_cache::setFolder(first);
    auto martinaBackend = fixture.world->registerPlayer("Pmartina", "Martina");
    auto leoBackend = fixture.world->registerPlayer("Pleo", "Leo");
    auto martina = std::make_shared<Store>(std::make_shared<PlayFab>(std::make_shared<MemoryStore>()), martinaBackend, nullptr, false);
    martina->profile = PlayerProfile{"Martina", "female", "2026-09-20T10:00:00Z"};
    harness::awaitVoid(martina->loadAvatar());
    auto picture = [](int side, std::uint8_t shade) {
        std::vector<std::uint8_t> rgba(static_cast<std::size_t>(side * side * 4), shade);
        auto png = encodePng(rgba, side, side);
        REQUIRE(png);
        return *png;
    };
    int rendered = 0;
    martina->renderPortraits = [&](const AvatarConfiguration&) {
        ++rendered;
        return std::optional<PortraitImages>(PortraitImages{picture(rules::account::portraitBust, 200), picture(rules::account::portraitFigure, 90)});
    };
    martina->avatarDraft->bodyColors["torso"] = "E0392B";
    martina->saveAvatar();
    REQUIRE(settled(*martina));
    CHECK(rendered == 1);
    CHECK(martina->notice == "Avatar salvato!");
    REQUIRE(martina->portraits.count("Martina"));
    std::string bust = martina->portraits.at("Martina");
    CHECK(isImageAsset(bust));
    Json stored = fixture.world->invoke("Pleo", "LudisseyPortraits", Json::object({{"players", Json::array({"Pmartina", "Pleo"})}}));
    CHECK(stored["players"]["Pmartina"]["bust"] == Json(bust));
    CHECK_FALSE(stored["players"]["Pleo"].contains("bust"));
    CHECK(stored["players"]["Pleo"]["gender"].isString());
    CHECK(stored["players"]["Pmartina"]["gender"].isString());
    fixture.world->invoke("Pleo", "LudisseyRequestFriend", Json::object({{"id", "Pmartina"}}));
    fixture.world->invoke("Pmartina", "LudisseyRespondFriend", Json::object({{"id", "Pleo"}, {"accept", true}}));
    asset_cache::setFolder(second);
    CHECK_FALSE(asset_cache::verified(bust));
    auto leo = std::make_shared<Store>(std::make_shared<PlayFab>(std::make_shared<MemoryStore>()), leoBackend, nullptr, false);
    leo->profile = PlayerProfile{"Leo", "male", "2026-09-20T10:00:00Z"};
    harness::awaitVoid(leo->refreshFriends());
    REQUIRE(leo->friends.size() == 1);
    CHECK(leo->friends[0].id == "Pmartina");
    CHECK(leo->friends[0].portrait == std::optional<std::string>(bust));
    CHECK(leo->portraits.at(leo->friends[0].username) == bust);
    REQUIRE(asset_cache::verified(bust));
    CHECK(asset_cache::readFile(*asset_cache::verified(bust)) == picture(rules::account::portraitBust, 200));
    asset_cache::setFolder(std::nullopt);
    std::error_code ignored;
    std::filesystem::remove_all(first, ignored);
    std::filesystem::remove_all(second, ignored);
}
