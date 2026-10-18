#include "Engine/Multiplayer/Replication.hpp"
#include "Engine/Networking/Join.hpp"
#include "Golden.hpp"

#include <algorithm>
#include <cmath>

using namespace ld;

namespace {

Json names(std::vector<std::string> values) {
    std::sort(values.begin(), values.end());
    Json out = Json::array();
    for (const auto& v : values) out.push(v);
    return out;
}

}

TEST_CASE("Parità: replicazione", "[parity][replication]") {
    Json g = golden::load("replication");
    ReplicationFrame frame;
    frame.player = "P1";
    frame.name = "Ada";
    frame.snapshot = {1, 2.5, -3, 0.25, true, 1000.5, 7};
    frame.avatar = AvatarConfiguration::standard("Ada", "female");
    frame.endpoint = 5555;
    CHECK(golden::same(frame.json(), g["frame"]) == "");
    auto encoded = Json::parse(replication_protocol::encode(frame));
    REQUIRE(encoded.ok());
    CHECK(golden::same(*encoded, *Json::parse(g["encoded"].string())) == "");
    std::string packet = replication_protocol::packet(SessionId("S1"), frame);
    CHECK(packet.substr(0, 3) == "S1\n");
    auto unpacked = replication_protocol::unpack(g["packet"].string());
    REQUIRE(unpacked);
    CHECK(unpacked->first == SessionId("S1"));
    CHECK(golden::same(unpacked->second.json(), g["decodedBack"]) == "");
    CHECK(unpacked->second == frame);
    CHECK(!replication_protocol::decode("{}") == g["invalid"][0].boolean());
    CHECK(!replication_protocol::decode(R"({"name":"a","snapshot":{"x":1,"y":1,"z":1,"yaw":0,"moving":false,"time":1},"version":2})") == g["invalid"][1].boolean());
    RemoteAvatar remote;
    remote.session = SessionId("S");
    remote.name = "N";
    double t = 100;
    Json samples = Json::array();
    for (int index = 0; index < 40; ++index) {
        t += index % 6 == 0 ? 0.11 : 0.05;
        AvatarSnapshot snapshot{index * 0.3, std::sin(index * 0.2), 0, index * 0.4 - 3, index % 3 != 0, t, index == 20 ? 19 : index};
        remote.receive(snapshot, t + 0.02 + (index % 4) * 0.01);
        double local = t + 0.03;
        auto s = remote.sample(local);
        samples.push(Json::array({local, remote.delay(), remote.discarded, s ? Json::array({s->x, s->y, s->z, s->yaw, s->moving ? 1 : 0, s->time}) : Json()}));
    }
    CHECK(golden::same(samples, g["samples"]) == "");
}

TEST_CASE("Parità: join, ticket e link", "[parity][join]") {
    Json g = golden::load("join");
    Json successors = Json::object();
    for (auto phase : allPhases()) {
        std::vector<std::string> list;
        for (auto next : phaseSuccessors(phase)) list.emplace_back(phaseName(next));
        successors.set(std::string(phaseName(phase)), names(list));
    }
    CHECK(golden::same(successors, g["successors"]) == "");
    Json messages = Json::array();
    for (auto phase : allPhases()) messages.push(Json::array({phaseName(phase), phaseMessage(phase)}));
    CHECK(golden::same(messages, g["messages"]) == "");
    JoinTicket ticket{SessionId("s"), "t", 2000000000, 1, ExperienceId("5a1f0c2e-7d3b-4c1a-8e2f-000000000001"), "n", ServerId("srv"), 3};
    CHECK(ticket.launchUrl() == g["url"].string());
    auto parsed = JoinTicket::parse(g["url"].string());
    REQUIRE(parsed);
    CHECK(*parsed == ticket);
    CHECK(parsed->expiresAt - 978307200 == g["parsed"]["expiresAt"].number());
    for (const auto& link : g["links"].items()) {
        auto l = ExperienceLink::parse(link[0].string());
        CHECK(golden::same(l ? Json::array({l->id.text(), l->play, l->valid()}) : Json(), link[1]) == "");
    }
    Json failures = Json::array();
    for (auto f : allFailures()) failures.push(Json::array({failureName(f), failureMessage(f), failureRetryable(f)}));
    CHECK(golden::same(failures, g["failures"]) == "");
    for (const auto& code : g["codes"].items()) CHECK(failureName(failureFromCode(*serviceCode("[" + code[0].string() + "] x"))) == code[1].string());
    auto studio = studioProjectFromUrl("ludissey-studio://open?project=5a1f0c2e-7d3b-4c1a-8e2f-000000000001");
    CHECK(golden::same(studio ? Json(*studio) : Json(), g["studio"][0]) == "");
    CHECK_FALSE(studioProjectFromUrl("ludissey-studio://open?project=../x"));
}
