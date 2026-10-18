#pragma once

#include "Engine/Core/Async.hpp"
#include "Engine/Core/Ids.hpp"
#include "Engine/Core/Json.hpp"
#include "Engine/Core/Rules.hpp"
#include "Engine/Networking/Http.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>

namespace ld {

std::filesystem::path supportFolder();

struct PlayerProfile {
    std::string username;
    std::string gender;
    std::string createdAt;
    Json json() const;
    static PlayerProfile fromJson(const Json& value);
};

class Backend {
public:
    virtual ~Backend() = default;
    virtual Async<Json> cloud(std::string function, Json parameters) = 0;
    virtual Async<Json> client(std::string method, Json body) = 0;
    virtual std::shared_ptr<HttpTransport> storage() = 0;
    virtual bool onThisMac() const { return false; }
};

class SecretStore {
public:
    virtual ~SecretStore() = default;
    virtual std::optional<std::string> load() = 0;
    virtual void save(const std::string& value) = 0;
    virtual void remove() = 0;
};

class SessionFile final : public SecretStore {
public:
    explicit SessionFile(std::filesystem::path location = {});
    std::optional<std::string> load() override;
    void save(const std::string& value) override;
    void remove() override;

private:
    std::filesystem::path file;
};

class MemoryStore final : public SecretStore {
public:
    std::optional<std::string> load() override { return value; }
    void save(const std::string& v) override { value = v; }
    void remove() override { value.reset(); }

private:
    std::optional<std::string> value;
};

bool sessionExpired(const std::string& code);

class PlayFabRequestError : public ServiceError {
public:
    PlayFabRequestError(std::string errorCode, const std::string& message) : ServiceError(message), code(std::move(errorCode)) {}
    std::string code;
};

class PlayFab final : public Backend {
public:
    static constexpr const char* titleId = rules::account::titleId.data();
    PlayFab(std::shared_ptr<SecretStore> vault = std::make_shared<SessionFile>(), std::shared_ptr<HttpTransport> transport = standardTransport());

    const std::shared_ptr<SecretStore> vault;
    void clear();
    Async<Json> call(std::string method, Json body = Json::object(), bool authenticated = true);
    Async<void> authenticate(std::string username, std::string password, bool registering);
    Async<bool> restoreSession();
    Async<Json> cloud(std::string function, Json parameters) override;
    Async<Json> client(std::string method, Json body) override { return call(std::move(method), std::move(body)); }
    std::shared_ptr<HttpTransport> storage() override { return transport; }

private:
    std::shared_ptr<HttpTransport> transport;
    std::optional<std::string> ticket;
    std::uint64_t generation = 1;
};

class LocalServer;

class LocalBackend final : public Backend {
public:
    LocalBackend(std::shared_ptr<LocalServer> server, PlayerId player);
    Async<Json> cloud(std::string function, Json parameters) override;
    Async<Json> client(std::string method, Json body) override;
    std::shared_ptr<HttpTransport> storage() override;
    bool onThisMac() const override { return true; }
    const std::shared_ptr<LocalServer> server;
    const PlayerId player;
};

struct BackendMode {
    enum class Kind { unknown, live, outdated, unavailable } kind = Kind::unknown;
    std::string reason;
    bool blocking() const { return kind == Kind::outdated || kind == Kind::unavailable; }
    bool operator==(const BackendMode&) const = default;
};

class RoutedBackend final : public Backend {
public:
    static const std::set<std::string>& required();

    explicit RoutedBackend(std::shared_ptr<Backend> live);
    static std::shared_ptr<RoutedBackend> forPlayFab(std::shared_ptr<PlayFab> playfab);

    Async<Json> cloud(std::string function, Json parameters) override;
    Async<Json> client(std::string method, Json body) override;
    std::shared_ptr<HttpTransport> storage() override;
    static std::optional<std::string> compatible(const Json& info);
    Async<BackendMode> negotiate();
    void reset();
    const BackendMode& mode() const { return current; }
    const std::shared_ptr<Backend> live;

private:
    BackendMode current;
};

}
