#pragma once

#include "Engine/Core/Ids.hpp"
#include "Engine/Core/Result.hpp"

#include <optional>
#include <set>
#include <string>
#include <vector>

namespace ld {

enum class JoinPhase { ready, resolvingExperience, requestingSession, launchingPlayer, connecting, loadingWorld, loadingAssets, enteringExperience, playing };

std::string_view phaseName(JoinPhase phase);
std::string phaseMessage(JoinPhase phase);
bool phaseActive(JoinPhase phase);
std::set<JoinPhase> phaseSuccessors(JoinPhase phase);
std::vector<JoinPhase> allPhases();

class JoinStateMachine {
public:
    explicit JoinStateMachine(JoinPhase start = JoinPhase::ready) : current(start), trail{start} {}
    JoinPhase phase() const { return current; }
    const std::vector<JoinPhase>& history() const { return trail; }
    bool move(JoinPhase next);

private:
    JoinPhase current;
    std::vector<JoinPhase> trail;
};

enum class JoinFailure { none, experienceUnavailable, sessionFailure, joinRejected, joinExpired, playerLaunchFailure, connectionTimeout, worldLoadFailure, assetLoadFailure, versionMismatch, networkFailure, serverFull, serverClosed, serverBusy, disconnected };

std::string_view failureName(JoinFailure failure);
std::string failureTitle(JoinFailure failure);
std::string failureMessage(JoinFailure failure);
bool failureRetryable(JoinFailure failure);
enum class FailureAction { none, play, retry };
FailureAction failureAction(JoinFailure failure);
JoinFailure failureFromCode(std::string_view code);
std::vector<JoinFailure> allFailures();
std::optional<std::string> serviceCode(std::string_view message);

struct JoinTicket {
    SessionId sessionId;
    std::string joinTicket;
    double expiresAt = 0;
    int playerProtocol = 0;
    ExperienceId experienceId;
    std::string nonce;
    ServerId serverId;
    int serverSequence = 0;

    bool operator==(const JoinTicket&) const = default;
    bool expired(double now) const { return expiresAt <= now; }
    bool compatible() const;
    JoinFailure validate(double now) const;
    std::string launchUrl() const;
    static std::optional<JoinTicket> parse(std::string_view url);
};

struct ExperienceLink {
    ExperienceId id;
    bool play = false;
    std::optional<ServerId> server;
    bool operator==(const ExperienceLink&) const = default;
    bool valid() const;
    static std::optional<ExperienceLink> parse(std::string_view url);
    std::string url() const;
};

std::optional<std::string> studioProjectFromUrl(std::string_view url);

}
