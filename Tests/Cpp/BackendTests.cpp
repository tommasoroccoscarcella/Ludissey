#include "Engine/Backend/Assets.hpp"
#include "Engine/Backend/Backend.hpp"
#include "Engine/Backend/LocalServer.hpp"
#include "Engine/Client/Store.hpp"
#include "Engine/Core/Rules.hpp"
#include "Engine/Core/Text.hpp"
#include "Golden.hpp"
#include "ServerHarness.hpp"

#include <chrono>
#include <regex>

using namespace ld;

namespace {

std::filesystem::path scratch(const std::string& name) {
    auto base = std::filesystem::temp_directory_path() / ("ludissey-test-" + name + "-" + makeUuid());
    std::filesystem::create_directories(base);
    return base;
}

template <typename T>
T wait(Async<T> task, double seconds = 20) {
    std::optional<T> value;
    std::exception_ptr error;
    bool done = false;
    auto runner = [&](Async<T> t) -> Async<void> {
        try {
            value.emplace(co_await t);
        } catch (...) {
            error = std::current_exception();
        }
        done = true;
    };
    runner(std::move(task)).start();
    auto started = std::chrono::steady_clock::now();
    while (!done && std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count() < seconds) fallbackQueue().pump(0.05);
    REQUIRE(done);
    if (error) std::rethrow_exception(error);
    return std::move(*value);
}

void waitVoid(Async<void> task, double seconds = 20) {
    auto wrapped = [](Async<void> t) -> Async<int> {
        co_await t;
        co_return 0;
    };
    wait(wrapped(std::move(task)), seconds);
}

class Unreachable final : public Backend {
public:
    Async<Json> cloud(std::string, Json) override {
        throw ServiceError("server remoto non raggiungibile");
        co_return Json();
    }
    Async<Json> client(std::string, Json) override {
        throw ServiceError("server remoto non raggiungibile");
        co_return Json();
    }
    std::shared_ptr<HttpTransport> storage() override { return standardTransport(); }
};

}

TEST_CASE("Async: coroutine, background, attese, errori", "[backend][async]") {
    auto compute = []() -> Async<int> {
        int a = co_await background([] { return 20; });
        co_await sleepFor(0.01);
        co_return a + 22;
    };
    CHECK(wait(compute()) == 42);
    auto failing = []() -> Async<int> {
        co_await background([] {});
        throw ServiceError("[ServerFull] pieno");
        co_return 0;
    };
    try {
        wait(failing());
        FAIL("nessun errore");
    } catch (const ServiceError& e) {
        CHECK(e.code() == std::optional<std::string>("ServerFull"));
        CHECK(e.reason() == "pieno");
    }
}

TEST_CASE("Server locale: runtime, stato e risorse", "[backend][local]") {
    auto folder = scratch("server");
    PlayerProfile profile{"Ada", "female", "2026-01-01T00:00:00.000Z"};
    {
        auto server = std::make_shared<LocalServer>(folder);
        waitVoid(server->enroll(PlayerId("P1"), profile));
        Json info = wait(server->invoke(PlayerId("P1"), "LudisseyServerInfo", Json::object()));
        CHECK(info["schemaVersion"].integer() == rules::schemaVersion);
        CHECK_FALSE(RoutedBackend::compatible(info));
        Json boot = wait(server->invoke(PlayerId("P1"), "LudisseyBootstrap", Json::object()));
        CHECK(boot["profile"]["username"].string() == "Ada");
        Experience world = Experience::blank();
        world.id = makeUuid();
        world.title = "Mondo locale";
        Json saved = wait(server->invoke(PlayerId("P1"), "LudisseySaveWorld", Json::object({{"world", world.json()}})));
        CHECK(saved["title"].string() == "Mondo locale");
        CHECK(saved["schemaVersion"].integer() == rules::schemaVersion);
        wait(server->client(PlayerId("P1"), "UpdateUserData", Json::object({{"Data", Json::object({{"Chiave", "valore"}})}})));
        try {
            wait(server->invoke(PlayerId("P1"), "FunzioneInesistente", Json::object()));
            FAIL("nessun errore");
        } catch (const ServiceError& e) {
            CHECK(std::string(e.what()).find("Funzione sconosciuta") != std::string::npos);
        }
    }
    CHECK(std::filesystem::exists(folder / "stato.json"));
    auto again = std::make_shared<LocalServer>(folder);
    Json worlds = wait(again->invoke(PlayerId("P1"), "LudisseyWorlds", Json::object()));
    REQUIRE(worlds["worlds"].size() == 1);
    CHECK(worlds["worlds"][0]["title"].string() == "Mondo locale");
    Json data = wait(again->client(PlayerId("P1"), "GetUserData", Json::object({{"Keys", Json::array({"Chiave"})}})));
    CHECK(data["Data"]["Chiave"]["Value"].string() == "valore");
    CHECK(again->blob("eABC/" + std::string(64, 'a') + ".obj"));
    CHECK_FALSE(again->blob("../etc/passwd"));
    CHECK_FALSE(again->blob("eABC/nome.obj"));
    std::filesystem::remove_all(folder);
}

TEST_CASE("Backend: solo PlayFab, mai un server sul Mac", "[backend][routed]") {
    auto remote = std::make_shared<Unreachable>();
    RoutedBackend routed(remote);
    BackendMode mode = wait(routed.negotiate());
    CHECK(mode.kind == BackendMode::Kind::unavailable);
    CHECK(mode.blocking());
    CHECK(mode.reason.find("non rispondono") != std::string::npos);
    CHECK_FALSE(routed.onThisMac());
    CHECK_THROWS(wait(routed.cloud("LudisseyWorlds", Json::object())));
    Json incompatible = Json::object({{"schemaVersion", 3}, {"playerProtocol", rules::playerProtocol}, {"handlers", Json::array()}});
    CHECK(*RoutedBackend::compatible(incompatible) == "C’è una nuova versione di Ludissey. Aggiornala per continuare.");
    Json current = Json::object({{"schemaVersion", rules::schemaVersion}, {"playerProtocol", rules::playerProtocol}, {"handlers", Json::array()}});
    for (const auto& name : RoutedBackend::required()) {
        CAPTURE(name);
        Json missing = current;
        Json names = Json::array();
        for (const auto& other : RoutedBackend::required())
            if (other != name) names.push(other);
        missing.set("handlers", names);
        auto problem = RoutedBackend::compatible(missing);
        REQUIRE(problem);
        CHECK(problem->find(name) != std::string::npos);
    }
    routed.reset();
    CHECK(routed.mode().kind == BackendMode::Kind::unknown);
}

TEST_CASE("Risorse: cache, caricamento e risoluzione", "[backend][assets]") {
    auto folder = scratch("assets");
    auto cache = scratch("cache");
    asset_cache::setFolder(cache);
    auto server = std::make_shared<LocalServer>(folder);
    waitVoid(server->enroll(PlayerId("P1"), {"Ada", "female", "2026-01-01T00:00:00.000Z"}));
    auto backend = std::make_shared<LocalBackend>(server, PlayerId("P1"));
    std::string mesh = "o cubo\nv 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
    std::filesystem::path source = folder / "cubo.obj";
    asset_cache::writeFile(source, mesh);
    std::string asset = asset_cache::adopt(source);
    CHECK(asset_cache::valid(asset));
    CHECK(asset_cache::verified(asset));
    Experience world = Experience::blank();
    world.id = makeUuid();
    WorldBlock block;
    block.kind = "mesh";
    block.meshAsset = asset;
    world.blocks.push_back(block);
    AssetService service(backend);
    waitVoid(service.upload(world));
    wait(backend->cloud("LudisseyPublishWorld", Json::object({{"world", world.json()}})));
    std::filesystem::remove(cache / asset);
    asset_cache::setFolder(cache);
    auto resolved = wait(service.resolve(world));
    REQUIRE(resolved.count(AssetId(asset)));
    CHECK(asset_cache::readFile(resolved[AssetId(asset)]) == mesh);
    asset_cache::writeFile(cache / asset, "alterato");
    asset_cache::setFolder(cache);
    CHECK_FALSE(asset_cache::verified(asset));
    asset_cache::setFolder(std::nullopt);
    std::filesystem::remove_all(folder);
    std::filesystem::remove_all(cache);
}

TEST_CASE("Archivio locale: lettura sicura e corruzione", "[backend][local]") {
    auto folder = scratch("corrupt-archive");
    auto server = std::make_shared<LocalServer>(folder);
    server->enrollNow(PlayerId("P1"), {"Alice", "female", "2026-01-01T00:00:00.000Z"});
    auto original = asset_cache::readFile(folder / "stato.json");
    for (const auto& invalid : {std::string("(function(){return {players:{}}})()"), std::string("{\"players\":[]}"), std::string("{broken")}) {
        asset_cache::writeFile(folder / "stato.json", invalid);
        CHECK_THROWS_AS(server->invokeNow(PlayerId("P1"), "LudisseyBootstrap", Json::object()), ServiceError);
        CHECK(asset_cache::readFile(folder / "stato.json") == invalid);
    }
    asset_cache::writeFile(folder / "stato.json", original);
    CHECK(server->invokeNow(PlayerId("P1"), "LudisseyBootstrap", Json::object())["profile"]["username"] == Json("Alice"));
    std::filesystem::remove_all(folder);
}

TEST_CASE("Backend: l’app richiede ogni funzione che usa", "[backend][routed][handlers]") {
    auto text = [](const std::filesystem::path& path) {
        std::ifstream stream(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    };
    auto root = golden::root();
    std::string cloud = text(root / "CloudScript/Ludissey.js") + text(root / "CloudScript/Avatar.js");
    std::set<std::string> defined;
    static const std::regex definition(R"(handlers\.(Ludissey[A-Za-z]+)\s*=)");
    for (auto it = std::sregex_iterator(cloud.begin(), cloud.end(), definition); it != std::sregex_iterator(); ++it) defined.insert((*it)[1].str());
    std::string code;
    for (const char* folder : {"Engine", "Interface", "Studio", "Player", "Platform", "Web"})
        for (const auto& entry : std::filesystem::recursive_directory_iterator(root / folder)) {
            auto name = entry.path().filename().string();
            auto extension = entry.path().extension().string();
            if (!entry.is_regular_file() || name.find("LocalServer") != std::string::npos || name.find("ServerState") != std::string::npos) continue;
            if (extension == ".cpp" || extension == ".hpp" || extension == ".mm" || extension == ".js") code += text(entry.path());
        }
    std::set<std::string> used;
    for (const auto& name : defined)
        if (name != "LudisseyServerInfo" && (code.find("\"" + name + "\"") != std::string::npos || code.find("'" + name + "'") != std::string::npos)) used.insert(name);
    CHECK(used == RoutedBackend::required());
    for (const auto& name : RoutedBackend::required()) CHECK(defined.count(name));
}

TEST_CASE("Backend: PlayFab non compatibile non scrive nulla sul Mac", "[backend][routed][local]") {
    auto folder = supportFolder() / "Server locale";
    auto listing = [&] {
        std::vector<std::pair<std::string, std::filesystem::file_time_type>> out;
        if (std::filesystem::exists(folder))
            for (const auto& entry : std::filesystem::recursive_directory_iterator(folder)) out.emplace_back(entry.path().string(), entry.last_write_time());
        return out;
    };
    auto before = listing();
    bool existed = std::filesystem::exists(folder);
    auto older = std::make_shared<harness::ScriptWorld>();
    older->evaluate("handlers.LudisseyServerInfo = function() { return {schemaVersion:1, playerProtocol:1, handlers:['LudisseyBootstrap']}; }");
    auto missing = std::make_shared<harness::ScriptWorld>();
    missing->evaluate("delete handlers.LudisseyChat;");
    std::vector<std::pair<std::shared_ptr<Backend>, BackendMode::Kind>> cases{{older->registerPlayer("Pvecchio", "Vecchio"), BackendMode::Kind::outdated},
                                                                             {missing->registerPlayer("Pmanca", "Manca"), BackendMode::Kind::unavailable},
                                                                             {std::make_shared<Unreachable>(), BackendMode::Kind::unavailable}};
    for (auto& [remote, expected] : cases) {
        auto routed = std::make_shared<RoutedBackend>(remote);
        auto store = std::make_shared<Store>(std::make_shared<PlayFab>(std::make_shared<MemoryStore>()), routed, nullptr, false);
        store->profile = PlayerProfile{"Bea", "female", "2026-09-25T10:00:00Z"};
        harness::awaitVoid(store->connectBackend());
        CHECK(store->serverMode.kind == expected);
        CHECK_FALSE(routed->onThisMac());
        harness::awaitVoid(store->refresh());
        CHECK(store->experienceCatalog().empty());
    }
    CHECK(std::filesystem::exists(folder) == existed);
    CHECK(listing() == before);
}
