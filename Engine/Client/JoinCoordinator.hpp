#pragma once

#include "Engine/Backend/Backend.hpp"
#include "Engine/Client/Models.hpp"
#include "Engine/Networking/Join.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace ld {

namespace ludissey_target {
inline constexpr const char* client = "com.ludissey.mac";
inline constexpr const char* studio = "com.ludissey.studio";
std::string current();
void setCurrent(std::string identifier);
bool isStudio();
std::string build();
}

class LaunchRequest {
public:
    static LaunchRequest& shared();
    std::optional<JoinTicket> joinTicket;
    std::optional<std::string> studioProject;
    std::optional<ExperienceLink> experienceLink;
    std::function<void()> changed;
    void accept(std::string_view url);
    void setTicket(std::optional<JoinTicket> ticket);
};

class JoinSessionProvider {
public:
    virtual ~JoinSessionProvider() = default;
    virtual Async<JoinTicket> requestSession(ExperienceId experienceId, std::optional<ServerId> serverId) = 0;
    virtual Async<void> release(JoinTicket ticket) = 0;
};

class JoinFailureError : public ServiceError {
public:
    explicit JoinFailureError(JoinFailure f) : ServiceError(std::string(failureMessage(f))), failure(f) {}
    JoinFailure failure;
};

JoinFailure joinFailureOf(std::exception_ptr error);

class BackendJoinSessionProvider final : public JoinSessionProvider {
public:
    explicit BackendJoinSessionProvider(std::shared_ptr<Backend> backend) : backend(std::move(backend)) {}
    Async<JoinTicket> requestSession(ExperienceId experienceId, std::optional<ServerId> serverId) override;
    Async<void> release(JoinTicket ticket) override;

private:
    std::shared_ptr<Backend> backend;
};

class JoinCoordinator {
public:
    explicit JoinCoordinator(std::shared_ptr<JoinSessionProvider> provider);
    JoinPhase phase() const { return machine.phase(); }
    const JoinStateMachine& state() const { return machine; }
    std::optional<JoinFailure> failure() const { return failed; }
    const std::optional<ExperienceEntry>& experience() const { return entry; }
    const std::optional<ServerId>& server() const { return target; }
    bool busy() const { return phaseActive(phase()) && !failed; }
    std::shared_ptr<JoinSessionProvider> provider;
    std::function<void()> changed;
    void play(const ExperienceEntry& experience, std::optional<ServerId> server = std::nullopt);
    void retry();
    void cancel();

private:
    JoinStateMachine machine;
    std::optional<JoinFailure> failed;
    std::optional<ExperienceEntry> entry;
    std::optional<ServerId> target;
    std::optional<JoinTicket> ticket;
    std::uint64_t generation = 0;
    Async<void> run(ExperienceEntry experience, std::optional<ServerId> server, std::uint64_t token);
    void notify();
};

}
