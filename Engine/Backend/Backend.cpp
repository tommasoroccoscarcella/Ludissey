#include "Backend.hpp"

#include "Engine/Backend/LocalServer.hpp"
#include "Engine/Core/Rules.hpp"
#include "Engine/Core/Text.hpp"
#include "Localization/Localization.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>

namespace ld {

namespace {

std::optional<std::filesystem::path>& supportOverride() {
    static std::optional<std::filesystem::path> folder;
    return folder;
}

}

std::filesystem::path supportFolder() {
    if (supportOverride()) return *supportOverride();
    if (const char* forced = std::getenv("LUDISSEY_SUPPORT")) return forced;
    const char* home = std::getenv("HOME");
    return std::filesystem::path(home ? home : "/tmp") / "Library/Application Support/Ludissey";
}

Json PlayerProfile::json() const { return Json::object({{"username", username}, {"gender", gender}, {"createdAt", createdAt}}); }

PlayerProfile PlayerProfile::fromJson(const Json& value) {
    if (!value["username"].isString() || !value["gender"].isString() || !value["createdAt"].isString()) throw ServiceError(tr("Profilo del giocatore non valido"));
    return {value["username"].string(), value["gender"].string(), value["createdAt"].string()};
}

SessionFile::SessionFile(std::filesystem::path location) : file(location.empty() ? supportFolder() / "Sessione" : std::move(location)) {}

std::optional<std::string> SessionFile::load() {
    std::error_code problem;
    if (!std::filesystem::is_regular_file(file, problem)) return std::nullopt;
    std::filesystem::permissions(file, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write, problem);
    std::ifstream stream(file, std::ios::binary);
    if (!stream) throw ServiceError(tr("Impossibile leggere la sessione salvata."));
    std::string value((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    if (value.empty()) return std::nullopt;
    return value;
}

void SessionFile::save(const std::string& value) {
    std::error_code problem;
    std::filesystem::create_directories(file.parent_path(), problem);
    auto pending = file;
    pending += ".nuova";
    int descriptor = ::open(pending.c_str(), O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    if (descriptor < 0) throw ServiceError(tr("Impossibile salvare la sessione."));
    bool written = ::fchmod(descriptor, S_IRUSR | S_IWUSR) == 0 && ::write(descriptor, value.data(), value.size()) == static_cast<ssize_t>(value.size());
    ::close(descriptor);
    if (written) std::filesystem::rename(pending, file, problem);
    if (!written || problem) {
        std::filesystem::remove(pending, problem);
        throw ServiceError(tr("Impossibile salvare la sessione."));
    }
}

void SessionFile::remove() {
    std::error_code problem;
    std::filesystem::remove(file, problem);
    if (problem) throw ServiceError(tr("Impossibile rimuovere la sessione salvata."));
}

bool sessionExpired(const std::string& code) {
    static const std::set<std::string> expired = {"InvalidSessionTicket", "NotAuthenticated", "SessionTicketExpired", "AccountBanned", "AccountDeleted"};
    return expired.count(code) > 0;
}

PlayFab::PlayFab(std::shared_ptr<SecretStore> store, std::shared_ptr<HttpTransport> http) : vault(std::move(store)), transport(std::move(http)) {}

void PlayFab::clear() {
    ticket.reset();
    ++generation;
}

Async<Json> PlayFab::call(std::string method, Json body, bool authenticated) {
    std::uint64_t current = generation;
    HttpRequest request;
    request.method = "POST";
    request.url = std::string("https://") + titleId + ".playfabapi.com/Client/" + method;
    request.timeout = 25;
    request.headers.push_back({"Content-Type", "application/json"});
    if (authenticated) {
        if (!ticket) throw ServiceError(tr("Accedi per continuare"));
        request.headers.push_back({"X-Authorization", *ticket});
    }
    request.body = body.dump();
    auto channel = transport;
    HttpResponse response = co_await background([channel, request] { return channel->perform(request); });
    if (current != generation) throw Cancelled();
    auto parsed = Json::parse(response.body);
    if (!parsed || !parsed->isObject()) throw ServiceError(tr("Risposta del server non valida"));
    const Json& result = *parsed;
    if (response.status != 200 || result["code"].integer(0) != 200) {
        static const std::map<std::string, std::string> messages = {
            {"InvalidUsernameOrPassword", tr("Nome utente o password non corretti")}, {"AccountNotFound", tr("Account non trovato")}, {"UsernameNotAvailable", tr("Nome utente già utilizzato")},
            {"InvalidPassword", tr("La password deve contenere da 6 a 100 caratteri")}, {"InvalidUsername", tr("Il nome utente deve contenere da 3 a 20 caratteri")}, {"AccountBanned", tr("Questo account è sospeso")},
            {"NotAuthenticated", tr("La sessione è scaduta: accedi nuovamente")}, {"InvalidSessionTicket", tr("La sessione è scaduta: accedi nuovamente")}, {"InsufficientFunds", tr("Saldo insufficiente")},
            {"APIRequestsDisabledForTitle", tr("Servizio temporaneamente non disponibile")}};
        std::string code = result["error"].stringOr("");
        auto known = messages.find(code);
        throw PlayFabRequestError(code, known != messages.end() ? known->second : result["errorMessage"].stringOr(tr("Impossibile completare la richiesta")));
    }
    co_return result["data"].isObject() ? result["data"] : Json::object();
}

Async<void> PlayFab::authenticate(std::string username, std::string password, bool registering) {
    std::string name = trim(username);
    std::size_t length = utf8Length(name), secret = utf8Length(password);
    if (length < 3 || length > 20 || secret < 6 || secret > 100) throw ServiceError(tr("Il nome deve avere da 3 a 20 caratteri, la password almeno 6."));
    Json data = Json::object({{"TitleId", titleId}, {"Username", name}, {"Password", password}});
    if (registering) data.set("RequireBothUsernameAndEmail", false);
    std::uint64_t current = generation;
    Json result = co_await call(registering ? "RegisterPlayFabUser" : "LoginWithPlayFab", data, false);
    if (!result["SessionTicket"].isString()) throw ServiceError(tr("Sessione non ricevuta"));
    if (current != generation) throw Cancelled();
    vault->save(result["SessionTicket"].string());
    ticket = result["SessionTicket"].string();
}

Async<bool> PlayFab::restoreSession() {
    auto saved = vault->load();
    if (!saved) co_return false;
    std::uint64_t current = generation;
    ticket = saved;
    std::exception_ptr failure;
    try {
        Json response = co_await call("GetAccountInfo");
        if (current != generation) throw Cancelled();
        if (!response["AccountInfo"]["PlayFabId"].isString()) throw ServiceError(tr("La sessione non è valida. Accedi di nuovo."));
        co_return true;
    } catch (...) {
        failure = std::current_exception();
    }
    if (current != generation) throw Cancelled();
    ticket.reset();
    try {
        std::rethrow_exception(failure);
    } catch (const PlayFabRequestError& error) {
        if (sessionExpired(error.code)) {
            vault->remove();
            co_return false;
        }
        throw;
    }
}

Async<Json> PlayFab::cloud(std::string function, Json parameters) {
    Json data = co_await call("ExecuteCloudScript", Json::object({{"FunctionName", function}, {"FunctionParameter", parameters}, {"RevisionSelection", "Live"}, {"GeneratePlayStreamEvent", false}}));
    if (data["Error"].isObject()) {
        for (const auto& log : data["Logs"].items())
            if (log["Data"]["Error"].isString()) throw ServiceError(log["Data"]["Error"].string());
        throw ServiceError(data["Error"]["Message"].stringOr(tr("Operazione non completata")));
    }
    if (!data["FunctionResult"].isObject()) throw ServiceError(tr("Risposta incompleta dal server"));
    co_return data["FunctionResult"];
}

LocalBackend::LocalBackend(std::shared_ptr<LocalServer> engine, PlayerId identifier) : server(std::move(engine)), player(std::move(identifier)) {}

Async<Json> LocalBackend::cloud(std::string function, Json parameters) { return server->invoke(player, std::move(function), std::move(parameters)); }

Async<Json> LocalBackend::client(std::string method, Json body) { return server->client(player, std::move(method), std::move(body)); }

std::shared_ptr<HttpTransport> LocalBackend::storage() { return server->storage(); }

const std::set<std::string>& RoutedBackend::required() {
    static const std::set<std::string> names = {"LudisseyBootstrap", "LudisseyWorlds", "LudisseyDiscover", "LudisseySaveWorld", "LudisseyPublishWorld", "LudisseyUnpublishWorld", "LudisseyDeleteWorld",
                                                "LudisseyAssetUpload", "LudisseyAssetFinalize", "LudisseyAssetResolve", "LudisseyPortraits", "LudisseyExperience", "LudisseyExperienceData", "LudisseyRequestSession",
                                                "LudisseyOpenSession", "LudisseyReadySession", "LudisseyHeartbeat", "LudisseyServerState", "LudisseyChat", "LudisseyLeaveSession", "LudisseyAvatar", "LudisseySaveAvatar",
                                                "LudisseyAcquireAvatarItem", "LudisseyFriends", "LudisseyRequestFriend", "LudisseyRespondFriend", "LudisseyRemoveFriend",
                                                "LudisseyPlayerProfile", "LudisseyPlayerAvatar"};
    return names;
}

RoutedBackend::RoutedBackend(std::shared_ptr<Backend> remote) : live(std::move(remote)) {}

std::shared_ptr<RoutedBackend> RoutedBackend::forPlayFab(std::shared_ptr<PlayFab> playfab) { return std::make_shared<RoutedBackend>(playfab); }

Async<Json> RoutedBackend::cloud(std::string function, Json parameters) { return live->cloud(std::move(function), std::move(parameters)); }

Async<Json> RoutedBackend::client(std::string method, Json body) { return live->client(std::move(method), std::move(body)); }

std::shared_ptr<HttpTransport> RoutedBackend::storage() { return live->storage(); }

std::optional<std::string> RoutedBackend::compatible(const Json& info) {
    if (!info["schemaVersion"].isNumber() || info["schemaVersion"].integer() != rules::schemaVersion)
        return tr("C’è una nuova versione di Ludissey. Aggiornala per continuare.");
    if (!info["playerProtocol"].isNumber() || info["playerProtocol"].integer() != rules::playerProtocol) return tr("C’è una nuova versione di Ludissey. Aggiornala per continuare.");
    std::set<std::string> handlers;
    for (const auto& h : info["handlers"].items())
        if (h.isString()) handlers.insert(h.string());
    std::vector<std::string> missing;
    for (const auto& name : required())
        if (!handlers.count(name)) missing.push_back(name);
    if (!missing.empty()) return tr("I server di Ludissey non sono aggiornati ({0}). Riprova più tardi.", join(missing, ", "));
    return std::nullopt;
}

Async<BackendMode> RoutedBackend::negotiate() {
    try {
        Json info = co_await live->cloud("LudisseyServerInfo", Json::object());
        bool older = !info["schemaVersion"].isNumber() || info["schemaVersion"].integer() != rules::schemaVersion || !info["playerProtocol"].isNumber() || info["playerProtocol"].integer() != rules::playerProtocol;
        auto problem = compatible(info);
        if (!problem) current = {BackendMode::Kind::live, ""};
        else current = {older ? BackendMode::Kind::outdated : BackendMode::Kind::unavailable, *problem};
    } catch (const Cancelled&) {
        throw;
    } catch (const std::exception& e) {
        current = {BackendMode::Kind::unavailable, tr("I server di Ludissey non rispondono ({0}). Riprova tra poco.", readable(e.what()))};
    }
    co_return current;
}

void RoutedBackend::reset() { current = {}; }

}
