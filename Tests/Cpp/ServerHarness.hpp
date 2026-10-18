#pragma once

#include "Engine/Backend/Assets.hpp"
#include "Engine/Backend/Backend.hpp"
#include "Engine/Core/Rules.hpp"
#include "Engine/Core/Text.hpp"
#include "Engine/Experience/Experience.hpp"
#include "Golden.hpp"
#include "JavaScript.hpp"

#include <chrono>
#include <cmath>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>

namespace harness {

template <typename T>
T await(ld::Async<T> task, double seconds = 30) {
    std::optional<T> value;
    std::exception_ptr error;
    bool done = false;
    auto runner = [&](ld::Async<T> t) -> ld::Async<void> {
        try {
            value.emplace(co_await t);
        } catch (...) {
            error = std::current_exception();
        }
        done = true;
    };
    runner(std::move(task)).start();
    auto started = std::chrono::steady_clock::now();
    while (!done && std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count() < seconds) ld::fallbackQueue().pump(0.02);
    if (!done) throw ld::ServiceError("Attesa scaduta");
    if (error) std::rethrow_exception(error);
    return std::move(*value);
}

inline void awaitVoid(ld::Async<void> task, double seconds = 30) {
    auto wrapped = [](ld::Async<void> t) -> ld::Async<int> {
        co_await t;
        co_return 0;
    };
    await(wrapped(std::move(task)), seconds);
}

inline void pumpFor(double seconds) {
    auto end = std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(seconds));
    while (std::chrono::steady_clock::now() < end) ld::fallbackQueue().pump(0.005);
}

inline bool waitUntil(const std::function<bool()>& condition, double seconds = 10) {
    auto end = std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(seconds));
    while (!condition()) {
        if (std::chrono::steady_clock::now() > end) return false;
        ld::fallbackQueue().pump(0.01);
    }
    return true;
}

class ScriptWorld;

class ScriptBackend final : public ld::Backend {
public:
    ScriptBackend(std::shared_ptr<ScriptWorld> world, std::string player) : world(std::move(world)), player(std::move(player)) {}
    ld::Async<ld::Json> cloud(std::string function, ld::Json parameters) override;
    ld::Async<ld::Json> client(std::string method, ld::Json body) override;
    std::shared_ptr<ld::HttpTransport> storage() override;
    std::shared_ptr<ScriptWorld> world;
    std::string player;
    std::vector<std::string> calls;
};

class ScriptWorld : public std::enable_shared_from_this<ScriptWorld> {
public:
    ScriptWorld() {
        std::string failure;
        context.evaluateScript("var handlers = {}; var currentPlayerId = null;", "", failure);
        for (const char* name : {"CloudScript/Rules.js", "CloudScript/Avatar.js", "CloudScript/Ludissey.js", "Tests/Emulator/PlayFabEmulator.js"}) {
            if (!context.evaluateScript(golden::readText(golden::root() / name), name, failure)) throw ld::ServiceError(std::string(name) + ": " + failure);
        }
        setTime(std::round(std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count() * 1000));
    }

    std::string evaluate(const std::string& source) {
        std::lock_guard<std::recursive_mutex> guard(lock);
        return context.evaluate(source);
    }
    double number(const std::string& source) {
        std::lock_guard<std::recursive_mutex> guard(lock);
        return context.number(source);
    }
    double time() { return number("emulator.time"); }
    void setTime(double value) { evaluate("emulator.time = " + ld::formatFixed(value, 0)); }
    void advance(double seconds) { setTime(time() + seconds * 1000); }

    std::shared_ptr<ScriptBackend> registerPlayer(const std::string& player, const std::string& name) {
        evaluate("emulatorProfile(" + ld::javaScriptLiteral(player) + "," + ld::javaScriptLiteral(name) + ")");
        return std::make_shared<ScriptBackend>(shared_from_this(), player);
    }

    ld::Json call(const std::string& function, const std::string& player, const std::string& name, const ld::Json& parameters) {
        std::string raw = evaluate(function + "(" + ld::javaScriptLiteral(player) + "," + ld::javaScriptLiteral(name) + "," + ld::javaScriptLiteral(parameters.dump()) + ")");
        auto object = ld::Json::parse(raw);
        if (!object || !object->isObject()) throw ld::ServiceError("Risposta dell’emulatore non valida");
        if ((*object)["error"].isString()) throw ld::ServiceError((*object)["error"].string());
        return (*object)["result"].isObject() ? (*object)["result"] : ld::Json::object();
    }
    ld::Json invoke(const std::string& player, const std::string& name, const ld::Json& parameters = ld::Json::object()) { return call("emulatorInvoke", player, name, parameters); }
    ld::Json client(const std::string& player, const std::string& name, const ld::Json& parameters = ld::Json::object()) { return call("emulatorClient", player, name, parameters); }
    void storeBlob(const std::string& key, const std::string& data) { evaluate("emulator.blobs[" + ld::javaScriptLiteral(key) + "] = " + ld::javaScriptLiteral(ld::base64Encode(data))); }
    std::optional<std::string> blob(const std::string& key) {
        std::string value = evaluate("(function(v){ return typeof v === 'string' ? v : ''; })(emulator.blobs[" + ld::javaScriptLiteral(key) + "])");
        if (value.empty()) return std::nullopt;
        auto decoded = ld::base64Decode(value);
        if (!decoded) return std::nullopt;
        return std::string(decoded->begin(), decoded->end());
    }

    std::shared_ptr<ld::HttpTransport> transport();

private:
    ld::JavaScriptContext context;
    std::recursive_mutex lock;
};

class BlobTransport final : public ld::HttpTransport {
public:
    explicit BlobTransport(std::weak_ptr<ScriptWorld> world) : world(std::move(world)) {}
    ld::HttpResponse perform(const ld::HttpRequest& request) override {
        const std::string prefix = "ludissey-emulator://blob/";
        auto owner = world.lock();
        if (!owner || request.url.rfind(prefix, 0) != 0) throw ld::NetworkError("Indirizzo non valido", false);
        std::string key = request.url.substr(prefix.size());
        ld::HttpResponse response;
        if (request.method == "PUT") {
            owner->storeBlob(key, request.body);
            response.status = 201;
            return response;
        }
        if (auto data = owner->blob(key)) {
            response.status = 200;
            response.body = *data;
        } else response.status = 404;
        return response;
    }

private:
    std::weak_ptr<ScriptWorld> world;
};

inline std::shared_ptr<ld::HttpTransport> ScriptWorld::transport() { return std::make_shared<BlobTransport>(weak_from_this()); }

inline ld::Async<ld::Json> ScriptBackend::cloud(std::string function, ld::Json parameters) {
    calls.push_back(function);
    co_return world->invoke(player, function, parameters);
}

inline ld::Async<ld::Json> ScriptBackend::client(std::string method, ld::Json body) {
    calls.push_back(method);
    co_return world->client(player, method, body);
}

inline std::shared_ptr<ld::HttpTransport> ScriptBackend::storage() { return world->transport(); }

class Fixture {
public:
    std::shared_ptr<ScriptWorld> world = std::make_shared<ScriptWorld>();
    std::string owner = "Powner";
    std::string experience;

    explicit Fixture(int serverSize = 20, const std::string& genre = "Avventura") {
        world->registerPlayer(owner, "Costruttrice");
        known.insert(owner);
        ld::Experience blank = ld::Experience::blank();
        blank.title = "Arena di prova";
        blank.genre = genre;
        blank.serverSize = serverSize;
        experience = blank.id;
        world->invoke(owner, "LudisseyPublishWorld", ld::Json::object({{"world", blank.json()}}));
    }

    std::string player(int index) {
        std::string id = "P" + std::to_string(index);
        if (!known.count(id)) {
            world->registerPlayer(id, "Giocatore" + std::to_string(index));
            known.insert(id);
        }
        return id;
    }

    ld::Json request(const std::string& p, std::optional<std::string> server = std::nullopt) {
        ld::Json parameters = ld::Json::object({{"experienceId", experience}, {"playerProtocol", ld::rules::playerProtocol}});
        if (server) parameters.set("serverId", *server);
        return world->invoke(p, "LudisseyRequestSession", parameters);
    }

    ld::Json open(const std::string& p, const ld::Json& ticket) {
        return world->invoke(p, "LudisseyOpenSession", ld::Json::object({{"experienceId", experience}, {"sessionId", ticket["sessionId"].string()}, {"joinTicket", ticket["joinTicket"].string()}}));
    }

    ld::Json ready(const std::string& p, const std::string& session) { return world->invoke(p, "LudisseyReadySession", ld::Json::object({{"experienceId", experience}, {"sessionId", session}})); }

    std::pair<std::string, int> join(const std::string& p, std::optional<std::string> server = std::nullopt) {
        ld::Json ticket = request(p, server);
        open(p, ticket);
        std::string session = ticket["sessionId"].string();
        ready(p, session);
        return {session, static_cast<int>(ticket["serverSequence"].integer())};
    }

    void leave(const std::string& p, const std::string& session) { world->invoke(p, "LudisseyLeaveSession", ld::Json::object({{"experienceId", experience}, {"sessionId", session}})); }

    ld::ExperienceDetails details(std::optional<std::string> as = std::nullopt) {
        auto parsed = ld::ExperienceDetails::fromJson(world->invoke(as.value_or(owner), "LudisseyExperience", ld::Json::object({{"id", experience}})));
        if (!parsed) throw ld::ServiceError(parsed.error().message);
        return *parsed;
    }

    std::string objectName() const { return ld::replaceAll(experience, "-", ""); }

    ld::Json registry() {
        auto parsed = ld::Json::parse(world->evaluate("JSON.stringify(emulator.entities['e" + owner + "'].objects['" + objectName() + "'] || {servers:[]})"));
        return parsed ? *parsed : ld::Json::object();
    }

    struct Slot {
        int sequence = 0, players = 0, reserved = 0, capacity = 0;
    };

    std::vector<Slot> layout() {
        std::vector<Slot> out;
        ld::Json current = registry();
        for (const auto& s : current["servers"].items())
            out.push_back({static_cast<int>(s["sequence"].integer()), static_cast<int>(s["players"].size()), static_cast<int>(s["reservations"].size()), static_cast<int>(s["capacity"].integer())});
        return out;
    }

    std::vector<int> occupancy() {
        std::vector<int> out;
        for (const auto& s : layout()) out.push_back(s.players + s.reserved);
        return out;
    }

private:
    std::set<std::string> known;
};

inline bool refused(const std::string& code, const std::function<void()>& body) {
    try {
        body();
        return false;
    } catch (const ld::ServiceError& e) {
        return e.code() == std::optional<std::string>(code);
    } catch (...) {
        return false;
    }
}

}
