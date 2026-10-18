#include "Engine/Client/JoinCoordinator.hpp"
#include "Engine/Core/Text.hpp"
#include "Engine/Multiplayer/Replication.hpp"
#include "Engine/Networking/Join.hpp"

#include <Catch2/catch_amalgamated.hpp>

#include <cmath>
#include <random>
#include <string>
#include <vector>

using namespace ld;

namespace {

std::string mutate(std::string text, std::mt19937& random) {
    static const std::string alphabet = "?&=/%:#@.+-_ \"\\{}[],0123456789abcdefABCDEF\x01\x7f\xc3\xa8\xff";
    int edits = 1 + static_cast<int>(random() % 5);
    for (int e = 0; e < edits; ++e) {
        std::size_t at = text.empty() ? 0 : random() % (text.size() + 1);
        switch (random() % 4) {
        case 0: text.insert(at, 1, alphabet[random() % alphabet.size()]); break;
        case 1:
            if (at < text.size()) text.erase(at, 1 + random() % 6);
            break;
        case 2:
            if (at < text.size()) text[at] = alphabet[random() % alphabet.size()];
            break;
        default: text = text.substr(0, at); break;
        }
    }
    return text;
}

}

TEST_CASE("Fuzz: link, progetti e pacchetti mutati", "[security][fuzz][join]") {
    JoinTicket ticket;
    ticket.sessionId = SessionId("22222222-3333-4444-8555-666666666666");
    ticket.joinTicket = "ticket-1";
    ticket.expiresAt = 1790000000;
    ticket.playerProtocol = rules::playerProtocol;
    ticket.experienceId = ExperienceId("11111111-2222-4333-8444-555555555555");
    ticket.nonce = "nonce-1";
    ticket.serverId = ServerId("33333333-4444-4555-8666-777777777777");
    ticket.serverSequence = 2;
    const std::vector<std::string> seeds{ticket.launchUrl(), ExperienceLink{ExperienceId("11111111-2222-4333-8444-555555555555"), true, ServerId("33333333-4444-4555-8666-777777777777")}.url(), "ludissey-studio://open?project=11111111-2222-4333-8444-555555555555"};
    ReplicationFrame frame;
    frame.name = "Giocatore";
    frame.snapshot = {1, 2, 3, 0.5, true, 4, 7};
    std::string payload = replication_protocol::encode(frame);
    std::mt19937 random(20261008);
    int accepted = 0, rejected = 0;
    for (int i = 0; i < 6000; ++i) {
        const std::string& seed = seeds[static_cast<std::size_t>(i) % seeds.size()];
        std::string input = mutate(seed, random);
        if (auto parsed = JoinTicket::parse(input)) {
            ++accepted;
            CHECK_FALSE(parsed->sessionId.text().empty());
            CHECK(std::isfinite(parsed->expiresAt));
        } else ++rejected;
        if (auto link = ExperienceLink::parse(input)) {
            if (link->valid()) {
                CHECK(isUuid(link->id.text()));
                if (link->server) CHECK(isUuid(link->server->text()));
            }
        }
        if (auto project = studioProjectFromUrl(input)) CHECK(isUuid(*project));
        LaunchRequest probe;
        probe.accept(input);
        std::string mutated = mutate(payload, random);
        if (auto decoded = replication_protocol::decode(mutated)) {
            const auto& s = decoded->snapshot;
            CHECK(std::isfinite(s.x + s.y + s.z + s.yaw + s.time));
            CHECK(decoded->name.size() < 4096);
        }
    }
    INFO("ticket accettati " << accepted << ", rifiutati " << rejected);
    CHECK(rejected > accepted);
    CHECK(JoinTicket::parse(seeds[0]));
}
