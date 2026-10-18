#include "Assets.hpp"

#include "Engine/Core/Text.hpp"
#include "Engine/Networking/Join.hpp"
#include "Localization/Localization.hpp"

#include <algorithm>
#include <fstream>
#include <mutex>
#include <regex>
#include <set>
#include <sstream>

namespace ld {

namespace asset_cache {

namespace {

std::optional<std::filesystem::path>& overrideFolder() {
    static std::optional<std::filesystem::path> folder;
    return folder;
}

std::mutex& checkedLock() {
    static std::mutex lock;
    return lock;
}

std::set<std::string>& checked() {
    static std::set<std::string> set;
    return set;
}

}

bool valid(std::string_view asset) {
    static const std::regex pattern("^[a-f0-9]{64}\\.[a-z0-9]{1,5}$");
    return std::regex_match(asset.begin(), asset.end(), pattern);
}

std::string digest(std::string_view data) { return sha256Hex(data); }

std::string identifier(std::string_view data, std::string_view extension) { return digest(data) + "." + lowercase(extension); }

std::filesystem::path folder() {
    if (overrideFolder()) return *overrideFolder();
    return supportFolder() / "Risorse";
}

void setFolder(std::optional<std::filesystem::path> value) {
    overrideFolder() = std::move(value);
    std::lock_guard<std::mutex> guard(checkedLock());
    checked().clear();
}

std::filesystem::path location(std::string_view asset) { return folder() / std::string(asset); }

std::optional<std::string> content(const PropertyValue& value) {
    if (value.kind != PropertyValue::Kind::content || value.text.rfind(rules::assets::prefix, 0) != 0) return std::nullopt;
    std::string asset = value.text.substr(rules::assets::prefix.size());
    if (!valid(asset)) return std::nullopt;
    return asset;
}

std::string reference(std::string_view asset) { return std::string(rules::assets::prefix) + std::string(asset); }

bool matches(std::string_view data, std::string_view asset) { return valid(asset) && startsWith(asset, digest(data) + "."); }

std::string readFile(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw ServiceError(tr("File non leggibile: {0}", path.filename().string()));
    std::stringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

void writeFile(const std::filesystem::path& path, std::string_view data) {
    std::filesystem::create_directories(path.parent_path());
    std::filesystem::path temporary = path;
    temporary += ".scrittura";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) throw ServiceError(tr("File non scrivibile: {0}", path.filename().string()));
        stream.write(data.data(), static_cast<std::streamsize>(data.size()));
    }
    std::filesystem::rename(temporary, path);
}

std::optional<std::filesystem::path> verified(std::string_view asset) {
    auto url = location(asset);
    {
        std::lock_guard<std::mutex> guard(checkedLock());
        if (checked().count(std::string(asset)) && std::filesystem::exists(url)) return url;
    }
    if (!valid(asset) || !std::filesystem::exists(url)) return std::nullopt;
    std::string data;
    try {
        data = readFile(url);
    } catch (const ServiceError&) {
        return std::nullopt;
    }
    if (!matches(data, asset)) return std::nullopt;
    std::lock_guard<std::mutex> guard(checkedLock());
    checked().insert(std::string(asset));
    return url;
}

std::filesystem::path store(std::string_view data, std::string_view asset) {
    if (!matches(data, asset)) throw ServiceError("[AssetCorrupted] " + tr("Il contenuto della risorsa non corrisponde al suo identificatore."));
    auto url = location(asset);
    writeFile(url, data);
    return url;
}

std::string adopt(const std::filesystem::path& source) {
    std::string data = readFile(source);
    std::string extension = source.extension().string();
    if (!extension.empty() && extension[0] == '.') extension = extension.substr(1);
    std::string asset = identifier(data, extension);
    if (!valid(asset)) throw ServiceError("[AssetRejected] " + tr("Estensione del file non valida: .{0}", extension));
    store(data, asset);
    return asset;
}

}

AssetService::AssetService(std::shared_ptr<Backend> owner, std::shared_ptr<HttpTransport> custom) : backend(std::move(owner)), transport(custom ? std::move(custom) : backend->storage()) {}

std::vector<AssetId> AssetService::references(const Experience& world) {
    std::set<AssetId> found;
    for (const auto& b : world.blocks)
        if (b.kind == "mesh" && b.meshAsset) found.insert(AssetId(*b.meshAsset));
    for (const auto& record : world.instances)
        for (const auto& [name, value] : record.properties)
            if (auto asset = asset_cache::content(value)) found.insert(AssetId(*asset));
    for (const auto& image : {world.icon, world.thumbnail})
        if (image && isImageAsset(*image)) found.insert(AssetId(*image));
    return {found.begin(), found.end()};
}

std::vector<AssetId> AssetService::meshes(const Experience& world) {
    std::set<AssetId> found;
    for (const auto& b : world.blocks)
        if (b.kind == "mesh" && b.meshAsset) found.insert(AssetId(*b.meshAsset));
    return {found.begin(), found.end()};
}

Async<void> AssetService::upload(Experience world) {
    auto assets = references(world);
    if (assets.empty()) co_return;
    for (const auto& asset : assets)
        if (!asset_cache::verified(asset.text())) throw ServiceError("[AssetMissing] " + tr("Risorsa non presente su questo Mac: reimporta il modello {0}.", asset.text()));
    Json list = Json::array();
    for (const auto& a : assets) list.push(a.text());
    Json response = co_await backend->cloud("LudisseyAssetUpload", Json::object({{"assets", list}}));
    Json pending = response["pending"].isArray() ? response["pending"] : Json::array();
    Json finalized = Json::array();
    for (const auto& item : pending.items()) {
        std::string asset = item["id"].stringOr("");
        std::string url = item["url"].stringOr("");
        auto file = asset_cache::verified(asset);
        if (asset.empty() || url.empty() || !file) throw ServiceError("[AssetRejected] " + tr("Destinazione di caricamento non valida."));
        HttpRequest request;
        request.method = "PUT";
        request.url = url;
        request.headers = {{"x-ms-blob-type", "BlockBlob"}, {"Content-Type", "application/octet-stream"}};
        request.body = asset_cache::readFile(*file);
        auto channel = transport;
        HttpResponse reply = co_await background([channel, request] { return channel->perform(request); });
        if (!reply.success()) throw ServiceError("[AssetRejected] " + tr("Caricamento della risorsa non riuscito ({0}).", reply.status));
        finalized.push(asset);
    }
    if (!pending.items().empty()) co_await backend->cloud("LudisseyAssetFinalize", Json::object({{"assets", finalized}}));
}

Async<std::map<AssetId, std::filesystem::path>> AssetService::resolve(Experience world) {
    std::map<AssetId, std::filesystem::path> resolved;
    std::vector<AssetId> missing;
    for (const auto& asset : references(world)) {
        if (!asset_cache::valid(asset.text())) throw ServiceError("[AssetLoadFailure] " + tr("Risorsa non valida"));
        if (auto url = asset_cache::verified(asset.text())) resolved[asset] = *url;
        else missing.push_back(asset);
    }
    if (missing.empty()) co_return resolved;
    Json list = Json::array();
    for (const auto& a : missing) list.push(a.text());
    Json response;
    try {
        response = co_await backend->cloud("LudisseyAssetResolve", Json::object({{"experienceId", world.id}, {"assets", list}}));
    } catch (const ServiceError&) {
        throw ServiceError("[AssetLoadFailure] " + tr("Risorsa dell’esperienza mancante."));
    }
    for (const auto& asset : missing) {
        std::string raw = response["urls"][asset.text()].stringOr("");
        if (raw.empty()) throw ServiceError("[AssetLoadFailure] " + tr("Risorsa dell’esperienza mancante."));
        HttpRequest request;
        request.url = raw;
        auto channel = transport;
        HttpResponse reply = co_await background([channel, request] { return channel->perform(request); });
        if (!reply.success() || !asset_cache::matches(reply.body, asset.text())) throw ServiceError("[AssetLoadFailure] " + tr("Risorsa dell’esperienza mancante."));
        resolved[asset] = asset_cache::store(reply.body, asset.text());
    }
    co_return resolved;
}

namespace {

AvatarCollection collection(const Json& response) {
    auto result = AvatarCollection::fromResponse(response);
    if (!result) throw ServiceError(result.error().message);
    return *result;
}

}

Async<AvatarLoad> BackendAvatarRepository::load() {
    Json response = co_await backend->cloud("LudisseyAvatar", Json::object());
    AvatarLoad out;
    if (response.contains("avatar") && !response["avatar"].isNull()) {
        auto avatar = AvatarConfiguration::fromJson(response["avatar"]);
        if (!avatar) throw ServiceError(avatar.error().message);
        out.avatar = *avatar;
    }
    out.collection = collection(response);
    co_return out;
}

Async<AvatarConfiguration> BackendAvatarRepository::save(AvatarConfiguration avatar) {
    Json response = co_await backend->cloud("LudisseySaveAvatar", Json::object({{"avatar", avatar.json()}}));
    auto stored = AvatarConfiguration::fromJson(response["avatar"].isObject() ? response["avatar"] : Json::object());
    if (!stored) throw ServiceError(stored.error().message);
    co_return *stored;
}

Async<AvatarCollection> BackendAvatarRepository::acquire(std::string item) {
    Json response = co_await backend->cloud("LudisseyAcquireAvatarItem", Json::object({{"itemId", item}}));
    co_return collection(response);
}

AvatarCache::AvatarCache(std::filesystem::path location) : folder(std::move(location)) {}

AvatarCache AvatarCache::standard() { return AvatarCache(supportFolder() / "Avatar"); }

std::filesystem::path AvatarCache::file(const std::string& account) const {
    std::string safe;
    for (char c : lowercase(account))
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-') safe += c;
    return folder / ((safe.empty() ? std::string("giocatore") : safe) + ".json");
}

std::optional<AvatarConfiguration> AvatarCache::load(const std::string& account) const {
    try {
        auto parsed = Json::parse(asset_cache::readFile(file(account)));
        if (!parsed) return std::nullopt;
        auto avatar = AvatarConfiguration::fromJson(*parsed);
        if (!avatar) return std::nullopt;
        return *avatar;
    } catch (const ServiceError&) {
        return std::nullopt;
    }
}

void AvatarCache::save(const AvatarConfiguration& avatar, const std::string& account) const { asset_cache::writeFile(file(account), avatar.json().dump()); }

}
