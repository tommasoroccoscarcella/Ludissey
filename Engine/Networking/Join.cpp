#include "Join.hpp"

#include "Engine/Core/Json.hpp"
#include "Engine/Core/Rules.hpp"
#include "Engine/Core/Text.hpp"
#include "Localization/Localization.hpp"
#include "Url.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace ld {

std::vector<JoinPhase> allPhases() {
    return {JoinPhase::ready, JoinPhase::resolvingExperience, JoinPhase::requestingSession, JoinPhase::launchingPlayer, JoinPhase::connecting,
            JoinPhase::loadingWorld, JoinPhase::loadingAssets, JoinPhase::enteringExperience, JoinPhase::playing};
}

std::string_view phaseName(JoinPhase phase) {
    switch (phase) {
    case JoinPhase::ready: return "ready";
    case JoinPhase::resolvingExperience: return "resolvingExperience";
    case JoinPhase::requestingSession: return "requestingSession";
    case JoinPhase::launchingPlayer: return "launchingPlayer";
    case JoinPhase::connecting: return "connecting";
    case JoinPhase::loadingWorld: return "loadingWorld";
    case JoinPhase::loadingAssets: return "loadingAssets";
    case JoinPhase::enteringExperience: return "enteringExperience";
    case JoinPhase::playing: return "playing";
    }
    return "ready";
}

std::string phaseMessage(JoinPhase phase) {
    switch (phase) {
    case JoinPhase::ready: return "";
    case JoinPhase::resolvingExperience: return tr("Preparazione…");
    case JoinPhase::requestingSession: return tr("Ricerca di un server…");
    case JoinPhase::launchingPlayer: return tr("Avvio dell’esperienza…");
    case JoinPhase::connecting: return tr("Connessione al server…");
    case JoinPhase::loadingWorld:
    case JoinPhase::loadingAssets: return tr("Caricamento esperienza…");
    case JoinPhase::enteringExperience: return tr("Ingresso…");
    case JoinPhase::playing: return tr("In gioco");
    }
    return "";
}

bool phaseActive(JoinPhase phase) { return phase != JoinPhase::ready && phase != JoinPhase::playing; }

std::set<JoinPhase> phaseSuccessors(JoinPhase phase) {
    switch (phase) {
    case JoinPhase::ready: return {JoinPhase::resolvingExperience, JoinPhase::connecting};
    case JoinPhase::resolvingExperience: return {JoinPhase::requestingSession, JoinPhase::ready};
    case JoinPhase::requestingSession: return {JoinPhase::launchingPlayer, JoinPhase::ready};
    case JoinPhase::launchingPlayer: return {JoinPhase::ready};
    case JoinPhase::connecting: return {JoinPhase::loadingWorld, JoinPhase::ready};
    case JoinPhase::loadingWorld: return {JoinPhase::loadingAssets, JoinPhase::ready};
    case JoinPhase::loadingAssets: return {JoinPhase::enteringExperience, JoinPhase::ready};
    case JoinPhase::enteringExperience: return {JoinPhase::playing, JoinPhase::ready};
    case JoinPhase::playing: return {JoinPhase::ready};
    }
    return {};
}

bool JoinStateMachine::move(JoinPhase next) {
    if (next != current && !phaseSuccessors(current).count(next)) return false;
    if (next == current) return true;
    current = next;
    trail.push_back(next);
    return true;
}

std::vector<JoinFailure> allFailures() {
    return {JoinFailure::experienceUnavailable, JoinFailure::sessionFailure, JoinFailure::joinRejected, JoinFailure::joinExpired, JoinFailure::playerLaunchFailure,
            JoinFailure::connectionTimeout, JoinFailure::worldLoadFailure, JoinFailure::assetLoadFailure, JoinFailure::versionMismatch, JoinFailure::networkFailure,
            JoinFailure::serverFull, JoinFailure::serverClosed, JoinFailure::serverBusy, JoinFailure::disconnected};
}

std::string_view failureName(JoinFailure failure) {
    switch (failure) {
    case JoinFailure::none: return "none";
    case JoinFailure::experienceUnavailable: return "experienceUnavailable";
    case JoinFailure::sessionFailure: return "sessionFailure";
    case JoinFailure::joinRejected: return "joinRejected";
    case JoinFailure::joinExpired: return "joinExpired";
    case JoinFailure::playerLaunchFailure: return "playerLaunchFailure";
    case JoinFailure::connectionTimeout: return "connectionTimeout";
    case JoinFailure::worldLoadFailure: return "worldLoadFailure";
    case JoinFailure::assetLoadFailure: return "assetLoadFailure";
    case JoinFailure::versionMismatch: return "versionMismatch";
    case JoinFailure::networkFailure: return "networkFailure";
    case JoinFailure::serverFull: return "serverFull";
    case JoinFailure::serverClosed: return "serverClosed";
    case JoinFailure::serverBusy: return "serverBusy";
    case JoinFailure::disconnected: return "disconnected";
    }
    return "none";
}

std::string failureTitle(JoinFailure failure) {
    switch (failure) {
    case JoinFailure::none: return "";
    case JoinFailure::experienceUnavailable: return tr("Esperienza non disponibile");
    case JoinFailure::sessionFailure: return tr("Nessun server pronto");
    case JoinFailure::joinRejected: return tr("Ingresso non riuscito");
    case JoinFailure::joinExpired: return tr("Ingresso scaduto");
    case JoinFailure::playerLaunchFailure: return tr("Il gioco non si è aperto");
    case JoinFailure::connectionTimeout: return tr("Connessione interrotta");
    case JoinFailure::worldLoadFailure: return tr("Mondo non caricato");
    case JoinFailure::assetLoadFailure: return tr("Mondo incompleto");
    case JoinFailure::versionMismatch: return tr("Aggiorna Ludissey");
    case JoinFailure::networkFailure: return tr("Connessione interrotta");
    case JoinFailure::serverFull: return tr("Server pieno");
    case JoinFailure::serverClosed: return tr("Server chiuso");
    case JoinFailure::serverBusy: return tr("Server occupati");
    case JoinFailure::disconnected: return tr("Disconnesso");
    }
    return "";
}

std::string failureMessage(JoinFailure failure) {
    switch (failure) {
    case JoinFailure::none: return "";
    case JoinFailure::experienceUnavailable: return tr("Il creatore l’ha chiusa o rimossa. Prova un’altra esperienza.");
    case JoinFailure::sessionFailure: return tr("Non siamo riusciti a prepararti un server. Riprova tra poco.");
    case JoinFailure::joinRejected: return tr("Il server non ti ha fatto entrare. Riprova o scegli un altro server.");
    case JoinFailure::joinExpired: return tr("È passato troppo tempo dalla richiesta. Riprova.");
    case JoinFailure::playerLaunchFailure: return tr("Chiudi Ludissey, riaprilo e riprova.");
    case JoinFailure::connectionTimeout: return tr("Il server non risponde. Controlla la rete e riprova.");
    case JoinFailure::worldLoadFailure: return tr("Il mondo non si è caricato. Riprova tra poco.");
    case JoinFailure::assetLoadFailure: return tr("Manca un pezzo del mondo. Riprova tra poco.");
    case JoinFailure::versionMismatch: return tr("Questa esperienza richiede l’ultima versione. Scaricala da ludissey.com.");
    case JoinFailure::networkFailure: return tr("Controlla la rete e riprova.");
    case JoinFailure::serverFull: return tr("Scegli un altro server oppure premi Gioca: ti mettiamo dove c’è posto.");
    case JoinFailure::serverClosed: return tr("Questo server non c’è più. Premi Gioca per entrare in un altro.");
    case JoinFailure::serverBusy: return tr("C’è tanta gente in questo momento. Riprova tra poco.");
    case JoinFailure::disconnected: return tr("Hai perso la connessione col server.");
    }
    return "";
}

bool failureRetryable(JoinFailure failure) { return failure != JoinFailure::versionMismatch && failure != JoinFailure::experienceUnavailable; }

FailureAction failureAction(JoinFailure failure) {
    if (failure == JoinFailure::serverFull || failure == JoinFailure::serverClosed) return FailureAction::play;
    return failureRetryable(failure) ? FailureAction::retry : FailureAction::none;
}

JoinFailure failureFromCode(std::string_view code) {
    if (code == "ExperienceUnavailable") return JoinFailure::experienceUnavailable;
    if (code == "WorldCorrupt" || code == "WorldChanged") return JoinFailure::worldLoadFailure;
    if (code == "VersionMismatch") return JoinFailure::versionMismatch;
    if (code == "ServerFull") return JoinFailure::serverFull;
    if (code == "ServerClosed") return JoinFailure::serverClosed;
    if (code == "ServerBusy" || code == "RegistryFull" || code == "ExperienceFull") return JoinFailure::serverBusy;
    if (code == "SessionExpired") return JoinFailure::joinExpired;
    if (code == "SessionUsed" || code == "SessionRejected") return JoinFailure::joinRejected;
    if (code == "SessionEnded") return JoinFailure::disconnected;
    return JoinFailure::sessionFailure;
}

std::optional<std::string> serviceCode(std::string_view message) {
    if (!startsWith(message, "[")) return std::nullopt;
    auto end = message.find(']');
    if (end == std::string_view::npos) return std::nullopt;
    return std::string(message.substr(1, end - 1));
}

bool JoinTicket::compatible() const { return playerProtocol == rules::playerProtocol; }

JoinFailure JoinTicket::validate(double now) const {
    if (sessionId.empty() || joinTicket.empty() || experienceId.empty() || nonce.empty() || serverId.empty() || serverSequence <= 0) return JoinFailure::joinRejected;
    if (!compatible()) return JoinFailure::versionMismatch;
    if (expired(now)) return JoinFailure::joinExpired;
    return JoinFailure::none;
}

std::string JoinTicket::launchUrl() const {
    Url url;
    url.scheme = "ludissey-player";
    url.host = "join";
    url.query = {{"session", sessionId.text()}, {"ticket", joinTicket}, {"experience", experienceId.text()}, {"nonce", nonce}, {"protocol", std::to_string(playerProtocol)},
                 {"expires", std::to_string(static_cast<long long>(expiresAt))}, {"server", serverId.text()}, {"sequence", std::to_string(serverSequence)}};
    return url.text();
}

std::optional<JoinTicket> JoinTicket::parse(std::string_view text) {
    auto url = Url::parse(text);
    if (!url || url->scheme != "ludissey-player" || url->host != "join") return std::nullopt;
    auto session = url->value("session"), ticket = url->value("ticket"), experience = url->value("experience"), nonce = url->value("nonce");
    auto proto = url->value("protocol"), expires = url->value("expires"), server = url->value("server"), sequence = url->value("sequence");
    if (!session || !ticket || !experience || !nonce || !proto || !expires || !server || !sequence) return std::nullopt;
    auto integer = [](const std::string& text) -> std::optional<int> {
        if (text.empty() || text.size() > 9) return std::nullopt;
        for (std::size_t i = text[0] == '-' ? 1 : 0; i < text.size(); ++i)
            if (!std::isdigit(static_cast<unsigned char>(text[i]))) return std::nullopt;
        return std::stoi(text);
    };
    auto protocolNumber = integer(*proto), sequenceNumber = integer(*sequence);
    auto expiry = parseNumber(*expires);
    if (!protocolNumber || !sequenceNumber || !expiry) return std::nullopt;
    return JoinTicket{SessionId(*session), *ticket, *expiry, *protocolNumber, ExperienceId(*experience), *nonce, ServerId(*server), *sequenceNumber};
}

bool ExperienceLink::valid() const { return isUuid(id.text()) && (!server || isUuid(server->text())); }

std::optional<ExperienceLink> ExperienceLink::parse(std::string_view text) {
    auto url = Url::parse(text);
    if (!url || url->scheme != "ludissey" || url->host != "experience") return std::nullopt;
    auto parts = url->pathComponents();
    if (parts.empty()) return ExperienceLink{ExperienceId(), false, std::nullopt};
    ExperienceLink link{ExperienceId(lowercase(parts[0])), parts.size() > 1 && parts[1] == "play", std::nullopt};
    if (auto server = url->value("server")) link.server = ServerId(lowercase(*server));
    return link;
}

std::string ExperienceLink::url() const {
    std::string out = "ludissey://experience/" + id.text() + (play ? "/play" : "");
    if (server) out += "?server=" + percentEncode(server->text());
    return out;
}

std::optional<std::string> studioProjectFromUrl(std::string_view text) {
    auto url = Url::parse(text);
    if (!url || url->scheme != "ludissey-studio" || url->host != "open") return std::nullopt;
    auto project = url->value("project");
    if (!project || !isUuid(*project) || std::any_of(project->begin(), project->end(), [](char c) { return c >= 'A' && c <= 'F'; })) return std::nullopt;
    return *project;
}

}
