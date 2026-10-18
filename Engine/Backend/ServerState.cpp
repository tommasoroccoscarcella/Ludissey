#include "ServerState.hpp"

#include "Engine/Core/Async.hpp"
#include "Engine/Core/Rules.hpp"
#include "Engine/Core/Text.hpp"
#include "Engine/Experience/Experience.hpp"
#include "Localization/Localization.hpp"
#include "ServerRules.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <stdlib.h>

namespace ld {
namespace {

using localization::localizable;
using namespace server_rules;
[[noreturn]] void fail(const std::string& message) { throw ServiceError(message); }
[[noreturn]] void refuse(const std::string& code, const std::string& message) { fail("[" + code + "] " + message); }
[[noreturn]] void apiError(const std::string& code) { fail(Json::object({{"apiErrorInfo", Json::object({{"apiError", Json::object({{"error", code}})}})}}).dump()); }
bool includes(const Json& array, const Json& value) { return std::find(array.items().begin(), array.items().end(), value) != array.items().end(); }
Json parsed(const std::string& source) {
    auto result = Json::parse(source);
    if (!result) fail(wording(localizable("Dati del server locale non validi")));
    return *result;
}
std::string iso(double millis) {
    auto seconds = static_cast<std::time_t>(std::floor(millis / 1000));
    std::tm tm{};
    gmtime_r(&seconds, &tm);
    char date[32], fraction[8];
    std::strftime(date, sizeof(date), "%Y-%m-%dT%H:%M:%S", &tm);
    std::snprintf(fraction, sizeof(fraction), ".%03dZ", static_cast<int>(millis - static_cast<double>(seconds) * 1000));
    return std::string(date) + fraction;
}
Json summary(const Json& world) {
    Json result = Json::object();
    for (auto k : {"id", "title", "owner", "author", "genre", "serverSize", "createdAt", "updatedAt", "revision"}) if (world.contains(k)) result.set(k, world[k]);
    for (auto k : {"icon", "thumbnail"}) if (world[k].isString() && isImageAsset(world[k].string())) result.set(k, world[k]);
    return result;
}
bool intact(const Json& world) {
    return world.isObject() && world["id"].isString() && world["blocks"].isArray() &&
        std::all_of(world["blocks"].items().begin(), world["blocks"].items().end(), [](const auto& b) { return b.isObject(); });
}
std::string content(const Json& world) {
    Json values = Json::array();
    for (auto k : {"title", "blocks", "genre", "serverSize", "lighting", "terrainDigest", "instancesDigest"}) values.push(world[k]);
    values.push(world.contains("languageVersion") ? world["languageVersion"] : Json(std::string(rules::lim::languageVersion)));
    values.push(world.contains("engineApiVersion") ? world["engineApiVersion"] : Json(rules::lim::engineApiVersion));
    for (auto k : {"icon", "thumbnail"}) values.push(world[k].isString() ? world[k] : Json());
    return encode(values);
}
std::vector<std::string> chunks(const std::string& value, std::size_t maximum) {
    std::vector<std::string> result;
    std::string chunk;
    std::size_t count = 0;
    for (auto c : utf8Decode(value)) {
        std::size_t length = c > 0xffff ? 2 : 1;
        if (count + length > maximum) { result.push_back(chunk); chunk.clear(); count = 0; }
        chunk += utf8Encode({c});
        count += length;
    }
    if (!chunk.empty()) result.push_back(chunk);
    return result;
}
Json references(const Json& world, const Json& records) {
    std::set<std::string> ids;
    for (const auto& b : world["blocks"].items()) if (!b["meshAsset"].string().empty()) ids.insert(b["meshAsset"].string());
    for (auto k : {"icon", "thumbnail"}) if (world[k].isString() && isImageAsset(world[k].string())) ids.insert(world[k].string());
    for (const auto& r : records.items()) {
        if (!r["properties"].isObject()) continue;
        for (const auto& [name, v] : r["properties"].members()) {
            if (!v.isObject() || !v["Content"].isString() || v["Content"].string().rfind(rules::assets::prefix, 0) != 0) continue;
            Json id(v["Content"].string().substr(rules::assets::prefix.size()));
            if (asset(id)) ids.insert(id.string());
        }
    }
    Json result = Json::array();
    for (const auto& id : ids) result.push(id);
    return result;
}
Json assets(const Json& args) {
    const auto& list = args["assets"];
    if (!list.isArray() || !list.size() || list.size() > rules::world::parts) refuse("AssetRejected", wording(localizable("Elenco risorse non valido")));
    for (const auto& id : list.items()) if (!asset(id)) refuse("AssetRejected", wording(localizable("Identificatore risorsa non valido")));
    return list;
}
std::size_t occupancy(const Json& s) { return s["players"].size() + s["reservations"].size(); }
std::string presence(const std::string& server) { return "LudisseyPresence_" + server; }
Json roster(const Json& s) {
    std::vector<std::string> names;
    for (const auto& p : s["players"].items()) names.push_back(p["name"].string());
    std::sort(names.begin(), names.end());
    Json result = Json::array();
    for (auto& name : names) result.push(name);
    return result;
}
void sweep(Json& record, double moment) {
    for (auto& s : record["servers"].items()) {
        std::erase_if(s["reservations"].items(), [&](const auto& r) { return r["expires"].number() <= moment; });
        std::erase_if(s["players"].items(), [&](const auto& p) { return moment - p["seen"].number() >= rules::playerTimeoutSeconds * 1000; });
        if (occupancy(s)) s.set("emptySince", nullptr);
        else if (s["emptySince"].isNull()) s.set("emptySince", moment);
    }
    std::erase_if(record["servers"].items(), [&](const auto& s) { return !s["emptySince"].isNull() && moment - s["emptySince"].number() >= rules::emptyServerSeconds * 1000; });
}
struct Found {
    Json* server = nullptr;
    Json* entry = nullptr;
    bool reservation = false;
    std::size_t index = 0;
};
Found locate(Json& record, const std::string& id) {
    for (auto& s : record["servers"].items()) {
        for (const char* key : {"reservations", "players"}) {
            auto& list = s[key].items();
            for (std::size_t i = 0; i < list.size(); ++i) if (list[i]["session"] == Json(id)) return {&s, &list[i], std::string(key) == "reservations", i};
        }
    }
    return {};
}
Found member(Json& record, const std::string& id, const std::string& player) {
    auto found = locate(record, id);
    if (!found.entry || found.reservation || (*found.entry)["player"] != Json(player)) refuse("SessionEnded", wording(localizable("Non sei più connesso a questo server")));
    return found;
}
const std::vector<std::string>& handlers() {
    static const std::vector<std::string> value = {
        "LudisseyAcquireAvatarItem", "LudisseyAssetFinalize", "LudisseyAssetResolve", "LudisseyAssetUpload", "LudisseyAvatar", "LudisseyBootstrap", "LudisseyChat", "LudisseyDeleteWorld", "LudisseyDiscover", "LudisseyExperience", "LudisseyExperienceData", "LudisseyFriends", "LudisseyHeartbeat", "LudisseyLeaveSession", "LudisseyOpenSession", "LudisseyPlayerAvatar", "LudisseyPlayerProfile", "LudisseyPortraits", "LudisseyPublishWorld", "LudisseyReadySession", "LudisseyRemoveFriend", "LudisseyRequestFriend", "LudisseyRequestSession", "LudisseyRespondFriend", "LudisseySaveAvatar", "LudisseySaveWorld", "LudisseyServerInfo", "LudisseyServerState", "LudisseyUnpublishWorld", "LudisseyWorlds"};
    return value;
}
}

ServerState::ServerState() : state(Json::object({{"serial", 0}, {"players", Json::object()}, {"groups", Json::object()}, {"entities", Json::object()}, {"members", Json::object()}})) {
    clock = [] { return std::floor(std::chrono::duration<double, std::milli>(std::chrono::system_clock::now().time_since_epoch()).count()); };
    random = [] {
        unsigned char bytes[32];
        arc4random_buf(bytes, sizeof(bytes));
        std::string result;
        for (auto byte : bytes) result += hexByte(byte);
        return result;
    };
}
Json& ServerState::account(const std::string& id) {
    if (!std::as_const(state)["players"].contains(id)) apiError("AccountNotFound");
    return state["players"][id];
}
Json ServerState::read(const std::string& id, const std::string& key, Json fallback) {
    const auto& values = std::as_const(account(id))["readOnly"];
    return values.contains(key) ? parsed(values[key].string()) : fallback;
}
void ServerState::save(const std::string& id, const std::string& key, const Json& value) { account(id)["readOnly"].set(key, encode(value)); }
Json ServerState::profile(const std::string& id) { return read(id, "LudisseyProfile"); }
void ServerState::enroll(const std::string& id, const std::string& name, const Json& p) {
    if (!std::as_const(state)["players"].contains(id)) {
        state["players"].set(id, Json::object({{"username", name}, {"readOnly", Json::object()}, {"user", Json::object()}, {"inventory", Json::array()}, {"friends", Json::array()}, {"entity", "e" + id}}));
        state["entities"].set("e" + id, Json::object({{"version", 0}, {"objects", Json::object()}, {"files", Json::object()}}));
    }
    account(id).set("username", name);
    save(id, "LudisseyProfile", p);
}
std::string ServerState::identifier() {
    auto r = random();
    if (r.size() != 64 || r.find_first_not_of("0123456789abcdef") != std::string::npos) fail(wording(localizable("Generatore casuale non valido")));
    auto n = std::string("0123456789abcdef").find(r[16]);
    return r.substr(0, 8) + "-" + r.substr(8, 4) + "-4" + r.substr(13, 3) + "-" + "89ab"[n % 4] + r.substr(17, 3) + "-" + r.substr(20, 12);
}
Json ServerState::catalog() {
    Json result = title.contains("LudisseyAvatarCatalog") ? parsed(title["LudisseyAvatarCatalog"].string()) : Json::array();
    if (!result.isArray()) return Json::array();
    std::erase_if(result.items(), [](const auto& item) { return !item["id"].isString() || !item["name"].isString() ||
        std::find(rules::avatar::categories.begin(), rules::avatar::categories.end(), item["category"].string()) == rules::avatar::categories.end() || (item.contains("asset") && !asset(item["asset"])); });
    return result;
}
Json ServerState::inventory(const std::string& id) {
    auto result = read(id, "LudisseyAvatarInventory", Json::array());
    return result.isArray() ? result : Json::array();
}
Json ServerState::published(const std::string& id) {
    const auto& values = std::as_const(state)["groups"]["LudisseyPublished"];
    return values.contains(id) ? parsed(values[id].string()) : Json();
}
Json ServerState::migrate(Json w) {
    if (w.isNull()) return w;
    if (w["lighting"].isNull()) w.set("lighting", lighting(nullptr));
    if (w["terrainDigest"].string().empty()) w.set("terrainDigest", digest(terrainText(terrain(nullptr))).substr(0, 16));
    if (!w["schemaVersion"].number()) w.set("schemaVersion", 1);
    if (w["schemaVersion"].number() < 3) {
        auto& blocks = w["blocks"].items();
        if (std::none_of(blocks.begin(), blocks.end(), [](const auto& b) { return b["name"] == Json(rules::baseplate::name); })) blocks.insert(blocks.begin(), baseplate(w["id"].string()));
        w.set("genre", rules::defaultGenre);
        w.set("serverSize", rules::serverSize::standard);
        if (!w.contains("createdAt")) w.set("createdAt", nullptr);
        if (!w["revision"].number()) w.set("revision", 0);
        w.set("schemaVersion", 3);
    }
    if (w["schemaVersion"].number() >= rules::lim::schemaVersion && w["schemaVersion"].number() < rules::schemaVersion) w.set("schemaVersion", rules::schemaVersion);
    return w;
}
Json ServerState::stored(const std::string& owner, const std::string& id, const std::string& prefix) {
    Json value;
    try { value = read(owner, prefix + id); } catch (const std::exception&) { refuse("WorldCorrupt", wording(localizable("Dati dell’esperienza danneggiati"))); }
    if (!value.isNull() && !intact(value)) refuse("WorldCorrupt", wording(localizable("Dati dell’esperienza danneggiati")));
    return migrate(value);
}
Json ServerState::attach(const std::string& owner, Json w, const std::string& prefix) {
    if (w.isNull()) return w;
    auto id = w["id"].string();
    std::string ground, objects;
    for (int i = 0; i < w["terrainParts"].integer(); ++i) ground += read(owner, prefix + "Terrain_" + id + "_" + std::to_string(i), "").string();
    for (int i = 0; i < w["instanceParts"].integer(); ++i) objects += read(owner, prefix + "Instances_" + id + "_" + std::to_string(i), "").string();
    w.set("terrain", terrain(ground.empty() ? Json() : parsed(ground)));
    migrateWater(w);
    if (w["schemaVersion"].number() < 4) {
        Json code;
        if (prefix == "Published") code = read(owner, "PublishedScripts_" + id);
        else {
            auto key = "Studio_" + replaceAll(id, "-", "");
            const auto& user = std::as_const(account(owner))["user"];
            if (user.contains(key)) code = parsed(user[key].string());
        }
        Json records = Json::array();
        for (const auto& b : std::as_const(w)["blocks"].items()) for (const auto& s : std::as_const(code)["parts"][b["id"].string()]["scripts"].items()) {
            if (!server_rules::identifier(s["id"]) || !s["source"].isString()) continue;
            records.push(Json::object({{"id", s["id"]}, {"className", "Script"}, {"name", s["name"].string().empty() ? "Script" : textPrefix(s["name"].string(), rules::instances::nameCharacters)}, {"parent", b["id"]},
                {"properties", Json::object({{"Disabled", false}, {"Source", Json::object({{"String", textPrefix(s["source"].string(), rules::instances::sourceCharacters)}})}})}}));
        }
        w.set("instances", records);
        w.set("schemaVersion", 4);
    } else w.set("instances", objects.empty() ? Json::array() : parsed(objects));
    w.erase("terrainParts"); w.erase("instanceParts");
    return w;
}
Json ServerState::experience(const std::string& player, const Json& value) {
    if (!server_rules::identifier(value)) refuse("ExperienceUnavailable", wording(localizable("Esperienza non valida")));
    auto id = value.string();
    auto entry = published(id);
    bool own = includes(read(player, "LudisseyWorlds", Json::array()), value);
    std::string owner = !entry.isNull() ? entry["owner"].string() : own ? player : "";
    if (owner.empty()) refuse("ExperienceUnavailable", wording(localizable("Esperienza non disponibile")));
    Json live = entry.isNull() ? Json() : stored(owner, id, "PublishedWorld_");
    Json w = live.isNull() ? attach(owner, stored(owner, id, "World_"), "") : attach(owner, live, "Published");
    if (w.isNull()) refuse("ExperienceUnavailable", wording(localizable("Esperienza rimossa")));
    w.set("published", !entry.isNull());
    return w;
}
Json ServerState::persist(const std::string& player, const Json& w, bool publish) {
    auto p = profile(player);
    if (!server_rules::identifier(w["id"])) fail(wording(localizable("Esperienza non valida")));
    auto id = w["id"].string();
    auto entry = published(id);
    if (!entry.isNull() && entry["owner"] != Json(player)) fail(wording(localizable("Non puoi modificare esperienze di altri giocatori")));
    if (!w["blocks"].isArray() || w["blocks"].size() > rules::world::parts) fail(wording(localizable("Massimo {0} oggetti per esperienza"), std::to_string(rules::world::parts)));
    std::set<std::string> ids;
    Json parts = Json::array();
    for (const auto& b : w["blocks"].items()) parts.push(part(b, ids));
    auto ground = terrain(w["terrain"]);
    auto groundParts = chunks(terrainText(ground), rules::terrain::keyCharacters);
    if (groundParts.size() > rules::terrain::keys) fail(wording(localizable("Terreno troppo grande: riduci le aree modificate")));
    auto records = instances(w["instances"], ids);
    auto objectText = encode(records);
    auto objectParts = records.size() ? chunks(objectText, rules::instances::keyCharacters) : std::vector<std::string>();
    if (objectParts.size() > rules::instances::keys) fail(wording(localizable("Script e istanze superano lo spazio disponibile")));
    hierarchy(parts, records);
    auto previous = stored(player, id, "World_");
    auto moment = iso(clock());
    auto titleText = text(w["title"], rules::world::titleCharacters);
    if (std::find(rules::genres.begin(), rules::genres.end(), w["genre"].string()) == rules::genres.end()) fail(wording(localizable("Genere non valido")));
    double capacity = w["serverSize"].number();
    if (!w["serverSize"].isNumber() || !std::isfinite(capacity) || std::floor(capacity) != capacity || capacity < rules::serverSize::minimum || capacity > rules::serverSize::maximum) fail(wording(localizable("Dimensione del server non valida")));
    Json language = w.contains("languageVersion") ? w["languageVersion"] : Json(std::string(rules::lim::languageVersion));
    if (!language.isString() || std::find(rules::lim::languageVersions.begin(), rules::lim::languageVersions.end(), language.string()) == rules::lim::languageVersions.end())
        refuse("VersionMismatch", wording(localizable("Questa esperienza richiede una versione più nuova di Ludissey. Aggiorna l’app.")));
    Json api = w.contains("engineApiVersion") ? w["engineApiVersion"] : Json(rules::lim::engineApiVersion);
    if (!api.isNumber() || std::floor(api.number()) != api.number() || api.number() < 1 || api.number() > rules::lim::engineApiVersion)
        refuse("VersionMismatch", wording(localizable("Questa esperienza richiede una versione più nuova di Ludissey. Aggiorna l’app.")));
    Json result = Json::object({{"id", id}, {"schemaVersion", w["schemaVersion"].isNumber() && w["schemaVersion"].number() >= rules::lim::schemaVersion && w["schemaVersion"].number() <= rules::schemaVersion ? rules::schemaVersion : 4}, {"languageVersion", language}, {"engineApiVersion", api}, {"title", titleText}, {"blocks", parts}, {"owner", player}, {"author", p["username"]},
        {"genre", w["genre"]}, {"serverSize", capacity}, {"lighting", lighting(w["lighting"])}, {"terrainDigest", digest(terrainText(ground)).substr(0, 16)}, {"instancesDigest", digest(objectText).substr(0, 16)},
        {"createdAt", previous.isNull() ? Json(moment) : previous["createdAt"]}, {"updatedAt", previous.isNull() ? Json(moment) : previous["updatedAt"]}, {"revision", previous.isNull() ? Json(0) : previous["revision"]}});
    for (auto k : {"icon", "thumbnail"}) if (w[k].isString() && isImageAsset(w[k].string())) result.set(k, w[k]);
    if (previous.isNull() || publish || content(previous) != content(result)) { result.set("updatedAt", moment); result.set("revision", previous["revision"].number() + 1); }
    if (textLength(encode(result)) > rules::world::payloadCharacters) fail(wording(localizable("Esperienza troppo grande: riduci gli oggetti")));
    if (publish) {
        auto refs = references(result, records);
        auto entity = account(player)["entity"].string();
        for (const auto& a : refs.items()) if (!std::as_const(state)["entities"][entity]["files"][a.string()]["finalized"].boolean()) refuse("AssetMissing", wording(localizable("Risorsa non caricata: {0}"), a.string()));
    }
    auto index = read(player, "LudisseyWorlds", Json::array());
    if (!includes(index, id)) {
        if (index.size() >= rules::world::experiencesPerDeveloper) fail("Massimo " + std::to_string(rules::world::experiencesPerDeveloper) + " esperienze");
        index.push(id);
    }
    Json document = result;
    document.set("terrainParts", groundParts.size()); document.set("instanceParts", objectParts.size());
    auto store = [&](const std::string& prefix) {
        save(player, prefix + "World_" + id, document);
        for (std::size_t i = 0; i < groundParts.size(); ++i) save(player, prefix + "Terrain_" + id + "_" + std::to_string(i), groundParts[i]);
        for (std::size_t i = 0; i < objectParts.size(); ++i) save(player, prefix + "Instances_" + id + "_" + std::to_string(i), objectParts[i]);
        auto& data = account(player)["readOnly"];
        if (prefix == "Published") data.erase("PublishedScripts_" + id);
        for (std::size_t i = groundParts.size(); i < rules::terrain::keys; ++i) data.erase(prefix + "Terrain_" + id + "_" + std::to_string(i));
        for (std::size_t i = objectParts.size(); i < rules::instances::keys; ++i) data.erase(prefix + "Instances_" + id + "_" + std::to_string(i));
    };
    save(player, "LudisseyWorlds", index);
    store("");
    if (publish) store("Published");
    result.set("terrain", ground); result.set("instances", records);
    if (publish) state["groups"]["LudisseyPublished"].set(id, encode(summary(result)));
    return result;
}

Json ServerState::observe(const Json& world) {
    auto entity = account(world["owner"].string())["entity"].string();
    const auto& raw = std::as_const(state)["entities"][entity]["objects"][replaceAll(world["id"].string(), "-", "")];
    Json record = Json::object({{"visits", raw["visits"].number()}, {"servers", raw["servers"].isArray() ? raw["servers"] : Json::array()}});
    sweep(record, clock());
    return record;
}
Json ServerState::transact(const Json& world, const std::function<Json(Json&, double)>& mutate) {
    auto record = observe(world);
    auto result = mutate(record, clock());
    if (textLength(encode(record)) > rules::registryCharacters) refuse("RegistryFull", wording(localizable("Questa esperienza è piena. Riprova tra poco.")));
    auto entity = account(world["owner"].string())["entity"].string();
    auto& storage = state["entities"][entity];
    storage["objects"].set(replaceAll(world["id"].string(), "-", ""), record);
    storage.set("version", storage["version"].number() + 1);
    return result;
}
Json ServerState::describe(const Json& s) {
    namespace states = rules::serverStates;
    const auto& reservations = s["reservations"].items();
    bool opened = std::any_of(reservations.begin(), reservations.end(), [](const Json& r) { return r["opened"].boolean(false); });
    std::string status(!s["players"].size() ? (reservations.empty() ? states::draining : opened ? states::ready : states::provisioning) : static_cast<double>(occupancy(s)) >= s["capacity"].number() ? states::full : states::joinable);
    return Json::object({{"id", s["id"]}, {"sequence", s["sequence"]}, {"capacity", s["capacity"]}, {"players", s["players"].size()}, {"reserved", s["reservations"].size()}, {"state", status}, {"region", region}, {"observedAt", iso(clock())}});
}
void ServerState::leavePresence(const std::string& server, const std::string& player, const std::string& session) {
    auto key = presence(server);
    if (!std::as_const(state)["groups"].contains(key)) state["groups"].set(key, Json::object());
    if (!std::as_const(state)["members"].contains(key)) state["members"].set(key, Json::array());
    state["groups"][key].erase(session);
    if (std::as_const(state)["members"][key].isArray()) std::erase_if(state["members"][key].items(), [&](const auto& p) { return p == Json(player); });
}
void ServerState::prunePresence(const Json& server) {
    std::set<std::string> live;
    for (const auto& p : server["players"].items()) live.insert(p["session"].string());
    auto key = presence(server["id"].string());
    if (std::as_const(state)["groups"].contains(key)) std::erase_if(state["groups"][key].members(), [&](const auto& pair) { return !live.contains(pair.first); });
}
Json ServerState::sessions(const std::string& player, const std::string& name, const Json& args) {
    auto w = experience(player, name == "LudisseyExperience" ? args["id"] : args["experienceId"]);
    if (name == "LudisseyExperience") {
        auto record = observe(w);
        auto detail = summary(w);
        detail.set("published", w["published"]); detail.set("visits", record["visits"]);
        Json servers = Json::array();
        for (const auto& s : std::as_const(record)["servers"].items()) if (s["players"].size()) servers.push(describe(s));
        Json result = Json::object({{"experience", detail}, {"servers", servers}});
        if (args["world"] == Json(true)) result.set("world", w);
        return result;
    }
    if (name == "LudisseyRequestSession") {
        auto p = profile(player);
        auto token = random();
        Json released = Json::array();
        auto ticket = transact(w, [&](Json& record, double moment) {
            for (auto& s : record["servers"].items()) {
                for (const auto& entry : std::as_const(s)["players"].items()) if (entry["player"] == Json(player)) released.push(Json::object({{"server", s["id"]}, {"session", entry["session"]}}));
                for (const auto* key : {"players", "reservations"}) std::erase_if(s[key].items(), [&](const auto& entry) { return entry["player"] == Json(player); });
                if (!occupancy(s) && s["emptySince"].isNull()) s.set("emptySince", moment);
            }
            Json* target = nullptr;
            auto& servers = record["servers"].items();
            auto requested = args["serverId"].string();
            for (auto& s : servers) {
                if (requested.empty() ? static_cast<double>(occupancy(s)) < s["capacity"].number() : s["id"] == Json(requested)) { target = &s; break; }
            }
            if (!requested.empty()) {
                if (!target) refuse("ServerClosed", wording(localizable("Il server non è più disponibile")));
                if (static_cast<double>(occupancy(*target)) >= (*target)["capacity"].number()) refuse("ServerFull", wording(localizable("Il server è pieno")));
            }
            if (!target) {
                if (servers.size() >= rules::maxServers) refuse("ExperienceFull", wording(localizable("Tutti i server sono pieni")));
                int sequence = 1;
                while (std::any_of(servers.begin(), servers.end(), [&](const auto& s) { return s["sequence"] == Json(sequence); })) ++sequence;
                auto serverId = identifier();
                servers.push_back(Json::object({{"id", serverId}, {"sequence", sequence}, {"capacity", w["serverSize"]}, {"createdAt", moment}, {"emptySince", nullptr}, {"players", Json::array()}, {"reservations", Json::array()}, {"chat", Json::array()}, {"chatSequence", 0}}));
                std::sort(servers.begin(), servers.end(), [](const auto& a, const auto& b) { return a["sequence"].number() < b["sequence"].number(); });
                for (auto& s : servers) if (s["id"] == Json(serverId)) { target = &s; break; }
            }
            auto id = identifier();
            double expires = moment + rules::reservationSeconds * 1000;
            (*target)["reservations"].push(Json::object({{"session", id}, {"player", player}, {"name", p["username"]}, {"hash", digest(token).substr(0, 32)}, {"expires", expires}, {"opened", false}}));
            target->set("emptySince", nullptr);
            return Json::object({{"sessionId", id}, {"joinTicket", token}, {"expiresAt", std::floor(expires / 1000)}, {"playerProtocol", rules::playerProtocol}, {"nonce", digest(id + token).substr(0, 32)}, {"experienceId", w["id"]}, {"serverId", (*target)["id"]}, {"serverSequence", (*target)["sequence"]}});
        });
        for (const auto& entry : released.items()) leavePresence(entry["server"].string(), player, entry["session"].string());
        return ticket;
    }
    if (!server_rules::identifier(args["sessionId"])) refuse("SessionRejected", wording(localizable("Sessione non valida")));
    auto id = args["sessionId"].string();
    if (name == "LudisseyOpenSession") {
        auto assigned = transact(w, [&](Json& record, double) {
            auto found = locate(record, id);
            if (!found.entry || !found.reservation) {
                if (found.entry) refuse("SessionUsed", wording(localizable("Questo accesso è già stato utilizzato")));
                refuse("SessionExpired", wording(localizable("Sessione scaduta")));
            }
            auto& r = *found.entry;
            if (r["player"] != Json(player) || r["hash"] != Json(digest(args["joinTicket"].string()).substr(0, 32))) refuse("SessionRejected", wording(localizable("Accesso alla sessione rifiutato")));
            if (r["opened"].boolean()) refuse("SessionUsed", wording(localizable("Questo accesso è già stato utilizzato")));
            r.set("opened", true);
            return describe(*found.server);
        });
        return Json::object({{"world", w}, {"server", assigned}});
    }
    if (name == "LudisseyReadySession") {
        if (args["revision"].isNumber() && args["revision"] != w["revision"]) refuse("WorldChanged", wording(localizable("L’esperienza è appena stata aggiornata. Riprova.")));
        auto ready = transact(w, [&](Json& record, double moment) {
            auto found = locate(record, id);
            bool repeated = found.entry && !found.reservation;
            if (repeated) {
                if ((*found.entry)["player"] != Json(player)) refuse("SessionUsed", wording(localizable("Questo accesso è già stato utilizzato")));
                found.entry->set("seen", moment);
            } else {
                if (!found.entry) refuse("SessionExpired", wording(localizable("Sessione scaduta")));
                Json r = *found.entry;
                if (r["player"] != Json(player) || !r["opened"].boolean()) refuse("SessionRejected", wording(localizable("Accesso alla sessione rifiutato")));
                auto& reservations = (*found.server)["reservations"].items();
                reservations.erase(reservations.begin() + static_cast<std::ptrdiff_t>(found.index));
                (*found.server)["players"].push(Json::object({{"session", id}, {"player", player}, {"name", r["name"]}, {"seen", moment}}));
                record.set("visits", record["visits"].number() + 1);
            }
            Json result = Json::object({{"server", describe(*found.server)}, {"roster", roster(*found.server)}, {"visits", record["visits"]}, {"presence", presence((*found.server)["id"].string())}, {"player", player}});
            if (repeated) result.set("repeated", true);
            result.set("record", *found.server);
            return result;
        });
        auto group = ready["presence"].string();
        if (!std::as_const(state)["groups"].contains(group)) state["groups"].set(group, Json::object());
        if (!std::as_const(state)["members"].contains(group)) state["members"].set(group, Json::array());
        if (!includes(std::as_const(state)["members"][group], player)) state["members"][group].push(player);
        prunePresence(ready["record"]); ready.erase("record");
        return ready;
    }
    if (name == "LudisseyHeartbeat") {
        auto beat = transact(w, [&](Json& record, double moment) {
            auto found = member(record, id, player);
            found.entry->set("seen", moment);
            return Json::object({{"server", describe(*found.server)}, {"roster", roster(*found.server)}, {"record", *found.server}});
        });
        prunePresence(beat["record"]); beat.erase("record");
        return beat;
    }
    if (name == "LudisseyServerState") {
        auto record = observe(w);
        auto found = member(record, id, player);
        Json chat = Json::array();
        for (const auto& m : std::as_const(*found.server)["chat"].items()) if (m["sequence"].number() > args["after"].number()) chat.push(m);
        return Json::object({{"server", describe(*found.server)}, {"roster", roster(*found.server)}, {"chat", chat}});
    }
    if (name == "LudisseyChat") {
        auto message = text(args["message"], rules::chatCharacters);
        return transact(w, [&](Json& record, double moment) {
            auto found = member(record, id, player);
            Json recent = Json::array();
            for (const auto& t : std::as_const(*found.entry)["chatTimes"].items())
                if (moment - t.number() < rules::chatWindowSeconds * 1000) recent.push(t);
            if (static_cast<int>(recent.size()) >= rules::chatBurst) refuse("ChatRateLimited", wording(localizable("Piano! Aspetta qualche secondo prima di scrivere ancora.")));
            recent.push(moment);
            found.entry->set("chatTimes", recent);
            found.entry->set("seen", moment);
            auto playerName = (*found.entry)["name"];
            auto& s = *found.server;
            s.set("chatSequence", s["chatSequence"].number() + 1);
            s["chat"].push(Json::object({{"sequence", s["chatSequence"]}, {"name", playerName}, {"text", message}, {"at", moment}}));
            auto& chat = s["chat"].items();
            if (chat.size() > rules::chatMessages) chat.erase(chat.begin(), chat.end() - rules::chatMessages);
            return Json::object({{"sequence", s["chatSequence"]}});
        });
    }
    if (name == "LudisseyLeaveSession") {
        auto result = transact(w, [&](Json& record, double moment) {
            auto found = locate(record, id);
            if (!found.entry || (*found.entry)["player"] != Json(player)) return Json::object({{"left", false}});
            auto& entries = (*found.server)[found.reservation ? "reservations" : "players"].items();
            entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(found.index));
            if (!occupancy(*found.server)) found.server->set("emptySince", moment);
            return Json::object({{"left", true}, {"server", describe(*found.server)}});
        });
        if (result["left"].boolean()) leavePresence(result["server"]["id"].string(), player, id);
        return result;
    }
    fail(wording(localizable("Funzione sconosciuta: {0}"), name));
}

Json ServerState::invoke(const std::string& player, const std::string& name, const Json& args) {
    if (std::find(handlers().begin(), handlers().end(), name) == handlers().end()) fail(wording(localizable("Funzione sconosciuta: {0}"), name));
    if (name == "LudisseyServerInfo") {
        Json list = Json::array();
        for (const auto& handler : handlers()) list.push(handler);
        return Json::object({{"schemaVersion", rules::schemaVersion}, {"playerProtocol", rules::playerProtocol}, {"handlers", list}});
    }
    auto p = profile(player);
    if (name == "LudisseyBootstrap") {
        if (p.isNull()) {
            if (args["gender"] != Json("male") && args["gender"] != Json("female")) return Json::object({{"needsProfile", true}});
            p = Json::object({{"username", account(player)["username"].stringOr(player)}, {"gender", args["gender"]}, {"createdAt", iso(clock())}});
            save(player, "LudisseyProfile", p);
        }
        return Json::object({{"profile", p}, {"id", player}});
    }
    if (p.isNull()) fail(wording(localizable("Completa il tuo avatar")));
    if (name == "LudisseyAvatar" || name == "LudisseySaveAvatar" || name == "LudisseyPlayerAvatar" || name == "LudisseyAcquireAvatarItem") {
        auto known = catalog();
        if (name == "LudisseyAcquireAvatarItem") {
            Json item;
            for (const auto& candidate : known.items()) if (candidate["id"] == args["itemId"]) { item = candidate; break; }
            if (item.isNull()) fail(wording(localizable("Oggetto non presente nel catalogo")));
            if (item["free"] != Json(true)) fail(wording(localizable("Oggetto non disponibile gratuitamente")));
            auto owned = inventory(player);
            if (std::none_of(owned.items().begin(), owned.items().end(), [&](const auto& e) { return e["itemID"] == item["id"]; })) {
                owned.push(Json::object({{"id", identifier()}, {"itemID", item["id"]}, {"acquiredAt", iso(clock())}}));
                save(player, "LudisseyAvatarInventory", owned);
            }
            return Json::object({{"catalog", known}, {"inventory", owned}});
        }
        auto target = name == "LudisseyPlayerAvatar" ? text(args["id"], 32) : player;
        auto targetProfile = target == player ? p : profile(target);
        if (targetProfile.isNull()) fail(wording(localizable("Giocatore non trovato")));
        auto owned = inventory(target);
        Json raw = name == "LudisseySaveAvatar" ? args["avatar"] : read(target, "LudisseyAvatar");
        if (name == "LudisseyAvatar" && raw.isNull()) {
            const auto& legacy = std::as_const(account(player))["user"]["LudisseyAvatar"];
            if (legacy.isString()) raw = parsed(legacy.string());
        }
        auto avatar = name == "LudisseyAvatar" && raw.isNull() ? Json() : server_rules::avatar(raw, targetProfile["username"].string(), targetProfile["gender"].string(), owned, known);
        if (name == "LudisseySaveAvatar") save(player, "LudisseyAvatar", avatar);
        Json result = Json::object({{"avatar", avatar}});
        if (name == "LudisseyAvatar") { result.set("catalog", known); result.set("inventory", owned); }
        return result;
    }
    if (name == "LudisseyExperienceData") {
        auto w = experience(player, args["experienceId"]);
        auto key = "LudisseyData_" + replaceAll(w["id"].string(), "-", "");
        if (args["action"] == Json("load")) return Json::object({{"value", read(player, key, Json())}});
        if (args["action"] != Json("save")) refuse("InvalidData", wording(localizable("Operazione sui dati non valida")));
        if (!args.contains("value") || textLength(encode(args["value"])) > rules::scriptData::characters)
            refuse("InvalidData", wording(localizable("Dati troppo grandi: massimo {0} caratteri"), std::to_string(rules::scriptData::characters)));
        const auto& stored = std::as_const(account(player))["readOnly"];
        if (!stored.contains(key)) {
            int count = 0;
            for (const auto& [k, v] : stored.members()) count += k.rfind("LudisseyData_", 0) == 0;
            if (count >= rules::scriptData::experiences) refuse("InvalidData", wording(localizable("Troppe esperienze con dati salvati")));
        }
        save(player, key, args["value"]);
        return Json::object({{"saved", true}});
    }
    if (name == "LudisseyFriends") {
        Json friends = Json::array();
        for (const auto& f : std::as_const(account(player))["friends"].items()) {
            const std::string id = f["FriendPlayFabId"].string();
            const auto& players = std::as_const(state)["players"];
            friends.push(players.contains(id) ? Json::object({{"FriendPlayFabId", id}, {"Username", players[id]["username"]}}) : Json::object({{"FriendPlayFabId", id}}));
        }
        return Json::object({{"friends", friends}, {"requests", read(player, "LudisseyRequests", Json::array())}});
    }
    if (name == "LudisseyPlayerProfile") {
        auto id = text(args["id"], 32);
        Json target = std::as_const(state)["players"].contains(id) ? profile(id) : Json();
        if (target.isNull()) fail(wording(localizable("Giocatore non trovato")));
        bool self = id == player;
        const auto& list = std::as_const(account(id))["friends"].items();
        bool friendly = !self && std::any_of(list.begin(), list.end(), [&](const auto& f) { return f["FriendPlayFabId"] == Json(player); });
        auto asked = [&](const std::string& owner, const std::string& who) {
            auto requests = read(owner, "LudisseyRequests", Json::array());
            return std::any_of(requests.items().begin(), requests.items().end(), [&](const auto& r) { return r["id"] == Json(who); });
        };
        const auto& user = std::as_const(account(id))["user"];
        Json preferences = Json::object();
        if (user["LudisseyPreferences"].isString())
            if (auto decoded = Json::parse(user["LudisseyPreferences"].string()); decoded && decoded->isObject()) preferences = *decoded;
        double seen = user["LudisseyPresence"].isString() ? parseNumber(user["LudisseyPresence"].string()).value_or(0) : 0;
        double age = clock() / 1000 - seen;
        Json experiences = Json::array();
        for (const auto& entry : invoke(player, "LudisseyDiscover", Json::object())["worlds"].items())
            if (entry["owner"] == Json(id)) experiences.push(entry);
        Json result = Json::object({{"id", id}, {"username", target["username"]}, {"gender", target["gender"] == Json("female") ? "female" : "male"}, {"createdAt", target["createdAt"].isString() ? target["createdAt"] : Json()}, {"self", self}, {"friend", friendly},
                                    {"requested", !self && asked(id, player)}, {"requesting", !self && asked(player, id)}, {"publicProfile", user.contains("LudisseyPublicProfile")}, {"friends", static_cast<double>(list.size())},
                                    {"online", seen > 0 && age >= 0 && age < rules::account::presenceSeconds}, {"experiences", experiences}});
        if (user.contains("LudisseyPublicProfile") || friendly) {
            result.set("bio", preferences["bio"].isString() ? utf8Prefix(preferences["bio"].string(), rules::account::bioCharacters) : std::string());
            Json shown = Json::array();
            for (std::size_t i = 0; i < list.size() && i < 6; ++i) {
                auto other = list[i]["FriendPlayFabId"].string();
                Json known = std::as_const(state)["players"].contains(other) ? profile(other) : Json();
                shown.push(Json::object({{"id", other}, {"username", known.isNull() ? list[i]["Username"].stringOr(other) : known["username"].string()}, {"gender", known["gender"] == Json("female") ? "female" : "male"}}));
            }
            result.set("friendList", shown);
        }
        return result;
    }
    if (name == "LudisseyRequestFriend") {
        auto target = text(args["id"], 32);
        account(target);
        if (target == player) fail(wording(localizable("Non puoi aggiungere te stesso")));
        if (profile(target).isNull()) fail(wording(localizable("Questo giocatore non ha ancora finito di registrarsi.")));
        const auto& friends = std::as_const(account(player))["friends"].items();
        if (std::any_of(friends.begin(), friends.end(), [&](const auto& f) { return f["FriendPlayFabId"] == Json(target); })) fail(wording(localizable("Siete già amici")));
        auto requests = read(target, "LudisseyRequests", Json::array());
        if (std::none_of(requests.items().begin(), requests.items().end(), [&](const auto& r) { return r["id"] == Json(player); })) {
            if (requests.size() >= 50) fail(wording(localizable("Questo giocatore ha già troppe richieste di amicizia.")));
            requests.push(Json::object({{"id", player}, {"username", p["username"]}, {"at", iso(clock())}}));
            save(target, "LudisseyRequests", requests);
        }
        return Json::object({{"sent", true}});
    }
    if (name == "LudisseyRespondFriend" || name == "LudisseyRemoveFriend") {
        auto target = args["id"].string();
        auto requests = read(player, "LudisseyRequests", Json::array());
        bool respond = name == "LudisseyRespondFriend";
        auto list = respond ? requests : account(player)["friends"];
        if (std::none_of(list.items().begin(), list.items().end(), [&](const auto& e) { return e[respond ? "id" : "FriendPlayFabId"] == args["id"]; })) fail(respond ? wording(localizable("Richiesta non trovata")) : wording(localizable("Amicizia non trovata")));
        bool accept = args["accept"] == Json(true);
        if (!respond || accept) for (const auto& pair : {std::pair(player, target), std::pair(target, player)}) {
            auto& friends = account(pair.first)["friends"].items();
            if (!respond) std::erase_if(friends, [&](const auto& f) { return f["FriendPlayFabId"] == Json(pair.second); });
            else friends.push_back(Json::object({{"FriendPlayFabId", pair.second}}));
        }
        if (respond) {
            std::erase_if(requests.items(), [&](const auto& r) { return r["id"] == args["id"]; });
            save(player, "LudisseyRequests", requests);
        }
        return Json::object({{respond ? "accepted" : "removed", respond ? accept : true}});
    }
    if (name == "LudisseySaveWorld" || name == "LudisseyPublishWorld") return persist(player, args["world"], name == "LudisseyPublishWorld");
    if (name == "LudisseyWorlds") {
        Json result = Json::array();
        auto index = read(player, "LudisseyWorlds", Json::array());
        for (const auto& id : index.items()) {
            Json raw;
            try { raw = read(player, "World_" + id.string()); } catch (const std::exception&) { continue; }
            if (intact(raw)) result.push(attach(player, migrate(raw), ""));
        }
        return Json::object({{"worlds", result}});
    }
    if (name == "LudisseyDiscover") {
        Json result = Json::array();
        for (const auto& [id, value] : std::as_const(state)["groups"]["LudisseyPublished"].members()) {
            auto entry = parsed(value.string());
            if (entry["genre"].string().empty()) entry.set("genre", rules::defaultGenre);
            if (!entry["serverSize"].number()) entry.set("serverSize", rules::serverSize::standard);
            if (!entry.contains("createdAt")) entry.set("createdAt", nullptr);
            if (!entry.contains("updatedAt")) entry.set("updatedAt", nullptr);
            if (!entry["revision"].number()) entry.set("revision", 0);
            result.push(summary(entry));
        }
        return Json::object({{"worlds", result}});
    }
    if (name == "LudisseyUnpublishWorld") {
        auto index = read(player, "LudisseyWorlds", Json::array());
        if (!args["id"].isString() || !includes(index, args["id"])) fail(wording(localizable("Esperienza non trovata")));
        auto id = args["id"].string();
        auto entry = published(id);
        if (entry.isNull()) return Json::object({{"published", false}});
        if (entry["owner"] != Json(player)) fail(wording(localizable("Non puoi modificare esperienze di altri giocatori")));
        state["groups"]["LudisseyPublished"].erase(id);
        return Json::object({{"published", false}});
    }
    if (name == "LudisseyDeleteWorld") {
        auto id = args["id"].string();
        auto index = read(player, "LudisseyWorlds", Json::array());
        if (!includes(index, args["id"])) fail(wording(localizable("Esperienza non trovata")));
        auto entry = published(id);
        if (!entry.isNull() && entry["owner"] != Json(player)) fail(wording(localizable("Non puoi eliminare esperienze di altri giocatori")));
        if (!std::as_const(state)["groups"].contains("LudisseyPublished")) state["groups"].set("LudisseyPublished", Json::object());
        state["groups"]["LudisseyPublished"].erase(id);
        auto& data = account(player)["readOnly"];
        for (const auto* prefix : {"World_", "PublishedWorld_", "PublishedScripts_"}) data.erase(prefix + id);
        for (const auto* prefix : {"", "Published"}) {
            for (int i = 0; i < rules::terrain::keys; ++i) data.erase(std::string(prefix) + "Terrain_" + id + "_" + std::to_string(i));
            for (int i = 0; i < rules::instances::keys; ++i) data.erase(std::string(prefix) + "Instances_" + id + "_" + std::to_string(i));
        }
        std::erase_if(index.items(), [&](const auto& v) { return v == args["id"]; });
        save(player, "LudisseyWorlds", index);
        return Json::object({{"deleted", true}});
    }
    if (name == "LudisseyAssetUpload" || name == "LudisseyAssetFinalize" || name == "LudisseyAssetResolve") {
        Json w = name == "LudisseyAssetResolve" ? experience(player, args["experienceId"]) : Json();
        auto requested = assets(args);
        auto entity = account(w.isNull() ? player : w["owner"].string())["entity"].string();
        if (name == "LudisseyAssetUpload") {
            Json pending = Json::array();
            for (const auto& a : requested.items()) {
                if (std::as_const(state)["entities"][entity]["files"][a.string()]["finalized"].boolean()) continue;
                if (!std::as_const(state)["entities"][entity]["files"].contains(a.string())) state["entities"][entity]["files"].set(a.string(), Json::object({{"finalized", false}, {"size", 0}}));
                pending.push(Json::object({{"id", a}, {"url", "ludissey-emulator://blob/" + entity + "/" + a.string()}}));
            }
            return Json::object({{"pending", pending}});
        }
        if (name == "LudisseyAssetFinalize") {
            for (const auto& a : requested.items()) {
                auto key = entity + "/" + a.string();
                if (!std::as_const(state)["entities"][entity]["files"].contains(a.string()) || !blobs.contains(key)) apiError("FileNotFound");
                auto& file = state["entities"][entity]["files"][a.string()];
                file.set("finalized", true); file.set("size", blobs[key]["length"]);
            }
            auto& storage = state["entities"][entity];
            storage.set("version", storage["version"].number() + 1);
            return Json::object({{"finalized", requested}});
        }
        auto allowed = references(w, w["instances"].isArray() ? w["instances"] : Json::array());
        Json urls = Json::object();
        for (const auto& a : requested.items()) {
            if (!includes(allowed, a)) refuse("AssetRejected", wording(localizable("Risorsa non usata da questa esperienza")));
            if (!std::as_const(state)["entities"][entity]["files"][a.string()]["finalized"].boolean()) refuse("AssetMissing", wording(localizable("Risorsa non disponibile: {0}"), a.string()));
            urls.set(a.string(), "ludissey-emulator://blob/" + entity + "/" + a.string());
        }
        return Json::object({{"urls", urls}});
    }
    if (name == "LudisseyPortraits") {
        auto stored = [&](const std::string& owner, const Json& asset) {
            if (!asset.isString() || !isImageAsset(asset.string())) return false;
            auto entity = std::as_const(account(owner))["entity"].string();
            return std::as_const(state)["entities"][entity]["files"][asset.string()]["finalized"].boolean();
        };
        if (args["save"].isObject()) {
            if (!stored(player, args["save"]["bust"]) || !stored(player, args["save"]["figure"])) refuse("AssetRejected", wording(localizable("Immagine dell’avatar non valida")));
            save(player, "LudisseyPortraits", Json::object({{"bust", args["save"]["bust"]}, {"figure", args["save"]["figure"]}}));
        }
        Json players = Json::object();
        int count = 0;
        if (args["players"].isArray())
            for (const auto& id : args["players"].items()) {
                if (++count > rules::account::portraitPlayers) break;
                if (!id.isString() || id.string().empty() || id.string().size() > 32 || !std::as_const(state)["players"].contains(id.string()) || profile(id.string()).isNull()) continue;
                std::string gender = profile(id.string())["gender"] == Json("female") ? "female" : "male";
                players.set(id.string(), Json::object({{"gender", gender}}));
                Json kept = read(id.string(), "LudisseyPortraits");
                if (!kept.isObject() || !stored(id.string(), kept["bust"]) || !stored(id.string(), kept["figure"])) continue;
                auto entity = std::as_const(account(id.string()))["entity"].string();
                players.set(id.string(), Json::object({{"gender", gender}, {"bust", kept["bust"]}, {"bustUrl", "ludissey-emulator://blob/" + entity + "/" + kept["bust"].string()}, {"figure", kept["figure"]}, {"figureUrl", "ludissey-emulator://blob/" + entity + "/" + kept["figure"].string()}}));
            }
        return Json::object({{"players", players}});
    }
    if (name == "LudisseyRequestSession" && args["playerProtocol"] != Json(rules::playerProtocol)) refuse("VersionMismatch", wording(localizable("Versione di Ludissey non compatibile")));
    return sessions(player, name, args);
}

Json ServerState::client(const std::string& player, const std::string& name, const Json& args) {
    auto select = [&](const Json& values) {
        Json result = Json::object();
        for (const auto& [key, v] : values.members()) if (!args["Keys"].isArray() || includes(args["Keys"], key)) result.set(key, Json::object({{"Value", v}}));
        return Json::object({{"Data", result}});
    };
    auto update = [&](Json& values) {
        for (const auto& [key, v] : args["Data"].members()) values.set(key, v);
        for (const auto& key : args["KeysToRemove"].items()) values.erase(key.string());
    };
    if (name == "GetUserData") return select(std::as_const(account(player))["user"]);
    if (name == "UpdateUserData") { update(account(player)["user"]); return Json::object(); }
    if (name == "GetSharedGroupData" || name == "UpdateSharedGroupData") {
        auto group = args["SharedGroupId"].string();
        if (!includes(std::as_const(state)["members"][group], player)) apiError("NotAuthorized");
        if (name == "GetSharedGroupData") return select(std::as_const(state)["groups"][group]);
        update(state["groups"][group]);
        return Json::object();
    }
    fail(wording(localizable("Metodo client non emulato: {0}"), name));
}

}
