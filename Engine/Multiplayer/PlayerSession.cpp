#include "PlayerSession.hpp"

#include "Engine/Core/Log.hpp"
#include "Engine/Core/Rules.hpp"
#include "Engine/Core/Text.hpp"
#include "Localization/Localization.hpp"

#include <algorithm>
#include <chrono>

namespace ld {

namespace {

class JoinError : public ServiceError {
public:
    explicit JoinError(JoinFailure f) : ServiceError(std::string(failureMessage(f))), failure(f) {}
    JoinFailure failure;
};

JoinFailure classify(std::exception_ptr error) {
    try {
        std::rethrow_exception(error);
    } catch (const JoinError& e) {
        return e.failure;
    } catch (const NetworkError& e) {
        return e.timedOut ? JoinFailure::connectionTimeout : JoinFailure::networkFailure;
    } catch (const ServiceError& e) {
        auto code = e.code();
        if (code == std::optional<std::string>("AssetLoadFailure")) return JoinFailure::assetLoadFailure;
        return code ? failureFromCode(*code) : JoinFailure::sessionFailure;
    } catch (...) {
        return JoinFailure::sessionFailure;
    }
}

double now() { return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count(); }

}

std::optional<AvatarSnapshot> PlayerGameplay::local() const {
    std::lock_guard<std::mutex> guard(lock);
    return storedLocal;
}

void PlayerGameplay::setLocal(std::optional<AvatarSnapshot> snapshot) {
    std::lock_guard<std::mutex> guard(lock);
    storedLocal = snapshot;
}

std::map<SessionId, RemoteAvatar> PlayerGameplay::remotes() const {
    std::lock_guard<std::mutex> guard(lock);
    return storedRemotes;
}

void PlayerGameplay::setRemotes(std::map<SessionId, RemoteAvatar> value) {
    std::lock_guard<std::mutex> guard(lock);
    storedRemotes = std::move(value);
}

void PlayerGameplay::report(const OutputLine& line) {
    console.push_back(line);
    if (console.size() > 400) console.erase(console.begin(), console.begin() + static_cast<std::ptrdiff_t>(console.size() - 400));
}

PlayerSession::PlayerSession(JoinTicket t, std::shared_ptr<Backend> b, std::shared_ptr<AssetService> a) : ticket(std::move(t)), backend(std::move(b)), assets(std::move(a)) {}

PlayerSession::~PlayerSession() {
    *alive = false;
    if (direct) direct->close();
}

Json PlayerSession::identity() const { return Json::object({{"experienceId", ticket.experienceId.text()}, {"sessionId", ticket.sessionId.text()}}); }

void PlayerSession::notify() {
    if (changed) changed();
}

void PlayerSession::move(JoinPhase next) {
    Log::info("Player", std::string("fase ") + std::string(phaseName(next)));
    if (!machine.move(next)) throw JoinError(JoinFailure::sessionFailure);
}

Async<void> PlayerSession::start() {
    if (phase() != JoinPhase::connecting || failed || hasLeft) co_return;
    auto self = shared_from_this();
    std::exception_ptr error;
    try {
        JoinFailure check = ticket.validate(now());
        if (check != JoinFailure::none) throw JoinError(check);
        Json request = identity();
        request.set("joinTicket", ticket.joinTicket);
        Json response = co_await backend->cloud("LudisseyOpenSession", request);
        opened = true;
        if (hasLeft) {
            co_await release();
            co_return;
        }
        if (!response.contains("world") || !response.contains("server")) throw JoinError(JoinFailure::worldLoadFailure);
        auto joined = ExperienceServer::fromJson(response["server"]);
        if (!joined || joined->id != ticket.serverId.text() || joined->sequence != ticket.serverSequence) throw JoinError(JoinFailure::joinRejected);
        assigned = *joined;
        move(JoinPhase::loadingWorld);
        notify();
        auto loaded = Experience::fromJson(response["world"]);
        if (!loaded || loaded->id != ticket.experienceId.text()) throw JoinError(JoinFailure::worldLoadFailure);
        if (loaded->compatibility()) throw JoinError(JoinFailure::versionMismatch);
        move(JoinPhase::loadingAssets);
        notify();
        auto resolved = co_await assets->resolve(*loaded);
        for (const auto& asset : AssetService::references(*loaded))
            if (!resolved.count(asset)) throw JoinError(JoinFailure::assetLoadFailure);
        for (const auto& asset : AssetService::meshes(*loaded))
            if (meshReadable && !meshReadable(resolved.at(asset))) throw JoinError(JoinFailure::assetLoadFailure);
        if (hasLeft) co_return;
        move(JoinPhase::enteringExperience);
        notify();
        Json confirmation = identity();
        confirmation.set("revision", loaded->revision);
        Json confirmed = co_await backend->cloud("LudisseyReadySession", confirmation);
        if (hasLeft) {
            co_await release();
            co_return;
        }
        apply(confirmed);
        if (confirmed["player"].isString()) localPlayer = confirmed["player"].string();
        if (confirmed["visits"].isNumber()) visitCount = static_cast<int>(confirmed["visits"].integer());
        if (confirmed["presence"].isString()) {
            presence = confirmed["presence"].string();
            if (backend->onThisMac()) {
                auto opened = DirectTransport::open(ticket.sessionId);
                if (opened) direct = std::move(*opened);
            }
        }
        loadedWorld = *loaded;
        isReady = true;
        move(JoinPhase::playing);
        startPulse();
        startStream();
        notify();
    } catch (...) {
        error = std::current_exception();
    }
    if (!error) co_return;
    failed = classify(error);
    machine.move(JoinPhase::ready);
    notify();
    co_await release();
}

void PlayerSession::apply(const Json& state) {
    if (state.contains("server"))
        if (auto current = ExperienceServer::fromJson(state["server"])) assigned = *current;
    if (state["roster"].isArray()) {
        names.clear();
        for (const auto& n : state["roster"].items())
            if (n.isString()) names.push_back(n.string());
    }
    if (state["chat"].isArray() && state["chat"].size() > 0) {
        int highest = cursor;
        for (const auto& m : state["chat"].items()) {
            if (!m["sequence"].isNumber() || !m["name"].isString() || !m["text"].isString()) continue;
            ChatMessage message{static_cast<int>(m["sequence"].integer()), m["name"].string(), m["text"].string()};
            if (message.sequence > cursor) messages.push_back(message);
            highest = std::max(highest, message.sequence);
        }
        cursor = highest;
        if (messages.size() > static_cast<std::size_t>(rules::chatMessages)) messages.erase(messages.begin(), messages.begin() + static_cast<std::ptrdiff_t>(messages.size() - rules::chatMessages));
    }
}

void PlayerSession::startPulse() {
    std::weak_ptr<PlayerSession> weak = shared_from_this();
    auto token = alive;
    auto loop = [](std::weak_ptr<PlayerSession> weak, std::shared_ptr<bool> token) -> Async<void> {
        int interval = rules::replication::intervalMilliseconds;
        int elapsed = 0, polled = 0;
        while (*token) {
            co_await sleepFor(interval / 1000.0);
            auto self = weak.lock();
            if (!self || !*token || self->hasLeft) co_return;
            co_await self->exchange(now());
            polled += interval;
            if (polled < rules::statePollSeconds * 1000) continue;
            polled = 0;
            elapsed += rules::statePollSeconds;
            bool beat = elapsed >= rules::heartbeatSeconds;
            if (beat) elapsed = 0;
            co_await self->refresh(beat);
        }
    };
    loop(weak, token).start();
}

void PlayerSession::startStream() {
    if (!direct) return;
    std::weak_ptr<PlayerSession> weak = shared_from_this();
    auto token = alive;
    int interval = direct->interval();
    auto loop = [](std::weak_ptr<PlayerSession> weak, std::shared_ptr<bool> token, int interval) -> Async<void> {
        while (*token) {
            co_await sleepFor(interval / 1000.0);
            auto self = weak.lock();
            if (!self || !*token || self->hasLeft) co_return;
            co_await self->flow(now());
        }
    };
    loop(weak, token, interval).start();
}

std::optional<ReplicationFrame> PlayerSession::outgoing() {
    if (!gameplay) return std::nullopt;
    auto local = gameplay->local();
    if (!local) return std::nullopt;
    ++published;
    local->sequence = published;
    ReplicationFrame frame;
    frame.player = localPlayer;
    frame.name = displayName;
    frame.snapshot = *local;
    frame.avatar = avatar;
    if (direct) frame.endpoint = direct->port();
    return frame;
}

Async<void> PlayerSession::flow(double moment) {
    if (!isReady || hasLeft || !direct) co_return;
    if (auto frame = outgoing()) direct->send(*frame);
    auto frames = direct->receive();
    if (!frames) co_return;
    std::map<SessionId, ReplicationFrame> known;
    for (const auto& [session, frame] : *frames)
        if (remoteAvatars.count(session)) known[session] = frame;
    merge(known, moment, false);
}

Async<void> PlayerSession::exchange(double moment) {
    if (!isReady || hasLeft || !presence) co_return;
    auto self = shared_from_this();
    std::string group = *presence;
    if (auto frame = outgoing()) {
        try {
            co_await backend->client("UpdateSharedGroupData", Json::object({{"SharedGroupId", group}, {"Data", Json::object({{ticket.sessionId.text(), replication_protocol::encode(*frame)}})}}));
        } catch (const std::exception&) {
        }
    }
    Json response;
    bool received = false;
    try {
        response = co_await backend->client("GetSharedGroupData", Json::object({{"SharedGroupId", group}}));
        received = true;
    } catch (const std::exception&) {
    }
    if (!received || hasLeft) co_return;
    std::map<SessionId, ReplicationFrame> others;
    if (response["Data"].isObject())
        for (const auto& [key, value] : response["Data"].members()) {
            if (key == ticket.sessionId.text() || !value["Value"].isString()) continue;
            if (auto frame = replication_protocol::decode(value["Value"].string())) others[SessionId(key)] = *frame;
        }
    if (direct) {
        std::set<SessionId> keeping;
        for (const auto& [session, frame] : others) {
            keeping.insert(session);
            if (frame.endpoint) direct->connect(session, *frame.endpoint);
        }
        direct->forget(keeping);
    }
    merge(others, moment, true);
}

void PlayerSession::merge(const std::map<SessionId, ReplicationFrame>& frames, double moment, bool authoritative) {
    std::map<SessionId, RemoteAvatar> next;
    if (authoritative) {
        for (const auto& [session, avatar] : remoteAvatars)
            if (frames.count(session)) next[session] = avatar;
    } else next = remoteAvatars;
    for (const auto& [session, frame] : frames) {
        if (session == ticket.sessionId || (frame.player && frame.player == localPlayer)) continue;
        if (auto last = retired.find(session); last != retired.end() && frame.snapshot.sequence <= last->second) continue;
        retired.erase(session);
        RemoteAvatar avatarState = next.count(session) ? next[session] : RemoteAvatar{};
        if (!next.count(session)) avatarState.session = session;
        avatarState.name = frame.name;
        avatarState.avatar = frame.avatar ? std::optional<AvatarConfiguration>(frame.avatar->validated(frame.name, std::string(rules::avatar::standard::bodyPreset))) : std::nullopt;
        avatarState.receive(frame.snapshot, moment);
        next[session] = avatarState;
    }
    if (authoritative) {
        for (auto it = next.begin(); it != next.end();) {
            if (moment - it->second.received >= rules::replication::staleSeconds) {
                retired[it->first] = it->second.snapshots.empty() ? 0 : it->second.snapshots.back().sequence;
                it = next.erase(it);
            } else ++it;
        }
        for (auto it = retired.begin(); it != retired.end();) {
            if (!frames.count(it->first)) it = retired.erase(it);
            else ++it;
        }
    }
    bool same = next.size() == remoteAvatars.size() && std::equal(next.begin(), next.end(), remoteAvatars.begin(), [](const auto& a, const auto& b) {
                    return a.first == b.first && a.second.name == b.second.name && a.second.snapshots == b.second.snapshots && a.second.received == b.second.received && a.second.avatar == b.second.avatar;
                });
    if (same) return;
    remoteAvatars = next;
    if (gameplay) gameplay->setRemotes(next);
    notify();
}

Async<void> PlayerSession::refresh(bool heartbeat) {
    if (!isReady || hasLeft) co_return;
    auto self = shared_from_this();
    std::exception_ptr error;
    try {
        if (heartbeat) apply(co_await backend->cloud("LudisseyHeartbeat", identity()));
        Json request = identity();
        request.set("after", cursor);
        apply(co_await backend->cloud("LudisseyServerState", request));
        notify();
    } catch (...) {
        error = std::current_exception();
    }
    if (error && classify(error) == JoinFailure::disconnected) {
        failed = JoinFailure::disconnected;
        isReady = false;
        *alive = false;
        notify();
    }
}

Async<void> PlayerSession::send(std::string text) {
    std::string message = utf8Prefix(trim(text), rules::chatCharacters);
    if (!isReady || message.empty()) co_return;
    auto self = shared_from_this();
    Json request = identity();
    request.set("message", message);
    std::string problem;
    try {
        co_await backend->cloud("LudisseyChat", request);
    } catch (const ServiceError& e) {
        problem = e.reason();
    } catch (const std::exception&) {
        problem = tr("Messaggio non inviato: controlla la connessione e riprova.");
    }
    chatError = problem;
    co_await refresh(false);
}

Async<void> PlayerSession::release() {
    if (!opened) co_return;
    try {
        co_await backend->cloud("LudisseyLeaveSession", identity());
    } catch (const std::exception&) {
    }
}

Async<void> PlayerSession::leave() {
    if (hasLeft) co_return;
    auto self = shared_from_this();
    hasLeft = true;
    *alive = false;
    if (direct) direct->close();
    isReady = false;
    notify();
    co_await release();
}

}
