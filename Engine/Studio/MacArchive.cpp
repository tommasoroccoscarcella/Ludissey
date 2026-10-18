#include "MacArchive.hpp"

#include "Engine/Backend/Assets.hpp"
#include "Engine/Backend/LocalServer.hpp"
#include "Engine/Core/Text.hpp"
#include "Localization/Localization.hpp"

namespace ld::mac_archive {

namespace {

std::optional<std::filesystem::path>& override() {
    static std::optional<std::filesystem::path> value;
    return value;
}

std::optional<std::string> owner(const std::filesystem::path& location, const std::string& username) {
    std::error_code missing;
    if (username.empty() || !std::filesystem::is_regular_file(location / "stato.json", missing)) return std::nullopt;
    auto parsed = Json::parse(asset_cache::readFile(location / "stato.json"));
    if (!parsed || !(*parsed)["players"].isObject()) return std::nullopt;
    for (const auto& [id, player] : (*parsed)["players"].members())
        if (player["username"].stringOr("") == username) return id;
    return std::nullopt;
}

struct Copy {
    std::filesystem::path path = std::filesystem::temp_directory_path() / ("Ludissey archivio " + makeUuid());
    explicit Copy(const std::filesystem::path& source) { std::filesystem::copy(source, path, std::filesystem::copy_options::recursive); }
    ~Copy() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

}

std::filesystem::path folder() { return override().value_or(supportFolder() / "Server locale"); }

void setFolder(std::optional<std::filesystem::path> value) { override() = std::move(value); }

std::vector<Experience> projects(const std::string& username) {
    auto location = folder();
    auto player = owner(location, username);
    if (!player) return {};
    std::vector<Experience> out;
    try {
        Copy copy(location);
        LocalServer reader(copy.path);
        Json listed = reader.invokeNow(PlayerId(*player), "LudisseyWorlds", Json::object());
        for (const auto& item : listed["worlds"].items())
            if (auto world = Experience::fromJson(item)) out.push_back(*world);
    } catch (const std::exception&) {
        return {};
    }
    return out;
}

Async<StudioReadBack> upload(std::shared_ptr<Backend> backend, const std::string& username, Experience world) {
    auto location = folder();
    auto player = owner(location, username);
    if (!player) throw ServiceError(tr("Copia sul Mac non trovata."));
    std::error_code missing;
    auto files = location / "Risorse" / ("e" + *player);
    if (std::filesystem::is_directory(files, missing))
        for (const auto& entry : std::filesystem::directory_iterator(files)) {
            auto name = entry.path().filename().string();
            if (entry.is_regular_file() && asset_cache::valid(name) && !asset_cache::verified(name)) asset_cache::store(asset_cache::readFile(entry.path()), name);
        }
    co_return co_await StudioSync(backend).save(world, false);
}

void remove(const std::string& username, const std::string& id) {
    auto location = folder();
    auto player = owner(location, username);
    if (!player) return;
    LocalServer archive(location);
    archive.invokeNow(PlayerId(*player), "LudisseyDeleteWorld", Json::object({{"id", id}}));
}

}
