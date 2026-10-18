#include "JoinCoordinator.hpp"

#include "Engine/Core/Rules.hpp"

#include <chrono>

namespace ld {

namespace ludissey_target {

namespace {

std::string& identifier() {
    static std::string value = client;
    return value;
}

std::string& version() {
    static std::string value = LUDISSEY_VERSION;
    return value;
}

}

std::string current() { return identifier(); }
void setCurrent(std::string value) { identifier() = std::move(value); }
bool isStudio() { return identifier() == studio; }
std::string build() { return version(); }

}

LaunchRequest& LaunchRequest::shared() {
    static LaunchRequest request;
    return request;
}

void LaunchRequest::accept(std::string_view url) {
    if (auto ticket = JoinTicket::parse(url)) joinTicket = ticket;
    if (auto project = studioProjectFromUrl(url)) studioProject = project;
    if (auto link = ExperienceLink::parse(url)) experienceLink = link;
    if (changed) changed();
}

void LaunchRequest::setTicket(std::optional<JoinTicket> ticket) {
    joinTicket = std::move(ticket);
    if (changed) changed();
}

JoinFailure joinFailureOf(std::exception_ptr error) {
    try {
        std::rethrow_exception(error);
    } catch (const JoinFailureError& e) {
        return e.failure;
    } catch (const NetworkError& e) {
        return e.timedOut ? JoinFailure::connectionTimeout : JoinFailure::networkFailure;
    } catch (const ServiceError& e) {
        auto code = e.code();
        return code ? failureFromCode(*code) : JoinFailure::sessionFailure;
    } catch (...) {
        return JoinFailure::sessionFailure;
    }
}

Async<JoinTicket> BackendJoinSessionProvider::requestSession(ExperienceId experienceId, std::optional<ServerId> serverId) {
    Json parameters = Json::object({{"experienceId", experienceId.text()}, {"playerProtocol", rules::playerProtocol}, {"clientBuild", ludissey_target::build()}, {"platform", "macOS"}});
    if (serverId) parameters.set("serverId", serverId->text());
    Json result;
    std::exception_ptr error;
    try {
        result = co_await backend->cloud("LudisseyRequestSession", parameters);
    } catch (...) {
        error = std::current_exception();
    }
    if (error) throw JoinFailureError(joinFailureOf(error));
    if (!result["sessionId"].isString() || !result["joinTicket"].isString() || !result["playerProtocol"].isNumber() || !result["expiresAt"].isNumber() || !result["nonce"].isString() ||
        !result["serverId"].isString() || !result["serverSequence"].isNumber() || result["experienceId"].stringOr("") != experienceId.text())
        throw JoinFailureError(JoinFailure::sessionFailure);
    JoinTicket ticket;
    ticket.sessionId = SessionId(result["sessionId"].string());
    ticket.joinTicket = result["joinTicket"].string();
    ticket.expiresAt = result["expiresAt"].number();
    ticket.playerProtocol = static_cast<int>(result["playerProtocol"].integer());
    ticket.experienceId = experienceId;
    ticket.nonce = result["nonce"].string();
    ticket.serverId = ServerId(result["serverId"].string());
    ticket.serverSequence = static_cast<int>(result["serverSequence"].integer());
    co_return ticket;
}

Async<void> BackendJoinSessionProvider::release(JoinTicket ticket) {
    try {
        co_await backend->cloud("LudisseyLeaveSession", Json::object({{"experienceId", ticket.experienceId.text()}, {"sessionId", ticket.sessionId.text()}}));
    } catch (const std::exception&) {
    }
}

JoinCoordinator::JoinCoordinator(std::shared_ptr<JoinSessionProvider> p) : provider(std::move(p)) {}

void JoinCoordinator::notify() {
    if (changed) changed();
}

void JoinCoordinator::play(const ExperienceEntry& experience, std::optional<ServerId> server) {
    if (busy() && entry && entry->id == experience.id && target == server) return;
    cancel();
    entry = experience;
    target = server;
    failed.reset();
    std::uint64_t token = ++generation;
    run(experience, server, token).start();
    notify();
}

void JoinCoordinator::retry() {
    if (!entry) return;
    bool allocate = failed == JoinFailure::serverFull || failed == JoinFailure::serverClosed;
    play(*entry, allocate ? std::nullopt : target);
}

void JoinCoordinator::cancel() {
    ++generation;
    if (ticket) {
        auto open = *ticket;
        auto p = provider;
        auto releasing = [](std::shared_ptr<JoinSessionProvider> p, JoinTicket t) -> Async<void> { co_await p->release(t); };
        releasing(p, open).start();
    }
    ticket.reset();
    machine.move(JoinPhase::ready);
    failed.reset();
    notify();
}

Async<void> JoinCoordinator::run(ExperienceEntry experience, std::optional<ServerId> server, std::uint64_t token) {
    auto cancelled = [&] { return token != generation; };
    auto advance = [&](JoinPhase next) {
        if (cancelled()) throw Cancelled();
        if (!machine.move(next)) throw JoinFailureError(JoinFailure::sessionFailure);
        notify();
    };
    std::exception_ptr error;
    try {
        advance(JoinPhase::resolvingExperience);
        if (experience.id.empty()) throw JoinFailureError(JoinFailure::experienceUnavailable);
        advance(JoinPhase::requestingSession);
        JoinTicket issued = co_await provider->requestSession(ExperienceId(experience.id), server);
        if (cancelled()) {
            co_await provider->release(issued);
            co_return;
        }
        ticket = issued;
        double now = std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
        JoinFailure invalid = issued.validate(now);
        if (invalid != JoinFailure::none) throw JoinFailureError(invalid);
        advance(JoinPhase::launchingPlayer);
        if (issued.launchUrl().empty()) throw JoinFailureError(JoinFailure::playerLaunchFailure);
        LaunchRequest::shared().setTicket(issued);
        ticket.reset();
        machine.move(JoinPhase::ready);
        entry.reset();
        notify();
    } catch (...) {
        error = std::current_exception();
    }
    if (!error) co_return;
    if (token != generation) co_return;
    if (ticket) {
        auto open = *ticket;
        ticket.reset();
        co_await provider->release(open);
    }
    bool cancelledNow = false;
    try {
        std::rethrow_exception(error);
    } catch (const Cancelled&) {
        cancelledNow = true;
    } catch (...) {
    }
    if (!cancelledNow && token == generation) failed = joinFailureOf(error);
    machine.move(JoinPhase::ready);
    notify();
}

}
