#include "LocalServer.hpp"

#include "Engine/Backend/Assets.hpp"
#include "Engine/Backend/ServerState.hpp"

#include <fcntl.h>
#include <regex>
#include <stdlib.h>
#include <sys/file.h>
#include <unistd.h>

namespace ld {

namespace {

class BlobTransport final : public HttpTransport {
public:
    explicit BlobTransport(std::weak_ptr<LocalServer> owner) : server(std::move(owner)) {}
    HttpResponse perform(const HttpRequest& request) override {
        const std::string prefix = "ludissey-emulator://";
        if (request.url.rfind(prefix, 0) != 0) return standardTransport()->perform(request);
        auto engine = server.lock();
        std::string key = request.url.substr(prefix.size());
        auto slash = key.find('/');
        if (slash != std::string::npos) key = key.substr(slash + 1);
        auto file = engine ? engine->blob(key) : std::nullopt;
        if (!file) throw NetworkError("Indirizzo della risorsa non valido", false);
        HttpResponse response;
        if (request.method == "PUT") {
            try {
                asset_cache::writeFile(*file, request.body);
                response.status = 201;
            } catch (const std::exception&) {
                response.status = 500;
            }
            return response;
        }
        try {
            response.body = asset_cache::readFile(*file);
            response.status = 200;
        } catch (const std::exception&) {
            response.status = 404;
        }
        return response;
    }

private:
    std::weak_ptr<LocalServer> server;
};

}

std::shared_ptr<LocalServer> LocalServer::standard() {
    static std::mutex guard;
    static std::weak_ptr<LocalServer> shared;
    std::lock_guard<std::mutex> lock(guard);
    if (auto existing = shared.lock()) return existing;
    auto created = std::make_shared<LocalServer>(supportFolder() / "Server locale");
    shared = created;
    return created;
}

LocalServer::LocalServer(std::filesystem::path location) : folder(std::move(location)), engine(std::make_unique<ServerState>()) {
    std::filesystem::create_directories(blobs());
}

LocalServer::~LocalServer() = default;

template <typename F>
auto LocalServer::exclusive(F body) -> decltype(body()) {
    std::lock_guard<std::recursive_mutex> guard(lock);
    std::filesystem::create_directories(folder);
    std::string lockPath = (folder / "stato.lock").string();
    int descriptor = ::open(lockPath.c_str(), O_CREAT | O_RDWR, 0600);
    if (descriptor < 0) throw ServiceError("[LocalServerFailure] Archivio del server locale non accessibile.");
    struct Unlock {
        int fd;
        ~Unlock() {
            flock(fd, LOCK_UN);
            ::close(fd);
        }
    } unlock{descriptor};
    if (flock(descriptor, LOCK_EX) != 0) throw ServiceError("[LocalServerFailure] Archivio del server locale non bloccabile.");
    std::optional<std::string> stored;
    if (std::filesystem::exists(folder / "stato.json")) stored = asset_cache::readFile(folder / "stato.json");
    if (stored != loaded) {
        ServerState fresh;
        if (stored) {
            auto parsed = Json::parse(*stored);
            if (!parsed || !parsed->isObject()) throw ServiceError("[LocalServerFailure] Archivio del server locale danneggiato.");
            for (const auto* key : {"serial", "players", "groups", "entities", "members"}) {
                if (!parsed->contains(key)) continue;
                const auto& value = std::as_const(*parsed)[key];
                if (std::string(key) == "serial" ? !value.isNumber() : !value.isObject()) throw ServiceError("[LocalServerFailure] Archivio del server locale danneggiato.");
                fresh.state.set(key, value);
            }
        }
        engine->state = std::move(fresh.state);
        loaded = stored;
    }
    Json index = Json::object();
    for (const auto& entity : std::filesystem::directory_iterator(blobs())) {
        if (!entity.is_directory()) continue;
        for (const auto& file : std::filesystem::directory_iterator(entity.path())) {
            auto key = entity.path().filename().string() + "/" + file.path().filename().string();
            if (file.is_regular_file() && blob(key)) index.set(key, Json::object({{"length", static_cast<double>(file.file_size())}}));
        }
    }
    engine->blobs = std::move(index);
    auto result = body();
    auto snapshot = engine->state.dump();
    if (std::optional<std::string>(snapshot) != loaded) {
        asset_cache::writeFile(folder / "stato.json", snapshot);
        loaded = snapshot;
    }
    return result;
}

Json LocalServer::call(bool client, const PlayerId& player, const std::string& name, const Json& parameters) {
    std::exception_ptr error;
    auto result = exclusive([&] {
        try { return client ? engine->client(player.text(), name, parameters) : engine->invoke(player.text(), name, parameters); }
        catch (...) { error = std::current_exception(); return Json(); }
    });
    if (error) std::rethrow_exception(error);
    return result;
}

Json LocalServer::invokeNow(const PlayerId& player, const std::string& name, const Json& parameters) { return call(false, player, name, parameters); }
Json LocalServer::clientNow(const PlayerId& player, const std::string& name, const Json& parameters) { return call(true, player, name, parameters); }

void LocalServer::enrollNow(const PlayerId& player, const PlayerProfile& profile) {
    exclusive([&] { engine->enroll(player.text(), profile.username, profile.json()); return 0; });
}

Async<Json> LocalServer::invoke(PlayerId player, std::string name, Json parameters) {
    auto self = shared_from_this();
    co_return co_await background([self, player, name, parameters] { return self->invokeNow(player, name, parameters); });
}

Async<Json> LocalServer::client(PlayerId player, std::string name, Json parameters) {
    auto self = shared_from_this();
    co_return co_await background([self, player, name, parameters] { return self->clientNow(player, name, parameters); });
}

Async<void> LocalServer::enroll(PlayerId player, PlayerProfile profile) {
    auto self = shared_from_this();
    co_await background([self, player, profile] { self->enrollNow(player, profile); });
}

std::optional<std::filesystem::path> LocalServer::blob(const std::string& key) const {
    auto slash = key.find('/');
    if (slash == std::string::npos || key.find('/', slash + 1) != std::string::npos) return std::nullopt;
    std::string entity = key.substr(0, slash), name = key.substr(slash + 1);
    static const std::regex pattern("^e[A-Za-z0-9]{1,40}$");
    if (!std::regex_match(entity, pattern) || !asset_cache::valid(name)) return std::nullopt;
    return blobs() / entity / name;
}

std::shared_ptr<HttpTransport> LocalServer::storage() { return std::make_shared<BlobTransport>(weak_from_this()); }

}
