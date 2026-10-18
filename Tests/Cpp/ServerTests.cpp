#include "Engine/Assets/Mesh.hpp"
#include "Engine/Client/Store.hpp"
#include "Engine/Multiplayer/PlayerSession.hpp"
#include "Engine/Scripting/WorldSession.hpp"
#include "Engine/Studio/StudioDocument.hpp"
#include "Engine/Studio/StudioSync.hpp"
#include "ServerHarness.hpp"

#include <thread>

using namespace ld;
using harness::Fixture;
using harness::refused;

namespace {

std::vector<int> sequences(const std::vector<ExperienceServer>& servers) {
    std::vector<int> out;
    for (const auto& s : servers) out.push_back(s.sequence);
    return out;
}

std::vector<int> players(const std::vector<ExperienceServer>& servers) {
    std::vector<int> out;
    for (const auto& s : servers) out.push_back(s.players);
    return out;
}

double wall() { return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count(); }

JoinTicket ticketFrom(const Json& issued, const std::string& experience) {
    JoinTicket t;
    t.sessionId = SessionId(issued["sessionId"].string());
    t.joinTicket = issued["joinTicket"].string();
    t.expiresAt = wall() + 60;
    t.playerProtocol = 1;
    t.experienceId = ExperienceId(experience);
    t.nonce = issued["nonce"].string();
    t.serverId = ServerId(issued["serverId"].string());
    t.serverSequence = static_cast<int>(issued["serverSequence"].integer());
    return t;
}

std::shared_ptr<PlayerSession> session(Fixture& fixture, const std::string& player, const std::string& name, std::shared_ptr<PlayerGameplay> gameplay) {
    auto backend = std::make_shared<harness::ScriptBackend>(fixture.world, player);
    auto ticket = ticketFrom(fixture.request(player), fixture.experience);
    auto s = std::make_shared<PlayerSession>(ticket, backend, std::make_shared<AssetService>(backend, fixture.world->transport()));
    s->gameplay = gameplay;
    s->displayName = name;
    harness::awaitVoid(s->start());
    return s;
}

AvatarSnapshot snapshot(double x, double z, double yaw, bool moving, double time) {
    AvatarSnapshot s;
    s.x = x;
    s.z = z;
    s.yaw = yaw;
    s.moving = moving;
    s.time = time;
    return s;
}

std::optional<Vec3> waterSpot(const Terrain& terrain) {
    for (int i = 0; i < Terrain::cellsX; ++i)
        for (int j = 0; j < Terrain::cellsY; ++j)
            for (int k = 0; k < Terrain::cellsZ; ++k) {
                Vec3 c = Terrain::center(i, j, k);
                if (terrain.water(c.x, c.y, c.z) && terrain.water(c.x, c.y + 1, c.z) && terrain.water(c.x, c.y - 1, c.z)) return c;
            }
    return std::nullopt;
}

ExperienceEntry entry(const Fixture& fixture) { return ExperienceEntry{fixture.experience, "Arena di prova", "", "Costruttrice", "Avventura", 2, std::nullopt, std::nullopt, {ExperienceScope::all}, true, std::nullopt, std::nullopt, std::nullopt, std::string()}; }

}

TEST_CASE("Server: metadati dell'esperienza", "[server]") {
    golden::registry();
    Fixture fixture(12, "Corse");
    auto created = fixture.details();
    const auto facts = created.experience;
    CHECK(facts.visits == 0);
    CHECK(facts.owner == fixture.owner);
    CHECK(facts.author == "Costruttrice");
    CHECK(facts.serverSize == 12);
    CHECK(facts.genre == "Corse");
    CHECK(facts.createdAt);
    CHECK(facts.updatedAt);
    CHECK(facts.published);
    CHECK(created.servers.empty());
    fixture.world->advance(60);
    auto worlds = fixture.world->invoke(fixture.owner, "LudisseyWorlds")["worlds"];
    auto draft = *Experience::fromJson(worlds[0]);
    fixture.world->invoke(fixture.owner, "LudisseySaveWorld", Json::object({{"world", draft.json()}}));
    CHECK(fixture.details().experience.updatedAt == facts.updatedAt);
    auto p = fixture.player(1);
    auto joined = fixture.join(p);
    fixture.world->advance(30);
    fixture.leave(p, joined.first);
    auto afterPlay = fixture.details().experience;
    CHECK(afterPlay.updatedAt == facts.updatedAt);
    CHECK(afterPlay.createdAt == facts.createdAt);
    CHECK(afterPlay.visits == 1);
    fixture.world->advance(60);
    draft.title = "Arena rinnovata";
    fixture.world->invoke(fixture.owner, "LudisseySaveWorld", Json::object({{"world", draft.json()}}));
    CHECK(fixture.details().experience.title == "Arena di prova");
    fixture.world->invoke(fixture.owner, "LudisseyPublishWorld", Json::object({{"world", draft.json()}}));
    auto republished = fixture.details().experience;
    CHECK(republished.title == "Arena rinnovata");
    CHECK(republished.updatedAt != facts.updatedAt);
    CHECK(republished.createdAt == facts.createdAt);
    CHECK(republished.revision > facts.revision);
    Experience invalid = draft;
    invalid.genre = "Inesistente";
    CHECK_THROWS(fixture.world->invoke(fixture.owner, "LudisseyPublishWorld", Json::object({{"world", invalid.json()}})));
    invalid = draft;
    invalid.serverSize = rules::serverSize::maximum + 1;
    CHECK_THROWS(fixture.world->invoke(fixture.owner, "LudisseyPublishWorld", Json::object({{"world", invalid.json()}})));
}

TEST_CASE("Server: riempimento dei server fino alla capacità", "[server]") {
    golden::registry();
    Fixture fixture;
    auto first = fixture.join(fixture.player(1));
    CHECK(first.second == 1);
    CHECK(fixture.occupancy() == std::vector<int>{1});
    fixture.join(fixture.player(2));
    CHECK(fixture.occupancy() == std::vector<int>{2});
    for (int i = 3; i <= 19; ++i) fixture.join(fixture.player(i));
    CHECK(fixture.occupancy() == std::vector<int>{19});
    auto twentieth = fixture.join(fixture.player(20));
    CHECK(twentieth.second == 1);
    CHECK(fixture.occupancy() == std::vector<int>{20});
    auto next = fixture.join(fixture.player(21));
    CHECK(next.second == 2);
    CHECK(fixture.occupancy() == std::vector<int>{20, 1});
    for (int i = 22; i <= 40; ++i) fixture.join(fixture.player(i));
    CHECK(fixture.occupancy() == std::vector<int>{20, 20});
    auto third = fixture.join(fixture.player(41));
    CHECK(third.second == 3);
    CHECK(fixture.occupancy() == std::vector<int>{20, 20, 1});
    for (int i = 42; i <= 50; ++i) fixture.join(fixture.player(i));
    CHECK(fixture.occupancy() == std::vector<int>{20, 20, 10});
    for (const auto& slot : fixture.layout()) {
        CHECK(slot.players + slot.reserved <= slot.capacity);
        CHECK(slot.capacity == 20);
    }
    auto leaver = fixture.registry()["servers"][0]["players"][3];
    fixture.leave(leaver["player"].string(), leaver["session"].string());
    CHECK(fixture.occupancy() == std::vector<int>{19, 20, 10});
    auto back = fixture.join(fixture.player(51));
    CHECK(back.second == 1);
    CHECK(fixture.occupancy() == std::vector<int>{20, 20, 10});
    auto servers = fixture.details().servers;
    auto full = *std::find_if(servers.begin(), servers.end(), [](const ExperienceServer& s) { return s.sequence == 1; });
    CHECK(refused("ServerFull", [&] { fixture.request(fixture.player(52), full.id); }));
    auto partial = *std::find_if(servers.begin(), servers.end(), [](const ExperienceServer& s) { return s.sequence == 3; });
    CHECK(fixture.join(fixture.player(53), partial.id).second == 3);
    CHECK(refused("ServerClosed", [&] { fixture.request(fixture.player(54), std::string("99999999-9999-4999-8999-999999999999")); }));
}

TEST_CASE("Server: concorrenza e 100 join", "[server]") {
    golden::registry();
    Fixture fixture;
    for (int i = 1; i <= 19; ++i) fixture.join(fixture.player(i));
    auto a = fixture.player(100), b = fixture.player(101);
    std::string interleave = "emulator.interleave = function() { if (emulator.pending.length) { var next = emulator.pending.shift(); emulator.results.push(JSON.parse(emulatorInvoke(next.player, 'LudisseyRequestSession', JSON.stringify(next.args)))); } };";
    fixture.world->evaluate("emulator.pending = [{player:'" + b + "', args:{experienceId:'" + fixture.experience + "', playerProtocol:1}}]; emulator.results = []; " + interleave);
    auto ticketA = fixture.request(a);
    fixture.world->evaluate("emulator.interleave = null");
    CHECK(fixture.world->number("emulator.results[0].result.serverSequence") == 1);
    CHECK(ticketA["serverSequence"].integer() == 2);
    CHECK(fixture.occupancy() == std::vector<int>{20, 1});
    auto storm = [&](bool versioned) {
        Fixture f;
        std::vector<std::string> list;
        for (int i = 1; i <= 100; ++i) list.push_back(f.player(i));
        std::string queue;
        for (std::size_t i = 1; i < list.size(); ++i) queue += (i > 1 ? "," : "") + std::string("{player:'") + list[i] + "', args:{experienceId:'" + f.experience + "', playerProtocol:1}}";
        f.world->evaluate(std::string("emulator.versioned = ") + (versioned ? "true" : "false") + "; emulator.pending = [" + queue + "]; emulator.results = []; " + interleave);
        auto firstTicket = f.request(list[0]);
        f.world->evaluate("emulator.interleave = null");
        CHECK(f.world->number("emulator.results.filter(function(r){return r.error;}).length") == 0);
        if (versioned) {
            auto tickets = *Json::parse(f.world->evaluate("JSON.stringify(emulator.results.map(function(r){return r.result;}))"));
            std::vector<Json> all{firstTicket};
            for (const auto& t : tickets.items()) all.push_back(t);
            CHECK(all.size() == 100);
            for (const auto& t : all) {
                std::string owner = f.world->evaluate("(function(){var s=" + javaScriptLiteral(t["sessionId"].string()) + ";var r=emulator.entities['e" + f.owner + "'].objects[" + javaScriptLiteral(f.objectName()) +
                                                      "];for(var i=0;i<r.servers.length;i++)for(var j=0;j<r.servers[i].reservations.length;j++)if(r.servers[i].reservations[j].session===s)return r.servers[i].reservations[j].player;return '';})()");
                REQUIRE_FALSE(owner.empty());
                f.open(owner, t);
                f.ready(owner, t["sessionId"].string());
            }
            CHECK(f.details().experience.visits == 100);
        }
        return std::make_pair(f.occupancy(), f.world->number("emulator.conflicts"));
    };
    auto safe = storm(true);
    CHECK(safe.first == std::vector<int>{20, 20, 20, 20, 20});
    CHECK(safe.second > 0);
    auto unsafe = storm(false);
    CHECK(unsafe.first != std::vector<int>{20, 20, 20, 20, 20});
}

TEST_CASE("Server: prenotazioni e ready", "[server]") {
    golden::registry();
    Fixture fixture(2);
    auto a = fixture.player(1), b = fixture.player(2), c = fixture.player(3);
    auto ticket = fixture.request(a);
    fixture.request(b);
    CHECK(fixture.occupancy() == std::vector<int>{2});
    CHECK(fixture.request(c)["serverSequence"].integer() == 2);
    fixture.world->advance(rules::reservationSeconds + 1);
    CHECK(refused("SessionExpired", [&] { fixture.open(a, ticket); }));
    CHECK(fixture.join(fixture.player(4)).second == 1);
    auto d = fixture.player(5);
    auto once = fixture.request(d);
    CHECK(refused("SessionRejected", [&] { fixture.open(fixture.player(6), once); }));
    Json forged = once;
    forged.set("joinTicket", std::string(64, '0'));
    CHECK(refused("SessionRejected", [&] { fixture.open(d, forged); }));
    fixture.open(d, once);
    CHECK(refused("SessionUsed", [&] { fixture.open(d, once); }));
    fixture.ready(d, once["sessionId"].string());
    auto occupied = fixture.occupancy();
    auto again = fixture.ready(d, once["sessionId"].string());
    CHECK(again["repeated"].boolean());
    CHECK(fixture.occupancy() == occupied);
    CHECK(refused("SessionUsed", [&] { fixture.ready(fixture.player(6), once["sessionId"].string()); }));
    auto unopened = fixture.request(fixture.player(7));
    CHECK(refused("SessionRejected", [&] { fixture.ready(fixture.player(7), unopened["sessionId"].string()); }));
    CHECK(refused("VersionMismatch", [&] { fixture.world->invoke(fixture.player(8), "LudisseyRequestSession", Json::object({{"experienceId", fixture.experience}, {"playerProtocol", 99}})); }));
}

TEST_CASE("Server: visite contate solo al ready", "[server]") {
    golden::registry();
    Fixture fixture;
    auto a = fixture.player(1);
    auto ok = fixture.join(a);
    CHECK(fixture.details().experience.visits == 1);
    auto retried = fixture.ready(a, ok.first);
    CHECK(retried["repeated"].boolean());
    CHECK(retried["visits"].integer() == 1);
    CHECK(fixture.details().experience.visits == 1);
    auto b = fixture.player(2);
    auto failed = fixture.request(b);
    fixture.open(b, failed);
    fixture.leave(b, failed["sessionId"].string());
    CHECK(fixture.details().experience.visits == 1);
    CHECK(fixture.occupancy() == std::vector<int>{1});
    fixture.request(fixture.player(3));
    fixture.world->advance(rules::reservationSeconds + 1);
    fixture.details();
    CHECK(fixture.details().experience.visits == 1);
    fixture.world->invoke(a, "LudisseyExperience", Json::object({{"id", fixture.experience}}));
    CHECK(fixture.details().experience.visits == 1);
    auto again = fixture.request(a);
    int connected = 0;
    for (const auto& s : fixture.layout()) connected += s.players;
    CHECK(connected == 0);
    fixture.open(a, again);
    fixture.ready(a, again["sessionId"].string());
    CHECK(fixture.details().experience.visits == 2);
    auto d = fixture.player(4);
    auto stale = fixture.request(d);
    auto opened = fixture.open(d, stale);
    auto loaded = *Experience::fromJson(opened["world"]);
    fixture.world->invoke(fixture.owner, "LudisseyPublishWorld", Json::object({{"world", loaded.json()}}));
    std::string refusal;
    try {
        fixture.world->invoke(d, "LudisseyReadySession", Json::object({{"experienceId", fixture.experience}, {"sessionId", stale["sessionId"].string()}, {"revision", loaded.revision}}));
    } catch (const ServiceError& e) {
        refusal = e.what();
    }
    CHECK(refusal.find("WorldChanged") != std::string::npos);
    CHECK(failureFromCode("WorldChanged") == JoinFailure::worldLoadFailure);
    CHECK(fixture.details().experience.visits == 2);
}

TEST_CASE("Server: lista dalla fonte dell'allocatore", "[server]") {
    golden::registry();
    Fixture fixture(3);
    for (int i = 1; i <= 7; ++i) fixture.join(fixture.player(i));
    fixture.request(fixture.player(8));
    auto listed = fixture.details(fixture.player(9)).servers;
    auto registry = fixture.layout();
    std::vector<int> registrySequences, registryPlayers, capacities;
    for (const auto& s : registry) {
        registrySequences.push_back(s.sequence);
        registryPlayers.push_back(s.players);
    }
    for (const auto& s : listed) capacities.push_back(s.capacity);
    CHECK(sequences(listed) == registrySequences);
    CHECK(players(listed) == registryPlayers);
    CHECK(capacities == std::vector<int>{3, 3, 3});
    CHECK(players(listed) == std::vector<int>{3, 3, 1});
    CHECK(registry.back().reserved == 1);
}

TEST_CASE("Server: ciclo di vita e heartbeat", "[server]") {
    golden::registry();
    Fixture fixture(2);
    std::vector<std::pair<std::string, std::string>> sessions;
    for (int i = 1; i <= 5; ++i) {
        auto p = fixture.player(i);
        sessions.push_back({p, fixture.join(p).first});
    }
    CHECK(fixture.occupancy() == std::vector<int>{2, 2, 1});
    fixture.leave(sessions[0].first, sessions[0].second);
    fixture.leave(sessions[1].first, sessions[1].second);
    CHECK(fixture.occupancy() == std::vector<int>{0, 2, 1});
    CHECK(sequences(fixture.details().servers) == std::vector<int>{2, 3});
    fixture.world->advance(rules::emptyServerSeconds + 1);
    CHECK(sequences(fixture.details().servers) == std::vector<int>{2, 3});
    CHECK(fixture.join(fixture.player(6)).second == 3);
    CHECK(fixture.join(fixture.player(7)).second == 1);
    auto stale = fixture.player(8);
    auto staleSession = fixture.join(stale).first;
    fixture.world->advance(rules::playerTimeoutSeconds + 1);
    for (std::size_t i = 2; i < sessions.size(); ++i) {
        try {
            fixture.world->invoke(sessions[i].first, "LudisseyHeartbeat", Json::object({{"experienceId", fixture.experience}, {"sessionId", sessions[i].second}}));
        } catch (const std::exception&) {
        }
    }
    CHECK(refused("SessionEnded", [&] { fixture.world->invoke(stale, "LudisseyHeartbeat", Json::object({{"experienceId", fixture.experience}, {"sessionId", staleSession}})); }));
}

TEST_CASE("Server: avvio su richiesta", "[server]") {
    golden::registry();
    Fixture fixture(3);
    auto listed = [&] { return fixture.details().servers; };
    auto registered = [&] { return fixture.registry()["servers"].size(); };
    CHECK(listed().empty());
    CHECK(registered() == 0);
    auto p1 = fixture.player(1);
    auto t1 = fixture.request(p1);
    CHECK(listed().empty());
    CHECK(registered() == 1);
    auto opened = fixture.open(p1, t1);
    CHECK(opened["server"]["state"] == Json(std::string(rules::serverStates::ready)));
    CHECK(listed().empty());
    fixture.ready(p1, t1["sessionId"].string());
    REQUIRE(listed().size() == 1);
    CHECK(listed()[0].sequence == 1);
    CHECK(listed()[0].players == 1);
    CHECK(listed()[0].state == ServerLifecycle::joinable);
    auto [s2, q2] = fixture.join(fixture.player(2));
    CHECK(q2 == 1);
    CHECK(listed()[0].players == 2);
    auto [s3, q3] = fixture.join(fixture.player(3));
    CHECK(q3 == 1);
    CHECK(listed()[0].players == 3);
    CHECK(listed()[0].state == ServerLifecycle::full);
    auto p4 = fixture.player(4);
    auto [s4, q4] = fixture.join(p4);
    CHECK(q4 == 2);
    CHECK(sequences(listed()) == std::vector<int>{1, 2});
    fixture.leave(p1, t1["sessionId"].string());
    CHECK(sequences(listed()) == std::vector<int>{1, 2});
    CHECK(listed()[0].players == 2);
    CHECK(listed()[0].state == ServerLifecycle::joinable);
    fixture.leave(fixture.player(2), s2);
    fixture.leave(fixture.player(3), s3);
    CHECK(sequences(listed()) == std::vector<int>{2});
    fixture.leave(p4, s4);
    CHECK(listed().empty());
    CHECK(registered() > 0);
    fixture.world->advance(rules::emptyServerSeconds + 1);
    CHECK(listed().empty());
    auto a = fixture.player(5), b = fixture.player(6);
    auto ta = fixture.request(a), tb = fixture.request(b);
    CHECK(ta["serverId"].string() == tb["serverId"].string());
    CHECK(registered() == 1);
    fixture.open(a, ta);
    fixture.ready(a, ta["sessionId"].string());
    fixture.open(b, tb);
    fixture.ready(b, tb["sessionId"].string());
    REQUIRE(listed().size() == 1);
    CHECK(listed()[0].players == 2);
    auto c = fixture.player(7);
    auto cancelled = fixture.request(c);
    fixture.leave(c, cancelled["sessionId"].string());
    CHECK(listed()[0].players == 2);
    CHECK(fixture.occupancy() == std::vector<int>{2});
    fixture.world->advance(rules::playerTimeoutSeconds + 1);
    CHECK(listed().empty());
}

TEST_CASE("Server: chat e roster isolati per istanza", "[server]") {
    golden::registry();
    Fixture fixture(2);
    auto a = fixture.player(1), b = fixture.player(2), c = fixture.player(3);
    auto sa = fixture.join(a).first, sb = fixture.join(b).first, sc = fixture.join(c).first;
    fixture.world->invoke(a, "LudisseyChat", Json::object({{"experienceId", fixture.experience}, {"sessionId", sa}, {"message", "Ciao a tutti"}}));
    auto seenByB = fixture.world->invoke(b, "LudisseyServerState", Json::object({{"experienceId", fixture.experience}, {"sessionId", sb}, {"after", 0}}));
    auto seenByC = fixture.world->invoke(c, "LudisseyServerState", Json::object({{"experienceId", fixture.experience}, {"sessionId", sc}, {"after", 0}}));
    CHECK(seenByB["chat"][0]["text"].string() == "Ciao a tutti");
    CHECK(seenByC["chat"].size() == 0);
    CHECK(seenByB["roster"] == Json::array({"Giocatore1", "Giocatore2"}));
    CHECK(refused("SessionEnded", [&] { fixture.world->invoke(c, "LudisseyServerState", Json::object({{"experienceId", fixture.experience}, {"sessionId", sa}})); }));
}

TEST_CASE("Server: migrazione dallo schema 2", "[server]") {
    golden::registry();
    auto world = std::make_shared<harness::ScriptWorld>();
    world->registerPlayer("Pold", "Veterana");
    std::string id = "abcdefab-cdef-4abc-8def-abcdefabcdef";
    std::string legacy = "{\"id\":\"" + id +
                         "\",\"schemaVersion\":2,\"title\":\"Vecchia\",\"blocks\":[{\"id\":\"11111111-1111-4111-8111-111111111111\",\"kind\":\"cube\",\"color\":\"6633EE\",\"x\":0,\"y\":0.5,\"z\":0,\"size\":1}],\"owner\":\"Pold\",\"author\":\"Veterana\",\"updatedAt\":\"2026-01-01T00:00:00.000Z\"}";
    world->evaluate("emulator.players['Pold'].readOnly['LudisseyWorlds'] = JSON.stringify(['" + id + "']); emulator.players['Pold'].readOnly['World_" + id + "'] = " + javaScriptLiteral(legacy) +
                    "; emulator.groups['LudisseyPublished'] = {'" + id + "': JSON.stringify({id:'" + id + "',title:'Vecchia',owner:'Pold',author:'Veterana'})};");
    auto migrated = *Experience::fromJson(world->invoke("Pold", "LudisseyWorlds")["worlds"][0]);
    CHECK(migrated.schemaVersion == 4);
    CHECK(migrated.genre == std::string(rules::defaultGenre));
    CHECK(migrated.serverSize == rules::serverSize::standard);
    CHECK_FALSE(migrated.createdAt);
    REQUIRE(migrated.blocks.size() == 2);
    CHECK(migrated.blocks[0].name == std::string(rules::baseplate::name));
    auto details = *ExperienceDetails::fromJson(world->invoke("Pold", "LudisseyExperience", Json::object({{"id", id}})));
    CHECK(details.experience.visits == 0);
    CHECK_FALSE(details.experience.createdAt);
    auto decoded = *Experience::parse(legacy);
    CHECK(decoded.blocks[0].id == Experience::baseplate(id).id);
}

TEST_CASE("Server: accesso e bozze private", "[server]") {
    golden::registry();
    Fixture fixture;
    auto stranger = fixture.player(1);
    Experience hidden = Experience::blank();
    hidden.title = "Bozza privata";
    fixture.world->invoke(fixture.owner, "LudisseySaveWorld", Json::object({{"world", hidden.json()}}));
    CHECK(refused("ExperienceUnavailable", [&] { fixture.world->invoke(stranger, "LudisseyExperience", Json::object({{"id", hidden.id}})); }));
    CHECK(refused("ExperienceUnavailable", [&] { fixture.world->invoke(stranger, "LudisseyRequestSession", Json::object({{"experienceId", hidden.id}, {"playerProtocol", 1}})); }));
    CHECK_THROWS(fixture.world->invoke(stranger, "LudisseyPublishWorld", Json::object({{"world", Json::object({{"id", fixture.experience}, {"title", "Furto"}, {"blocks", Json::array()}, {"genre", "Altro"}, {"serverSize", 5}})}})));
    Experience cyclic = Experience::blank();
    WorldBlock loopA, loopB;
    loopA.kind = "model";
    loopB.kind = "model";
    loopA.parent = loopB.id;
    loopB.parent = loopA.id;
    cyclic.blocks.push_back(loopA);
    cyclic.blocks.push_back(loopB);
    CHECK_THROWS(fixture.world->invoke(fixture.owner, "LudisseySaveWorld", Json::object({{"world", cyclic.json()}})));
    Experience orphan = Experience::blank();
    WorldBlock child;
    child.parent = orphan.blocks[0].id;
    orphan.blocks.push_back(child);
    CHECK_THROWS(fixture.world->invoke(fixture.owner, "LudisseySaveWorld", Json::object({{"world", orphan.json()}})));
    ServiceError full("[ServerFull] Il server è pieno");
    CHECK(full.code() == std::optional<std::string>("ServerFull"));
    CHECK(full.reason() == "Il server è pieno");
    CHECK(failureFromCode("ServerBusy") == JoinFailure::serverBusy);
    CHECK(failureFromCode("SessionEnded") == JoinFailure::disconnected);
}

TEST_CASE("Server: limiti configurabili di server e registro", "[server]") {
    golden::registry();
    Fixture fixture(2);
    fixture.world->evaluate("RULES.maxServers = 3");
    for (int i = 1; i <= 6; ++i) fixture.join(fixture.player(i));
    CHECK(fixture.occupancy() == std::vector<int>{2, 2, 2});
    auto before = fixture.registry();
    CHECK(refused("ExperienceFull", [&] { fixture.request(fixture.player(7)); }));
    CHECK(fixture.registry() == before);
    fixture.world->evaluate("RULES.maxServers = 40");
    std::string sizeQuery = "JSON.stringify(emulator.entities['e" + fixture.owner + "'].objects['" + fixture.objectName() + "']).length";
    double size = fixture.world->number(sizeQuery);
    fixture.world->evaluate("RULES.registryCharacters = " + std::to_string(static_cast<int>(size) + 600));
    int admitted = 0;
    bool refusedJoin = false;
    for (int i = 8; i <= 20; ++i) {
        try {
            fixture.join(fixture.player(i));
            ++admitted;
        } catch (const ServiceError& e) {
            refusedJoin = e.code() == std::optional<std::string>("RegistryFull");
            break;
        }
    }
    CHECK(refusedJoin);
    CHECK(admitted >= 1);
    CHECK(admitted < 13);
    CHECK(fixture.world->number(sizeQuery) <= size + 600);
    Json after = fixture.registry();
    for (const auto& s : after["servers"].items()) CHECK(s["players"].size() + s["reservations"].size() <= 2);
    CHECK(failureFromCode("RegistryFull") == JoinFailure::serverBusy);
    CHECK(failureFromCode("ExperienceFull") == JoinFailure::serverBusy);
    Fixture exact(4);
    for (int i = 1; i <= 3; ++i) exact.join(exact.player(i));
    auto entrant = exact.player(4);
    std::string saved = exact.world->evaluate("JSON.stringify({entities:emulator.entities,serial:emulator.serial,groups:emulator.groups,members:emulator.members})");
    auto restore = [&] { exact.world->evaluate("(function(s) { emulator.entities = s.entities; emulator.serial = s.serial; emulator.groups = s.groups; emulator.members = s.members; })(" + saved + ")"); };
    std::string exactQuery = "JSON.stringify(emulator.entities['e" + exact.owner + "'].objects['" + exact.objectName() + "']).length";
    exact.request(entrant);
    int needed = static_cast<int>(exact.world->number(exactQuery));
    std::vector<std::string> outcomes;
    for (auto [label, limit] : std::vector<std::pair<std::string, int>>{{"limite−1", needed - 1}, {"limite", needed}, {"limite+1", needed + 1}}) {
        restore();
        exact.world->evaluate("RULES.registryCharacters = " + std::to_string(limit));
        auto snapshotRegistry = exact.registry();
        try {
            exact.request(entrant);
            outcomes.push_back(label + " ammesso");
            CHECK(limit >= needed);
            CHECK(static_cast<int>(exact.world->number(exactQuery)) == needed);
        } catch (const ServiceError& e) {
            outcomes.push_back(label + " " + e.code().value_or("?"));
            CHECK(limit < needed);
            CHECK(exact.registry() == snapshotRegistry);
        }
    }
    CHECK(outcomes == std::vector<std::string>{"limite−1 RegistryFull", "limite ammesso", "limite+1 ammesso"});
    auto first = fixture.registry()["servers"][0]["players"][0];
    fixture.leave(first["player"].string(), first["session"].string());
    CHECK_NOTHROW(fixture.join(fixture.player(30)));
}

TEST_CASE("Replica: presenza e interpolazione", "[server][replication]") {
    golden::registry();
    Fixture fixture(2);
    auto alpha = std::make_shared<PlayerGameplay>(), beta = std::make_shared<PlayerGameplay>(), gamma = std::make_shared<PlayerGameplay>();
    auto a = session(fixture, fixture.player(1), "Giocatore1", alpha);
    auto b = session(fixture, fixture.player(2), "Giocatore2", beta);
    auto c = session(fixture, fixture.player(3), "Giocatore3", gamma);
    REQUIRE(a->ready());
    REQUIRE(b->ready());
    REQUIRE(c->ready());
    CHECK(a->ticket.serverId == b->ticket.serverId);
    CHECK(c->ticket.serverId != a->ticket.serverId);
    alpha->setLocal(snapshot(0, 0, 0, false, 100));
    beta->setLocal(snapshot(4, 4, 1, false, 100));
    gamma->setLocal(snapshot(9, 9, 0, false, 100));
    harness::awaitVoid(a->exchange(1000));
    harness::awaitVoid(c->exchange(1000));
    harness::awaitVoid(b->exchange(1000));
    REQUIRE(b->remotes().count(a->ticket.sessionId));
    CHECK(b->remotes().at(a->ticket.sessionId).name == "Giocatore1");
    CHECK(b->remotes().size() == 1);
    CHECK(beta->remotes().size() == 1);
    alpha->setLocal(snapshot(10, 0, 0.5, true, 101));
    harness::awaitVoid(a->exchange(1001));
    harness::awaitVoid(b->exchange(1001));
    const RemoteAvatar moving = b->remotes().at(a->ticket.sessionId);
    double delay = rules::replication::delayMilliseconds / 1000.0;
    auto middle = moving.sample(900 + 100.5 + delay);
    REQUIRE(middle);
    CHECK(moving.snapshots.size() == 2);
    CHECK(std::fabs(middle->x - 5) < 1e-6);
    CHECK(std::fabs(middle->yaw - 0.25) < 1e-6);
    CHECK(moving.sample(900 + 120 + delay)->x == 10);
    CHECK(moving.sample(900 + 50)->x == 0);
    harness::awaitVoid(c->exchange(1001));
    CHECK(c->remotes().empty());
    RemoteAvatar wrapped;
    wrapped.session = SessionId("s");
    wrapped.name = "n";
    AvatarSnapshot w1, w2;
    w1.yaw = 3.0;
    w2.yaw = -3.0;
    w2.time = 1;
    wrapped.snapshots = {w1, w2};
    wrapped.offset = 0;
    CHECK(std::fabs(std::fabs(wrapped.sample(0.5 + delay)->yaw) - (3.0 + (2 * pi - 6) / 2)) < 1e-6);
    RemoteAvatar ordered;
    ordered.session = SessionId("s");
    ordered.name = "n";
    auto seq = [](double x, double time, int sequence) {
        AvatarSnapshot s;
        s.x = x;
        s.time = time;
        s.sequence = sequence;
        s.moving = sequence > 1;
        return s;
    };
    ordered.receive(seq(0, 10, 1), 20);
    ordered.receive(seq(3, 12, 3), 20.5);
    ordered.receive(seq(2, 11, 2), 20.6);
    ordered.receive(seq(9, 12, 3), 20.7);
    ordered.receive(seq(9, 13, 3), 20.8);
    REQUIRE(ordered.snapshots.size() == 2);
    CHECK(ordered.snapshots[0].sequence == 1);
    CHECK(ordered.snapshots[1].sequence == 3);
    CHECK(ordered.discarded == 3);
    CHECK(ordered.snapshots.back().x == 3);
    const auto& published = b->remotes().at(a->ticket.sessionId).snapshots;
    REQUIRE(published.size() == 2);
    CHECK(published[0].sequence == 1);
    CHECK(published[1].sequence == 2);
    double stale = rules::replication::staleSeconds;
    harness::awaitVoid(b->exchange(1001 + stale - 0.5));
    CHECK(b->remotes().count(a->ticket.sessionId));
    harness::awaitVoid(b->exchange(1001 + stale + 0.5));
    CHECK_FALSE(b->remotes().count(a->ticket.sessionId));
    harness::awaitVoid(b->exchange(1001 + stale + 0.8));
    CHECK_FALSE(b->remotes().count(a->ticket.sessionId));
    harness::awaitVoid(a->exchange(1001 + stale + 1));
    harness::awaitVoid(b->exchange(1001 + stale + 1));
    CHECK(b->remotes().count(a->ticket.sessionId));
    harness::awaitVoid(a->leave());
    harness::awaitVoid(b->exchange(1001 + stale + 2));
    CHECK_FALSE(b->remotes().count(a->ticket.sessionId));
    auto stranger = std::make_shared<harness::ScriptBackend>(fixture.world, fixture.player(3));
    CHECK_THROWS(harness::await(stranger->client("GetSharedGroupData", Json::object({{"SharedGroupId", "LudisseyPresence_" + b->ticket.serverId.text()}}))));
    harness::awaitVoid(b->leave());
    harness::awaitVoid(c->leave());
}

TEST_CASE("Replica: nessun clone di sé dopo un rientro", "[server][replication]") {
    golden::registry();
    Fixture fixture(4);
    auto me = fixture.player(1);
    auto old = std::make_shared<PlayerGameplay>();
    auto crashed = session(fixture, me, "Ludissey", old);
    old->setLocal(snapshot(0, 0, 0, false, 100));
    harness::awaitVoid(crashed->exchange(1000));
    auto fresh = std::make_shared<PlayerGameplay>();
    auto current = session(fixture, me, "Ludissey", fresh);
    CHECK(current->ticket.serverId == crashed->ticket.serverId);
    fresh->setLocal(snapshot(3, 0, 0, true, 101));
    harness::awaitVoid(current->exchange(1001));
    harness::awaitVoid(current->exchange(1001.5));
    CHECK(current->remotes().empty());
    CHECK(fresh->remotes().empty());
    auto otherPlay = std::make_shared<PlayerGameplay>();
    auto other = session(fixture, fixture.player(2), "Secondo", otherPlay);
    otherPlay->setLocal(snapshot(9, 0, 0, false, 102));
    harness::awaitVoid(other->exchange(1002));
    harness::awaitVoid(current->exchange(1002.5));
    REQUIRE(current->remotes().size() == 1);
    CHECK(current->remotes().begin()->second.name == "Secondo");
    harness::awaitVoid(other->exchange(1003));
    REQUIRE(other->remotes().size() == 1);
    CHECK(other->remotes().begin()->second.name == "Ludissey");
    SessionId departed = other->ticket.sessionId;
    harness::awaitVoid(other->leave());
    harness::awaitVoid(current->exchange(1004));
    CHECK(current->remotes().empty());
    auto returningPlay = std::make_shared<PlayerGameplay>();
    auto returning = session(fixture, fixture.player(2), "Secondo", returningPlay);
    CHECK_FALSE(returning->ticket.sessionId == departed);
    returningPlay->setLocal(snapshot(4, 0, 0, true, 103));
    harness::awaitVoid(returning->exchange(1005));
    harness::awaitVoid(current->exchange(1005.5));
    harness::awaitVoid(current->exchange(1006));
    REQUIRE(current->remotes().size() == 1);
    CHECK(current->remotes().begin()->first == returning->ticket.sessionId);
    CHECK(current->remotes().begin()->second.name == "Secondo");
    CHECK_FALSE(current->remotes().count(crashed->ticket.sessionId));
    CHECK_FALSE(current->remotes().count(current->ticket.sessionId));
    harness::awaitVoid(returning->leave());
    harness::awaitVoid(current->leave());
}

TEST_CASE("Replica: 20 Hz e buffer adattivo", "[server][replication]") {
    auto first = DirectTransport::open(SessionId("sessione-a"));
    auto second = DirectTransport::open(SessionId("sessione-b"));
    REQUIRE(first);
    REQUIRE(second);
    (*first)->connect(SessionId("sessione-b"), (*second)->port());
    (*second)->connect(SessionId("sessione-a"), (*first)->port());
    RemoteAvatar buffer;
    buffer.session = SessionId("sessione-a");
    buffer.name = "A";
    std::vector<double> latencies;
    double start = wallClock();
    for (int index = 1; index <= 40; ++index) {
        double sent = wallClock();
        ReplicationFrame frame;
        frame.player = "PA";
        frame.name = "A";
        frame.snapshot.x = index * 0.2;
        frame.snapshot.moving = true;
        frame.snapshot.time = sent - start;
        frame.snapshot.sequence = index;
        frame.endpoint = (*first)->port();
        REQUIRE((*first)->send(frame));
        bool arrived = false;
        for (int attempt = 0; attempt < 40 && !arrived; ++attempt) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            auto received = (*second)->receive();
            if (!received) continue;
            auto it = received->find(SessionId("sessione-a"));
            if (it == received->end() || it->second.snapshot.sequence != index) continue;
            auto landed = (*second)->arrival(SessionId("sessione-a"));
            if (!landed) continue;
            arrived = true;
            latencies.push_back(*landed - sent);
            buffer.receive(it->second.snapshot, wallClock() - start);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds((*first)->interval()));
    }
    auto ordered = latencies;
    std::sort(ordered.begin(), ordered.end());
    double p95 = ordered.empty() ? 1 : ordered[std::min(ordered.size() - 1, static_cast<std::size_t>(ordered.size() * 0.95))];
    CHECK(latencies.size() >= 38);
    CHECK(p95 < 0.02);
    CHECK(buffer.delay() < 0.2);
    CHECK(buffer.delay() >= RemoteAvatar::minimumDelay);
    double now = wallClock() - start;
    double previous = buffer.sample(now - 0.5) ? buffer.sample(now - 0.5)->x : 0;
    int backwards = 0;
    for (int step = 1; step <= 30; ++step) {
        auto s = buffer.sample(now - 0.5 + step / 60.0);
        double x = s ? s->x : previous;
        if (x < previous - 1e-9) ++backwards;
        previous = x;
    }
    CHECK(backwards == 0);
    RemoteAvatar shared;
    CHECK(shared.delay() == rules::replication::delayMilliseconds / 1000.0);
    ReplicationFrame packetFrame;
    packetFrame.player = "P";
    packetFrame.name = "N";
    packetFrame.snapshot = {1, 2, 3, 0, false, 1, 7};
    auto unpacked = replication_protocol::unpack(replication_protocol::packet(SessionId("x"), packetFrame));
    REQUIRE(unpacked);
    CHECK(unpacked->second.snapshot.sequence == 7);
    CHECK_FALSE(replication_protocol::decode("{\"name\":\"n\",\"version\":99,\"snapshot\":{\"x\":0,\"y\":0,\"z\":0,\"yaw\":0,\"moving\":false,\"time\":0}}"));
    (*first)->close();
    (*second)->close();
}

TEST_CASE("Flusso client: Gioca e server pieno", "[server][client]") {
    golden::registry();
    Fixture fixture(2);
    auto backend = fixture.world->registerPlayer("Pclient", "Cliente");
    auto store = std::make_shared<Store>(std::make_shared<PlayFab>(std::make_shared<MemoryStore>()), backend, nullptr, false);
    store->profile = PlayerProfile{"Cliente", "male", "2026-09-20T10:00:00Z"};
    auto& launch = LaunchRequest::shared();
    launch.joinTicket.reset();
    store->play(entry(fixture));
    REQUIRE(harness::waitUntil([&] { return launch.joinTicket.has_value() || store->join->failure().has_value(); }));
    CHECK_FALSE(store->join->failure());
    REQUIRE(launch.joinTicket);
    JoinTicket ticket = *launch.joinTicket;
    CHECK(ticket.serverSequence == 1);
    CHECK_FALSE(ticket.serverId.empty());
    auto assets = std::make_shared<AssetService>(backend, fixture.world->transport());
    auto player = std::make_shared<PlayerSession>(ticket, backend, assets);
    harness::awaitVoid(player->start());
    CHECK(player->ready());
    CHECK(player->phase() == JoinPhase::playing);
    CHECK(player->server()->id == ticket.serverId.text());
    CHECK(player->visits() == std::optional<int>(1));
    CHECK(player->roster() == std::vector<std::string>{"Cliente"});
    CHECK(player->world()->blocks.front().name == std::string(rules::baseplate::name));
    harness::awaitVoid(store->loadDetails(fixture.experience));
    CHECK(store->details[fixture.experience].experience.visits == 1);
    CHECK(players(store->details[fixture.experience].servers) == std::vector<int>{1});
    auto replay = std::make_shared<PlayerSession>(ticket, backend, assets);
    harness::awaitVoid(replay->start());
    CHECK(replay->failure() == std::optional<JoinFailure>(JoinFailure::joinRejected));
    CHECK_FALSE(replay->ready());
    harness::awaitVoid(player->send("Ciao dal Player"));
    REQUIRE_FALSE(player->chat().empty());
    CHECK(player->chat().back().text == "Ciao dal Player");
    harness::awaitVoid(player->leave());
    harness::awaitVoid(store->loadDetails(fixture.experience));
    CHECK(store->details[fixture.experience].servers.empty());
    CHECK(store->details[fixture.experience].experience.visits == 1);
    fixture.join(fixture.player(1));
    fixture.join(fixture.player(2));
    auto servers = fixture.details().servers;
    auto fullServer = *std::find_if(servers.begin(), servers.end(), [](const ExperienceServer& s) { return s.sequence == 1; });
    CHECK(fullServer.full());
    launch.joinTicket.reset();
    store->play(entry(fixture), ServerId(fullServer.id));
    REQUIRE(harness::waitUntil([&] { return store->join->failure().has_value() || launch.joinTicket.has_value(); }));
    CHECK(store->join->failure() == std::optional<JoinFailure>(JoinFailure::serverFull));
    CHECK_FALSE(launch.joinTicket);
    store->join->retry();
    REQUIRE(harness::waitUntil([&] { return launch.joinTicket.has_value() || (store->join->failure() && store->join->failure() != JoinFailure::serverFull); }));
    REQUIRE(launch.joinTicket);
    CHECK(launch.joinTicket->serverSequence == 2);
    harness::awaitVoid(BackendJoinSessionProvider(backend).release(*launch.joinTicket));
    launch.joinTicket.reset();
    int total = 0;
    for (int n : fixture.occupancy()) total += n;
    CHECK(total == 2);
    store->play(entry(fixture));
    store->join->cancel();
    harness::pumpFor(0.1);
    CHECK(store->join->phase() == JoinPhase::ready);
    CHECK_FALSE(store->join->failure());
    launch.joinTicket.reset();
}

TEST_CASE("Flusso Studio: salva, riapri e pubblica", "[server][studio]") {
    golden::registry();
    auto world = std::make_shared<harness::ScriptWorld>();
    auto backend = world->registerPlayer("Pstudio", "Architetta");
    StudioDocument document;
    document.open(Experience::blank());
    CHECK(document.world.blocks.front().name == std::string(rules::baseplate::name));
    CHECK_FALSE(document.world.blocks.front().locked);
    document.configure("Isola dei venti", std::nullopt, std::nullopt);
    document.configure(std::nullopt, "Piattaforme", std::nullopt);
    document.configure(std::nullopt, std::nullopt, 8);
    document.configure(std::nullopt, std::nullopt, 999);
    CHECK(document.world.genre == "Piattaforme");
    CHECK(document.world.serverSize == rules::serverSize::maximum);
    document.undo();
    CHECK((document.world.serverSize == 8 || document.world.serverSize == rules::serverSize::standard));
    document.configure(std::nullopt, std::nullopt, 8);
    document.addPart("cube");
    document.setSelection(StudioSelection::ofService("Lighting"));
    document.deleteSelection();
    CHECK(document.selection() == std::optional<StudioSelection>(StudioSelection::ofService("Lighting")));
    CHECK(document.world.blocks.size() == 2);
    document.setLighting("ClockTime", [](WorldLighting& l) { l.clockTime = 19.5; });
    document.setLighting("SkyTop", [](WorldLighting& l) { l.skyTop = "2B2F4A"; });
    document.undo();
    CHECK(document.world.lighting.skyTop == std::string(rules::lighting::skyTop));
    CHECK(document.world.lighting.clockTime == 19.5);
    document.redo();
    document.setLighting("Brightness", [](WorldLighting& l) { l.brightness = 99; });
    CHECK(document.world.lighting.brightness == rules::lighting::brightness::maximum);
    document.perform(StudioEdit::generate(TerrainSettings{11, 3, -2, 1}));
    CHECK((document.world.terrain.water(0, 0.5, 0) || !document.world.terrain.keys().empty()));
    std::string scripted = document.world.blocks[1].id;
    document.addScript(scripted);
    std::string scriptId = document.world.instances.back().id;
    document.setSource(scriptId, "let part = script.parent\nif part is BasePart {\n    part.color = Color(224 / 255, 57 / 255, 43 / 255)\n    print(\"pronto nel Player\")\n    await Task.wait(0.1)\n    part.position = part.position + Vec3(0, 3, 0)\n}\n");
    CHECK_FALSE(document.scriptErrors().count(scriptId));
    auto module = document.insert("ModuleScript", "ReplicatedStorage");
    REQUIRE(module);
    document.setSource(*module, "let saluto = \"modulo condiviso\"\n");
    auto starter = document.insert("LocalScript", "StarterPlayer.StarterPlayerScripts");
    REQUIRE(starter);
    document.setSource(*starter, "import ModuleScript\nprint(ModuleScript.saluto, Players.localPlayer?.name ?? \"\")\n");
    document.setSelection(StudioSelection::instance(scripted));
    document.run(StudioCommand::group);
    std::string groupedModel = document.selectedPart()->id;
    StudioSync sync(backend, world->transport());
    Experience snapshotWorld = document.world;
    auto saved = harness::await(sync.save(*document.publishable(), false));
    document.adopt(saved.draft, snapshotWorld);
    CHECK_FALSE(document.dirty());
    CHECK(document.world.author == "Architetta");
    CHECK(document.world.createdAt);
    CHECK(document.world.revision == 1);
    auto reopened = *Experience::fromJson(world->invoke("Pstudio", "LudisseyWorlds")["worlds"][0]);
    CHECK_FALSE(StudioSync::semanticDifference(document.world, reopened));
    StudioDocument reopenedDocument;
    reopenedDocument.open(reopened);
    CHECK(reopenedDocument.world.genre == "Piattaforme");
    CHECK(reopenedDocument.world.serverSize == 8);
    CHECK(reopenedDocument.world.blocks.size() == 3);
    CHECK(reopenedDocument.world.childIds(groupedModel).size() == 1);
    CHECK(reopenedDocument.world.instances == document.world.instances);
    CHECK(reopenedDocument.world.terrain == document.world.terrain);
    CHECK_FALSE(reopenedDocument.world.terrain.empty());
    CHECK(reopenedDocument.world.lighting == document.world.lighting);
    Experience publishSnapshot = reopenedDocument.world;
    auto published = harness::await(sync.save(*reopenedDocument.publishable(), true));
    reopenedDocument.adopt(published.draft, publishSnapshot);
    REQUIRE(published.details);
    CHECK(published.details->experience.published);
    CHECK(published.details->experience.author == "Architetta");
    auto store = std::make_shared<Store>(std::make_shared<PlayFab>(std::make_shared<MemoryStore>()), backend, nullptr, false);
    harness::awaitVoid(store->refreshWorlds());
    std::vector<WorldSummary> summaries;
    Json discovered = world->invoke("Pstudio", "LudisseyDiscover");
    for (const auto& item : discovered["worlds"].items()) summaries.push_back(WorldSummary::fromJson(item));
    store->setDiscover(summaries);
    auto listed = std::find_if(store->experienceCatalog().begin(), store->experienceCatalog().end(), [&](const ExperienceEntry& e) { return e.id == reopened.id; });
    REQUIRE(listed != store->experienceCatalog().end());
    CHECK(listed->genre == "Piattaforme");
    CHECK(listed->serverSize == 8);
    CHECK(listed->published);
    CHECK(listed->author == "Architetta");
    harness::awaitVoid(store->loadDetails(reopened.id));
    const auto& facts = store->details[reopened.id].experience;
    CHECK(facts.visits == 0);
    CHECK(facts.createdAt);
    CHECK(facts.updatedAt);
    auto visitor = world->registerPlayer("Pnuoto", "Nuotatrice");
    auto issued = world->invoke("Pnuoto", "LudisseyRequestSession", Json::object({{"experienceId", reopened.id}, {"playerProtocol", 1}}));
    auto visit = std::make_shared<PlayerSession>(ticketFrom(issued, reopened.id), visitor, std::make_shared<AssetService>(visitor, world->transport()));
    harness::awaitVoid(visit->start());
    REQUIRE(visit->world());
    Experience played = *visit->world();
    auto pool = waterSpot(played.terrain);
    REQUIRE(pool);
    CHECK(played.terrain == reopenedDocument.world.terrain);
    CHECK(played.lighting == reopenedDocument.world.lighting);
    auto parts = played.workspaceParts();
    CHECK(std::any_of(parts.begin(), parts.end(), [&](const WorldBlock& b) { return b.parent == groupedModel; }));
    CHECK_FALSE(std::any_of(parts.begin(), parts.end(), [&](const WorldBlock& b) { return b.id == groupedModel; }));
    CHECK(played.instances.size() == 3);
    std::vector<std::string> lines;
    WorldSession runtime(played, WorldSession::Mode::play, "Nuotatrice", 0, false, [&](const OutputLine& l) { lines.push_back(l.text); });
    runtime.physics = false;
    runtime.start({0, 1, 0});
    auto partY = [&] {
        for (const auto& b : runtime.parts())
            if (b.id == scripted) return b.y;
        return 0.0;
    };
    double startY = partY();
    for (int i = 0; i < 20; ++i) runtime.advance(1.0 / 60);
    auto colored = [&] {
        for (const auto& b : runtime.parts())
            if (b.id == scripted) return b.color;
        return std::string();
    };
    std::string all;
    for (const auto& l : lines) all += l + " | ";
    INFO(all);
    CHECK(colored() == "E0392B");
    CHECK(std::any_of(lines.begin(), lines.end(), [](const std::string& l) { return l.find("pronto nel Player") != std::string::npos; }));
    CHECK(std::find(lines.begin(), lines.end(), "modulo condiviso Nuotatrice") != lines.end());
    for (int i = 0; i < 30; ++i) runtime.advance(1.0 / 60);
    CHECK(partY() > startY + 2.9);
    runtime.stop();
    CharacterState swimmer{pool->x, pool->y - 0.8, pool->z};
    for (int i = 0; i < 30; ++i) swimmer = motion::step(swimmer, CharacterInput{}, 0, played.workspaceParts(), played.terrain, 1.0 / 60);
    CHECK(swimmer.swimming);
    harness::awaitVoid(visit->leave());
    Experience dark = *reopenedDocument.publishable();
    dark.lighting.clockTime = 400;
    dark.lighting.skyTop = "notacolor";
    auto clamped = *Experience::fromJson(world->invoke("Pstudio", "LudisseySaveWorld", Json::object({{"world", dark.json()}})));
    CHECK(clamped.lighting.clockTime == rules::lighting::clockTime::maximum);
    CHECK(clamped.lighting.skyTop == std::string(rules::lighting::skyTop));
    Experience tampered = *reopenedDocument.publishable();
    tampered.blocks[1].x = 999;
    CHECK_THROWS(harness::await(sync.save(tampered, false)));
    Experience misplaced = *reopenedDocument.publishable();
    for (auto& r : misplaced.instances)
        if (r.className == "LocalScript") r.parent = "Lighting";
    CHECK_THROWS(world->invoke("Pstudio", "LudisseySaveWorld", Json::object({{"world", misplaced.json()}})));
    Experience invented = *reopenedDocument.publishable();
    invented.instances.push_back(InstanceRecord{makeUuid(), "ProximityPrompt", "Futuro", "Workspace", {}});
    CHECK_THROWS(world->invoke("Pstudio", "LudisseySaveWorld", Json::object({{"world", invented.json()}})));
}

TEST_CASE("Risorse: import, upload e manomissione", "[server][assets]") {
    golden::registry();
    auto root = std::filesystem::temp_directory_path() / ("Ludissey Risorse Verifica " + makeUuid());
    auto author = root / "Autore", playerFolder = root / "Giocatore";
    std::filesystem::create_directories(root);
    auto obj = root / "Piramide.obj";
    asset_cache::writeFile(obj, "v 0 0 0\nv 1 0 0\nv 0 1 0\nv 0 0 1\nf 1 2 3\nf 1 2 4\nf 1 3 4\nf 2 3 4\n");
    asset_cache::setFolder(author);
    auto world = std::make_shared<harness::ScriptWorld>();
    auto backend = world->registerPlayer("Passet", "Modellista");
    StudioDocument document;
    document.open(Experience::blank());
    REQUIRE(document.importMesh(obj));
    const WorldBlock mesh = document.world.blocks.back();
    REQUIRE(mesh.meshAsset);
    std::string asset = *mesh.meshAsset;
    CHECK(asset_cache::valid(asset));
    CHECK(endsWith(asset, ".obj"));
    CHECK(document.world.json().dump().find(root.string()) == std::string::npos);
    StudioSync sync(backend, world->transport());
    harness::await(sync.save(*document.publishable(), true));
    CHECK(world->blob("ePasset/" + asset));
    asset_cache::setFolder(playerFolder);
    auto visitor = world->registerPlayer("Pvisit", "Visitatore");
    auto issued = world->invoke("Pvisit", "LudisseyRequestSession", Json::object({{"experienceId", document.world.id}, {"playerProtocol", 1}}));
    auto player = std::make_shared<PlayerSession>(ticketFrom(issued, document.world.id), visitor, std::make_shared<AssetService>(visitor, world->transport()));
    player->meshReadable = [](const std::filesystem::path& path) { return mesh_import::cached(path) != nullptr; };
    harness::awaitVoid(player->start());
    CHECK(player->ready());
    auto verified = asset_cache::verified(asset);
    REQUIRE(verified);
    CHECK(verified->string().rfind(playerFolder.string(), 0) == 0);
    harness::awaitVoid(player->leave());
    world->storeBlob("ePasset/" + asset, "manomesso");
    std::filesystem::remove_all(playerFolder);
    auto second = world->invoke("Pvisit", "LudisseyRequestSession", Json::object({{"experienceId", document.world.id}, {"playerProtocol", 1}}));
    asset_cache::setFolder(root / "Pulita");
    auto broken = std::make_shared<PlayerSession>(ticketFrom(second, document.world.id), visitor, std::make_shared<AssetService>(visitor, world->transport()));
    harness::awaitVoid(broken->start());
    CHECK(broken->failure() == std::optional<JoinFailure>(JoinFailure::assetLoadFailure));
    CHECK_FALSE(broken->ready());
    CHECK(ExperienceDetails::fromJson(world->invoke("Pvisit", "LudisseyExperience", Json::object({{"id", document.world.id}})))->experience.visits == 1);
    asset_cache::setFolder(std::nullopt);
    std::filesystem::remove_all(root);
}
