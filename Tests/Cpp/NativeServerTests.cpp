#include "Engine/Backend/ServerRules.hpp"
#include "Engine/Backend/ServerState.hpp"
#include "ServerHarness.hpp"

#include <cstdio>

using namespace ld;

namespace {
class ParityServer {
public:
    ServerState native;
    std::shared_ptr<harness::ScriptWorld> oracle = std::make_shared<harness::ScriptWorld>();
    double moment = 1790000000000;
    ParityServer() {
        oracle->setTime(moment);
        oracle->evaluate("runtime.region = 'local'");
        native.clock = [&] { return moment; };
        native.random = [&] {
            auto serial = native.state["serial"].integer() + 1;
            native.state.set("serial", serial);
            char hex[32];
            std::snprintf(hex, sizeof(hex), "%016llx", serial);
            return sha256Hex(std::string("emulator-") + hex);
        };
        for (const auto* id : {"owner", "friend", "visitor"}) {
            oracle->registerPlayer(id, id);
            native.enroll(id, id, Json::object({{"username", id}, {"gender", "male"}, {"createdAt", "2026-09-21T14:13:20.000Z"}}));
        }
        sync();
    }
    void sync() {
        oracle->evaluate("Object.assign(emulator," + native.state.dump() + "); emulator.title = " + native.title.dump() + "; emulator.blobs = " + native.blobs.dump());
    }
    void advance(double seconds) { moment += seconds * 1000; oracle->setTime(moment); }
    Json call(const std::string& name, const Json& args = Json::object(), const std::string& player = "owner", bool client = false) {
        INFO(name + " " + player + " " + args.dump());
        Json expected, actual;
        std::string expectedError, actualError;
        try { expected = client ? oracle->client(player, name, args) : oracle->invoke(player, name, args); } catch (const std::exception& e) { expectedError = e.what(); }
        try { actual = client ? native.client(player, name, args) : native.invoke(player, name, args); } catch (const std::exception& e) { actualError = e.what(); }
        REQUIRE(actualError == expectedError);
        INFO("expected " + expected.dump()); INFO("actual " + actual.dump());
        REQUIRE(actual == expected);
        auto persisted = Json::parse(oracle->evaluate("JSON.stringify({serial:emulator.serial,players:emulator.players,groups:emulator.groups,entities:emulator.entities,members:emulator.members})"));
        REQUIRE(persisted);
        for (auto k : {"serial", "players", "groups", "entities", "members"}) {
            INFO(k); INFO("native " + native.state[k].dump()); INFO("legacy " + (*persisted)[k].dump());
            REQUIRE(native.state[k] == (*persisted)[k]);
        }
        return actual;
    }
    Json world() {
        auto w = Experience::blank().json();
        w.set("title", "Arena à 🧱");
        w.set("genre", rules::defaultGenre);
        w.set("serverSize", 2);
        return w;
    }
};
}

TEST_CASE("Server nativo: account, amicizie e avatar", "[native-server]") {
    ParityServer p;
    p.call("LudisseyServerInfo");
    p.call("LudisseyBootstrap");
    p.call("LudisseyFriends");
    p.call("LudisseyRequestFriend", Json::object({{"id", "owner"}}));
    p.call("LudisseyRequestFriend", Json::object({{"id", "friend"}}));
    p.call("LudisseyRequestFriend", Json::object({{"id", "friend"}}));
    p.call("LudisseyFriends", Json::object(), "friend");
    p.call("LudisseyRespondFriend", Json::object({{"id", "owner"}, {"accept", true}}), "friend");
    p.call("LudisseyFriends");
    p.call("LudisseyRemoveFriend", Json::object({{"id", "friend"}}));
    p.call("LudisseyRemoveFriend", Json::object({{"id", "friend"}}));
    p.call("LudisseyAvatar");
    p.call("LudisseySaveAvatar", Json::object({{"avatar", Json::object({{"displayName", "  Costruttore 🧱  "}, {"skinTone", "F5CD30"}})}}));
    p.call("LudisseyAvatar");
    p.call("LudisseyPlayerAvatar", Json::object({{"id", "friend"}}));
    p.native.title.set("LudisseyAvatarCatalog", Json::array({Json::object({{"id", "hat"}, {"name", "Cappello"}, {"category", "hat"}, {"free", true}})}).dump());
    p.sync();
    p.call("LudisseyAcquireAvatarItem", Json::object({{"itemId", "hat"}}));
    p.call("LudisseyAcquireAvatarItem", Json::object({{"itemId", "hat"}}));
    p.call("LudisseySaveAvatar", Json::object({{"avatar", Json::object({{"equipped", Json::array({Json::object({{"itemID", "hat"}, {"category", "hat"}})})}})}}));
    p.call("UpdateUserData", Json::object({{"Data", Json::object({{"value", "abc"}})}}), "owner", true);
    p.call("GetUserData", Json::object(), "owner", true);
    std::string bust = std::string(64, 'a') + ".png", figure = std::string(64, 'b') + ".png";
    Json pictures = Json::object({{"assets", Json::array({bust, figure})}});
    p.call("LudisseyPortraits", Json::object({{"players", Json::array({"owner", "friend", "sconosciuto", 7})}}));
    p.call("LudisseyPortraits", Json::object({{"save", Json::object({{"bust", bust}, {"figure", figure}})}}));
    p.call("LudisseyAssetUpload", pictures);
    p.native.blobs.set("eowner/" + bust, Json::object({{"length", 5}}));
    p.native.blobs.set("eowner/" + figure, Json::object({{"length", 6}}));
    p.sync();
    p.call("LudisseyAssetFinalize", pictures);
    p.call("LudisseyPortraits", Json::object({{"save", Json::object({{"bust", bust}, {"figure", std::string(64, 'c') + ".txt"}})}}));
    auto saved = p.call("LudisseyPortraits", Json::object({{"save", Json::object({{"bust", bust}, {"figure", figure}})}, {"players", Json::array({"owner"})}}));
    CHECK(saved["players"]["owner"]["bust"] == Json(bust));
    auto seen = p.call("LudisseyPortraits", Json::object({{"players", Json::array({"owner", "friend"})}}), "friend");
    CHECK(seen["players"]["owner"]["figure"] == Json(figure));
    CHECK_FALSE(seen["players"]["friend"].contains("bust"));
    CHECK(seen["players"]["friend"]["gender"].isString());
}

TEST_CASE("Server nativo: documenti e revisioni", "[native-server]") {
    ParityServer p;
    auto w = p.world();
    auto id = w["id"];
    auto save = [&](bool publish = false) { return p.call(publish ? "LudisseyPublishWorld" : "LudisseySaveWorld", Json::object({{"world", w}})); };
    save();
    p.call("LudisseyWorlds");
    p.advance(5);
    save();
    w.set("title", "Secondo titolo");
    save();
    save(true);
    p.call("LudisseyDiscover");
    auto plain = w;
    w.set("icon", std::string(64, 'a') + ".png");
    w.set("thumbnail", std::string(64, 'b') + ".jpg");
    save();
    p.call("LudisseyWorlds");
    save(true);
    w.set("icon", "../segreto.png"); save();
    w.set("icon", std::string(64, 'c') + ".exe"); save();
    w.set("thumbnail", 5); save();
    p.call("LudisseyWorlds");
    w = plain;
    save();
    p.call("LudisseyExperience", Json::object({{"id", id}, {"world", true}}), "visitor");
    p.call("LudisseySaveWorld", Json::object({{"world", w}}), "visitor");
    auto valid = w;
    w.set("serverSize", 1.5); save(); w = valid;
    w.set("genre", "invalid"); save(); w = valid;
    w["blocks"].items()[0].set("rotationY", 181); save(); w = valid;
    w["blocks"].items()[0].set("properties", Json::object({{"Nonexistent", true}})); save(); w = valid;
    w["blocks"].items()[0].set("parent", "missing"); save(); w = valid;
    w.set("terrain", Json::object({{"chunks", Json::object({{"0,0,0", "AAAA"}})}})); save(); w = valid;
    p.call("LudisseyDeleteWorld", Json::object({{"id", id}}));
    p.call("LudisseyWorlds");
    p.call("LudisseyDiscover");
}

TEST_CASE("Server nativo: ticket, presenza e chat", "[native-server]") {
    ParityServer p;
    auto w = p.world();
    auto id = w["id"];
    p.call("LudisseyPublishWorld", Json::object({{"world", w}}));
    auto request = [&](const std::string& player) { return p.call("LudisseyRequestSession", Json::object({{"experienceId", id}, {"playerProtocol", rules::playerProtocol}}), player); };
    auto first = request("owner");
    auto second = request("friend");
    auto full = Json::object({{"experienceId", id}, {"serverId", first["serverId"]}, {"playerProtocol", rules::playerProtocol}});
    p.call("LudisseyRequestSession", full, "visitor");
    auto args = Json::object({{"experienceId", id}, {"sessionId", first["sessionId"]}, {"joinTicket", first["joinTicket"]}});
    p.call("LudisseyReadySession", args);
    p.call("LudisseyOpenSession", args, "visitor");
    p.call("LudisseyOpenSession", args);
    p.call("LudisseyOpenSession", args);
    auto ready = p.call("LudisseyReadySession", args);
    p.call("LudisseyReadySession", args);
    p.call("LudisseyHeartbeat", args);
    auto group = ready["presence"];
    auto presenceArgs = Json::object({{"SharedGroupId", group}, {"Data", Json::object({{first["sessionId"].string(), "state"}})}});
    p.call("UpdateSharedGroupData", presenceArgs, "owner", true);
    p.call("GetSharedGroupData", presenceArgs, "owner", true);
    p.call("GetSharedGroupData", presenceArgs, "visitor", true);
    args.set("message", " Ciao 🧱 ");
    p.call("LudisseyChat", args);
    p.call("LudisseyServerState", args);
    p.call("LudisseyExperience", Json::object({{"id", id}}));
    p.call("LudisseyLeaveSession", args);
    p.call("LudisseyLeaveSession", args);
    p.call("LudisseyServerState", args);
    p.advance(rules::reservationSeconds + rules::emptyServerSeconds + rules::playerTimeoutSeconds + 1);
    args.set("sessionId", second["sessionId"]); args.set("joinTicket", second["joinTicket"]);
    p.call("LudisseyOpenSession", args, "friend");
    request("friend");
}

TEST_CASE("Server nativo: autorizzazione delle risorse", "[native-server]") {
    ParityServer p;
    auto w = p.world();
    auto asset = sha256Hex("mesh") + ".obj";
    auto request = Json::object({{"assets", Json::array({asset})}});
    p.call("LudisseyAssetFinalize", request);
    p.call("LudisseyAssetUpload", request);
    p.call("LudisseyAssetFinalize", request);
    p.native.blobs.set("eowner/" + asset, Json::object({{"length", 42}}));
    p.sync();
    p.call("LudisseyAssetFinalize", request);
    p.call("LudisseyAssetUpload", request);
    w["blocks"].items()[0].set("kind", "mesh");
    w["blocks"].items()[0].set("meshAsset", asset);
    p.call("LudisseyPublishWorld", Json::object({{"world", w}}));
    request.set("experienceId", w["id"]);
    p.call("LudisseyAssetResolve", request, "visitor");
    request.set("assets", Json::array({sha256Hex("other") + ".obj"}));
    p.call("LudisseyAssetResolve", request, "visitor");
    auto sound = sha256Hex("sound") + ".wav", picture = sha256Hex("picture") + ".png";
    auto content = [](const std::string& id) { return Json::object({{"Content", std::string(rules::assets::prefix) + id}}); };
    w.set("instances", Json::array({Json::object({{"id", "5a1f0c2e-7d3b-4c1a-8e2f-0000000000c1"}, {"className", "Sound"}, {"name", "Musica"}, {"parent", "SoundService"}, {"properties", Json::object({{"SoundId", content(sound)}})}}),
                                    Json::object({{"id", "5a1f0c2e-7d3b-4c1a-8e2f-0000000000c2"}, {"className", "Sound"}, {"name", "Esterno"}, {"parent", "SoundService"}, {"properties", Json::object({{"SoundId", Json::object({{"Content", "https://esempio.test/a.mp3"}})}})}})}));
    CHECK(p.call("LudisseySaveWorld", Json::object({{"world", w}}))["id"] == w["id"]);
    CHECK(p.call("LudisseyPublishWorld", Json::object({{"world", w}})).isNull());
    auto extra = Json::object({{"assets", Json::array({sound})}});
    p.call("LudisseyAssetUpload", extra);
    p.native.blobs.set("eowner/" + sound, Json::object({{"length", 64}}));
    p.sync();
    p.call("LudisseyAssetFinalize", extra);
    auto published = p.call("LudisseyPublishWorld", Json::object({{"world", w}}));
    extra.set("experienceId", w["id"]);
    CHECK(published["id"] == w["id"]);
    auto urls = p.call("LudisseyAssetResolve", extra, "visitor");
    CHECK(urls["urls"][sound].isString());
    extra.set("assets", Json::array({picture}));
    CHECK(p.call("LudisseyAssetResolve", extra, "visitor").isNull());
    auto icon = sha256Hex("icona") + ".png", cover = sha256Hex("miniatura") + ".jpg";
    w.set("icon", icon);
    w.set("thumbnail", cover);
    CHECK(p.call("LudisseyPublishWorld", Json::object({{"world", w}})).isNull());
    auto images = Json::object({{"assets", Json::array({icon, cover})}});
    p.call("LudisseyAssetUpload", images);
    p.native.blobs.set("eowner/" + icon, Json::object({{"length", 80}}));
    p.native.blobs.set("eowner/" + cover, Json::object({{"length", 90}}));
    p.sync();
    p.call("LudisseyAssetFinalize", images);
    CHECK(p.call("LudisseyPublishWorld", Json::object({{"world", w}}))["icon"] == Json(icon));
    auto listed = p.call("LudisseyDiscover", Json::object(), "visitor");
    bool found = false;
    for (const auto& entry : listed["worlds"].items())
        if (entry["id"] == w["id"]) found = entry["icon"] == Json(icon) && entry["thumbnail"] == Json(cover);
    CHECK(found);
    images.set("experienceId", w["id"]);
    auto resolved = p.call("LudisseyAssetResolve", images, "visitor");
    CHECK(resolved["urls"][icon].isString());
    CHECK(resolved["urls"][cover].isString());
}

TEST_CASE("Server nativo: proprietà storiche", "[native-server]") {
    ParityServer p;
    const auto& registry = server_rules::registry();
    for (const auto& [className, entry] : registry["classes"].members()) {
        for (const auto& [name, type] : entry["properties"].members()) {
            auto category = type[0].string(), kind = type[1].string();
            Json value;
            if (category == "Primitive") value = kind == "bool" ? Json(true) : kind == "string" ? Json::object({{"String", "à🧱"}}) : Json(1.25);
            else if (kind == "ProtectedString") value = Json::object({{"String", "print(\"à🧱\")"}});
            else if (kind == "Content") value = Json::object({{"Content", "asset"}});
            else if (kind == "Color3" || kind == "BrickColor") value = Json::object({{"Color3", "AABBCC"}});
            else if (kind == "Vector3" || kind == "Vector2" || kind == "UDim" || kind == "UDim2") {
                Json list = Json::array();
                int count = kind == "Vector3" ? 3 : kind == "UDim2" ? 4 : 2;
                for (int i = 0; i < count; ++i) list.push(i + 0.25);
                value = Json::object({{kind, list}});
            } else if (category == "Enum") value = Json::object({{"Enum", registry["enums"][kind][0]}});
            else if (category == "Class") value = Json::object({{"Ref", nullptr}});
            for (const auto& candidate : {value, Json::array(), Json::object({{"wrong", true}})}) {
                INFO(className + "." + name + " " + candidate.dump());
                auto raw = Json::object({{name, candidate}});
                auto expected = Json::parse(p.oracle->evaluate("JSON.stringify((function(){try{return {value:properties(" + javaScriptLiteral(className) + "," + raw.dump() + ")};}catch(e){return {error:String(e)};}})())"));
                REQUIRE(expected);
                Json actual;
                try { actual = Json::object({{"value", server_rules::properties(className, raw)}}); }
                catch (const std::exception& e) { actual = Json::object({{"error", e.what()}}); }
                CHECK(actual == *expected);
            }
        }
    }
    for (const auto* text : {"", "ASCII", "città", "🧱", "a🧱é中"}) {
        CHECK(server_rules::digest(text) == p.oracle->evaluate("sha256(" + javaScriptLiteral(text) + ")"));
    }
}

TEST_CASE("Server nativo: numeri JS e digest Unicode", "[native-server]") {
    ParityServer p;
    Json values = Json::array({0.000001, 0.0000001, 0.0000123, -0.0000123, 1e15, 1e20, 1e21, 0.1, 1.2345678901234567});
    for (int exponent = -20; exponent <= 22; ++exponent) for (double factor : {1.0, 1.234, 9.87654321012345}) values.push(factor * std::pow(10.0, exponent));
    auto encoded = server_rules::encode(values);
    CHECK(encoded == p.oracle->evaluate("JSON.stringify(" + values.dump() + ")"));
    auto ordered = Json::object({{"10", 1}, {"z", 2}, {"2", 3}, {"01", 4}, {"4294967295", 5}, {"0", 6}});
    CHECK(server_rules::encode(ordered) == p.oracle->evaluate("JSON.stringify(" + ordered.dump() + ")"));
    auto w = p.world();
    w["blocks"].items()[0].set("x", 0.000001);
    w.set("instances", Json::array({Json::object({{"id", makeUuid()}, {"className", "NumberValue"}, {"name", "Valore"}, {"parent", "Workspace"}, {"properties", Json::object({{"Value", 0.000001}})}})}));
    p.call("LudisseySaveWorld", Json::object({{"world", w}}));
    p.call("LudisseySaveWorld", Json::object({{"world", w}}));
    p.call("LudisseyWorlds");
}

TEST_CASE("Server nativo: migrazione e script multichiave", "[native-server]") {
    ParityServer p;
    auto w = p.world();
    auto id = w["id"].string();
    w.set("schemaVersion", 1);
    w.set("owner", "owner"); w.set("author", "owner");
    auto partId = w["blocks"][0]["id"].string();
    Json code = Json::object({{"parts", Json::object({{partId, Json::object({{"scripts", Json::array({Json::object({{"id", makeUuid()}, {"source", "print('ciao')"}, {"name", "Legacy"}})})}})}})}});
    p.native.state["players"]["owner"]["readOnly"].set("World_" + id, w.dump());
    p.native.state["players"]["owner"]["readOnly"].set("LudisseyWorlds", Json::array({id}).dump());
    p.native.state["players"]["owner"]["user"].set("Studio_" + replaceAll(id, "-", ""), code.dump());
    p.sync();
    p.call("LudisseyWorlds");
    p.call("LudisseyExperience", Json::object({{"id", id}, {"world", true}}));
    w = p.world();
    Terrain t;
    t.set(0, 0, 0, Terrain::compose(TerrainMaterial::grass, 15, 0));
    t.set(20, 12, 30, Terrain::compose(TerrainMaterial::air, 0, 12));
    t.settings = TerrainSettings{12, 1.5, 2.25, -1};
    w.set("terrain", t.json());
    std::string source;
    while (source.size() < static_cast<std::size_t>(rules::instances::keyCharacters * 2)) source += "print(\"città 🧱\")\n";
    w.set("instances", Json::array({Json::object({{"id", makeUuid()}, {"className", "Script"}, {"name", "Script"}, {"parent", "ServerScriptService"}, {"properties", Json::object({{"Source", Json::object({{"String", source}})}, {"Disabled", false}})}})}));
    p.call("LudisseyPublishWorld", Json::object({{"world", w}}));
    p.call("LudisseyWorlds");
    w.set("instances", Json::array()); w.set("terrain", Json::object());
    p.call("LudisseyPublishWorld", Json::object({{"world", w}}));
    p.call("LudisseyWorlds");
}

TEST_CASE("Server nativo: dati giocatore per Storage", "[native-server][storage]") {
    ParityServer p;
    auto w = p.world();
    auto id = w["id"];
    p.call("LudisseyPublishWorld", Json::object({{"world", w}}));
    auto data = [&](const std::string& player, Json args) { return p.call("LudisseyExperienceData", args, player); };
    data("visitor", Json::object({{"experienceId", id}, {"action", "load"}}));
    data("visitor", Json::object({{"experienceId", id}, {"action", "save"}, {"value", Json::object({{"monete", 12}, {"nome", "Visitatore à 🧱"}, {"lista", Json::array({1, 2.5, true})}})}}));
    data("visitor", Json::object({{"experienceId", id}, {"action", "load"}}));
    data("friend", Json::object({{"experienceId", id}, {"action", "load"}}));
    data("visitor", Json::object({{"experienceId", id}, {"action", "erase"}}));
    data("visitor", Json::object({{"experienceId", id}, {"action", "save"}}));
    data("visitor", Json::object({{"experienceId", id}, {"action", "save"}, {"value", std::string(static_cast<std::size_t>(rules::scriptData::characters), 'x')}}));
    data("visitor", Json::object({{"experienceId", "non-un-id"}, {"action", "load"}}));
    data("visitor", Json::object({{"experienceId", makeUuid()}, {"action", "load"}}));
    data("visitor", Json::object({{"experienceId", id}, {"action", "save"}, {"value", Json()}}));
    data("visitor", Json::object({{"experienceId", id}, {"action", "load"}}));
}

TEST_CASE("Server nativo: versioni di Lim e API", "[native-server][versions]") {
    ParityServer p;
    auto w = p.world();
    auto save = [&](const Json& world) { return p.call("LudisseySaveWorld", Json::object({{"world", world}})); };
    auto saved = save(w);
    CHECK(saved["languageVersion"] == Json(std::string(rules::lim::languageVersion)));
    CHECK(saved["engineApiVersion"] == Json(rules::lim::engineApiVersion));
    auto legacy = w;
    legacy.erase("languageVersion");
    legacy.erase("engineApiVersion");
    save(legacy);
    auto newer = w;
    newer.set("engineApiVersion", rules::lim::engineApiVersion + 1);
    save(newer);
    newer = w;
    newer.set("languageVersion", "2.0");
    save(newer);
    newer = w;
    newer.set("engineApiVersion", 1.5);
    save(newer);
    p.call("LudisseyWorlds");
}

TEST_CASE("Server nativo: visibilità e limite chat", "[native-server][visibility][chat]") {
    ParityServer p;
    auto w = p.world();
    auto id = w["id"];
    p.call("LudisseySaveWorld", Json::object({{"world", w}}));
    p.call("LudisseyUnpublishWorld", Json::object({{"id", id}}));
    p.call("LudisseyExperience", Json::object({{"id", id}}), "visitor");
    p.call("LudisseyPublishWorld", Json::object({{"world", w}}));
    auto listed = p.call("LudisseyDiscover");
    CHECK(listed["worlds"].size() == 1);
    p.call("LudisseyUnpublishWorld", Json::object({{"id", id}}), "visitor");
    p.call("LudisseyUnpublishWorld", Json::object({{"id", 42}}));
    auto request = p.call("LudisseyRequestSession", Json::object({{"experienceId", id}, {"playerProtocol", rules::playerProtocol}}), "visitor");
    auto args = Json::object({{"experienceId", id}, {"sessionId", request["sessionId"]}, {"joinTicket", request["joinTicket"]}});
    p.call("LudisseyOpenSession", args, "visitor");
    p.call("LudisseyReadySession", args, "visitor");
    for (int i = 0; i < rules::chatBurst + 2; ++i) {
        args.set("message", "Messaggio " + std::to_string(i));
        p.call("LudisseyChat", args, "visitor");
        p.advance(0.5);
    }
    auto state = p.call("LudisseyServerState", args, "visitor");
    CHECK(static_cast<int>(state["chat"].size()) == rules::chatBurst);
    p.advance(rules::chatWindowSeconds);
    args.set("message", "Dopo la pausa");
    p.call("LudisseyChat", args, "visitor");
    auto hidden = p.call("LudisseyUnpublishWorld", Json::object({{"id", id}}));
    CHECK(hidden["published"] == Json(false));
    CHECK(p.call("LudisseyDiscover")["worlds"].size() == 0);
    p.call("LudisseyExperience", Json::object({{"id", id}}), "visitor");
    p.call("LudisseyRequestSession", Json::object({{"experienceId", id}, {"playerProtocol", rules::playerProtocol}}), "friend");
    auto own = p.call("LudisseyExperience", Json::object({{"id", id}}));
    CHECK(own["experience"]["published"] == Json(false));
    p.call("LudisseyPublishWorld", Json::object({{"world", w}}));
    CHECK(p.call("LudisseyDiscover")["worlds"].size() == 1);
}
