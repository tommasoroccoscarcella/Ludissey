#include "Store.hpp"

#include "Engine/Core/Rules.hpp"
#include "Engine/Core/Text.hpp"
#include "Localization/Localization.hpp"

#include <algorithm>
#include <chrono>

namespace ld {

namespace {

std::vector<Experience> experiences(const Json& list) {
    std::vector<Experience> out;
    for (const auto& item : list.items()) {
        auto world = Experience::fromJson(item);
        if (!world) throw ServiceError(world.error().message);
        out.push_back(*world);
    }
    return out;
}

std::vector<WorldSummary> summaries(const Json& list) {
    std::vector<WorldSummary> out;
    for (const auto& item : list.items()) {
        if (!item["id"].isString() || !item["title"].isString() || !item["owner"].isString() || !item["author"].isString()) throw ServiceError(tr("Elenco delle esperienze non valido"));
        out.push_back(WorldSummary::fromJson(item));
    }
    return out;
}

double wall() { return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count(); }

}

Store::Store(std::shared_ptr<PlayFab> playfab, std::shared_ptr<Backend> chosen, std::shared_ptr<AvatarRepository> repository, bool restore)
    : restoringSession(restore), api(std::move(playfab)), backend(chosen ? chosen : std::static_pointer_cast<Backend>(RoutedBackend::forPlayFab(api))),
      join(std::make_shared<JoinCoordinator>(std::make_shared<BackendJoinSessionProvider>(backend))), avatars(repository ? repository : std::make_shared<BackendAvatarRepository>(backend)),
      avatarCache(AvatarCache::standard()), didRestoreSession(!restore) {
    join->changed = [this] { notify(); };
}

Store::~Store() {
    *alive = false;
    join->changed = nullptr;
}

void Store::notify() {
    if (changed) changed();
}

void Store::setWorlds(std::vector<Experience> value) {
    ownWorlds = std::move(value);
    rebuildCatalog();
}

void Store::setDiscover(std::vector<WorldSummary> value) {
    publicWorlds = std::move(value);
    rebuildCatalog();
}

void Store::setExperienceMetadata(std::map<std::string, ExperienceMetadata> value) {
    metadata = std::move(value);
    rebuildCatalog();
}

Async<void> Store::connectBackend() {
    auto routed = std::dynamic_pointer_cast<RoutedBackend>(backend);
    if (!routed || !profile) co_return;
    auto self = shared_from_this();
    std::uint64_t current = epoch;
    auto mode = co_await routed->negotiate();
    if (current != epoch) co_return;
    serverMode = mode;
    notify();
}

void Store::reconnect() {
    auto self = shared_from_this();
    perform([self]() -> Async<void> {
        co_await self->connectBackend();
        if (!self->serverMode.blocking()) co_await self->refresh();
    });
}

void Store::openDownloads() {
    if (openUrl) openUrl(std::string(rules::site::url) + "/#scarica");
}

Async<void> Store::restoreSession() {
    if (didRestoreSession) co_return;
    didRestoreSession = true;
    auto self = shared_from_this();
    std::uint64_t current = epoch;
    std::exception_ptr failure;
    try {
        bool restored = co_await api->restoreSession();
        if (restored && current == epoch) {
            Json result = co_await api->cloud("LudisseyBootstrap", Json::object());
            if (current == epoch) {
                if (result["needsProfile"].isBool() && result["needsProfile"].boolean()) needsProfile = true;
                else {
                    profile = PlayerProfile::fromJson(result["profile"]);
                    playerId = result["id"].stringOr("");
                    co_await loadAvatar();
                    co_await connectBackend();
                    try {
                        co_await refresh();
                    } catch (const std::exception&) {
                        notice = tr("Sessione ripristinata. Aggiorna i dati quando la connessione è disponibile.");
                    }
                }
            }
        }
    } catch (...) {
        failure = std::current_exception();
    }
    if (failure && current == epoch) {
        profile.reset();
        needsProfile = false;
    }
    if (current == epoch) restoringSession = false;
    notify();
}

void Store::navigate(Route destination, std::optional<ExperienceScope> scope) {
    notificationsOpen = false;
    if (route != destination || detail || listing || profileId) searchQuery.clear();
    route = destination;
    detail.reset();
    listing.reset();
    profileId.reset();
    profileName.clear();
    history.clear();
    if (scope) experienceScope = *scope;
    experienceQuery.clear();
    inventoryQuery.clear();
    friendQuery.clear();
    notify();
}

void Store::perform(std::function<Async<void>()> action) {
    if (busy) return;
    std::uint64_t current = epoch;
    busy = true;
    notice.clear();
    notify();
    auto runner = [](std::shared_ptr<Store> self, std::function<Async<void>()> work, std::uint64_t token) -> Async<void> {
        std::exception_ptr failure;
        try {
            co_await work();
        } catch (...) {
            failure = std::current_exception();
        }
        bool expired = false;
        std::string message;
        if (failure && token == self->epoch) {
            try {
                std::rethrow_exception(failure);
            } catch (const Cancelled&) {
            } catch (const PlayFabRequestError& e) {
                expired = sessionExpired(e.code);
                message = readable(e.what());
            } catch (const std::exception& e) {
                message = readable(e.what());
            }
        }
        if (token == self->epoch) self->busy = false;
        if (expired) {
            self->logout();
            self->error = tr("La sessione è scaduta o non è più valida: accedi di nuovo.");
        } else if (!message.empty()) self->error = message;
        self->notify();
    };
    runner(shared_from_this(), std::move(action), current).start();
}

void Store::authenticate(std::string username, std::string password, bool registering, std::string gender) {
    auto self = shared_from_this();
    perform([self, username, password, registering, gender]() -> Async<void> {
        co_await self->api->authenticate(username, password, registering);
        std::exception_ptr failure;
        try {
            Json result = co_await self->api->cloud("LudisseyBootstrap", registering ? Json::object({{"gender", gender}}) : Json::object());
            if (result["needsProfile"].isBool() && result["needsProfile"].boolean()) {
                self->needsProfile = true;
                co_return;
            }
            self->profile = PlayerProfile::fromJson(result["profile"]);
            self->playerId = result["id"].stringOr("");
            co_await self->loadAvatar();
            co_await self->connectBackend();
            co_await self->refresh();
        } catch (...) {
            failure = std::current_exception();
        }
        if (!failure) co_return;
        if (registering) throw ServiceError(tr("Account creato. Accedi con le tue credenziali per completare l’avatar.") + " " + describe(failure));
        std::rethrow_exception(failure);
    });
}

void Store::finishProfile(std::string gender) {
    auto self = shared_from_this();
    perform([self, gender]() -> Async<void> {
        Json result = co_await self->api->cloud("LudisseyBootstrap", Json::object({{"gender", gender}}));
        self->profile = PlayerProfile::fromJson(result["profile"]);
        self->playerId = result["id"].stringOr("");
        self->needsProfile = false;
        co_await self->loadAvatar();
        co_await self->connectBackend();
        co_await self->refresh();
    });
}

void Store::logout() {
    dialog.reset();
    if (busy) return;
    try {
        api->vault->remove();
    } catch (const std::exception& e) {
        error = readable(e.what());
        notify();
        return;
    }
    *alive = false;
    alive = std::make_shared<bool>(true);
    ++notificationToken;
    avatarCategory = AvatarCategory{"hat"};
    avatarRefusal.reset();
    avatar.reset();
    avatarDraft.reset();
    preferences = PlayerPreferences();
    notifications.clear();
    notificationsOpen = false;
    metadata.clear();
    preferencesLoaded = false;
    dismissedNotifications.clear();
    readNotifications.clear();
    detail.reset();
    listing.reset();
    profileId.reset();
    profileName.clear();
    history.clear();
    details.clear();
    detailsLoading.clear();
    detailsError.clear();
    join->cancel();
    if (auto routed = std::dynamic_pointer_cast<RoutedBackend>(backend)) routed->reset();
    serverMode = BackendMode();
    ++epoch;
    profile.reset();
    playerId.clear();
    profiles.clear();
    profilesLoading.clear();
    profilesError.clear();
    profileLooks.clear();
    needsProfile = false;
    friends.clear();
    requests.clear();
    avatarCollection = AvatarCollection();
    ownWorlds.clear();
    publicWorlds.clear();
    rebuildCatalog();
    notice.clear();
    error.reset();
    route = ludissey_target::isStudio() ? Route::experiences : Route::home;
    experienceScope = ludissey_target::isStudio() ? ExperienceScope::mine : ExperienceScope::community;
    searchQuery.clear();
    experienceQuery.clear();
    inventoryQuery.clear();
    friendQuery.clear();
    api->clear();
    busy = false;
    notify();
}

Async<void> Store::refresh() {
    auto self = shared_from_this();
    if (serverMode.blocking()) co_return;
    std::uint64_t current = epoch;
    if (!preferencesLoaded) {
        co_await loadPlayerState();
        if (current != epoch) co_return;
        preferencesLoaded = true;
        startPresence();
    }
    co_await refreshFriends();
    if (current != epoch) co_return;
    co_await refreshWorlds();
    if (current != epoch) co_return;
    Json published = co_await backend->cloud("LudisseyDiscover", Json::object());
    if (current != epoch) co_return;
    setDiscover(summaries(published["worlds"]));
    Json data = co_await api->call("GetTitleData", Json::object({{"Keys", Json::array({"LudisseyExperienceMetadata"})}}));
    if (current != epoch) co_return;
    if (data["Data"]["LudisseyExperienceMetadata"].isString()) {
        std::map<std::string, ExperienceMetadata> loaded;
        if (auto parsed = Json::parse(data["Data"]["LudisseyExperienceMetadata"].string()); parsed && parsed->isObject())
            for (const auto& [id, value] : parsed->members()) loaded[id] = ExperienceMetadata::fromJson(value);
        setExperienceMetadata(loaded);
    }
    notify();
}

Async<void> Store::refreshWorlds() {
    auto self = shared_from_this();
    std::uint64_t current = epoch;
    Json own = co_await backend->cloud("LudisseyWorlds", Json::object());
    if (current != epoch) co_return;
    setWorlds(experiences(own["worlds"]));
    notify();
}

Async<void> Store::loadDetails(std::string id) {
    auto self = shared_from_this();
    std::uint64_t current = epoch;
    detailsLoading.insert(id);
    notify();
    std::optional<ExperienceDetails> loaded;
    std::string failure;
    try {
        Json response = co_await backend->cloud("LudisseyExperience", Json::object({{"id", id}}));
        auto parsed = ExperienceDetails::fromJson(response);
        if (!parsed) throw ServiceError(parsed.error().message);
        loaded = *parsed;
    } catch (const std::exception& e) {
        failure = messageCode(e.what()) == std::optional<std::string>("ExperienceUnavailable") ? tr("Questa esperienza non è disponibile in questo momento.") : tr("Non riusciamo a caricare i server. Riprova tra poco.");
    }
    if (current != epoch) co_return;
    detailsLoading.erase(id);
    if (loaded) {
        details[id] = *loaded;
        detailsError.erase(id);
    } else detailsError[id] = failure;
    notify();
}

Async<void> Store::refreshFriends() {
    auto self = shared_from_this();
    std::uint64_t current = epoch;
    Json result = co_await backend->cloud("LudisseyFriends", Json::object());
    std::vector<Friend> loaded;
    for (const auto& item : result["friends"].items()) {
        if (!item["FriendPlayFabId"].isString()) continue;
        std::string id = item["FriendPlayFabId"].string();
        std::string name = item["Username"].isString() ? item["Username"].string() : item["TitleDisplayName"].isString() ? item["TitleDisplayName"].string() : id;
        loaded.push_back({id, name, false, std::nullopt, "male"});
    }
    std::vector<FriendRequest> pending;
    for (const auto& item : result["requests"].items()) {
        if (!item["id"].isString() || !item["username"].isString()) throw ServiceError(tr("Richieste di amicizia non valide"));
        pending.push_back({item["id"].string(), item["username"].string(), item["at"].stringOr("")});
    }
    for (auto& f : loaded) {
        std::optional<Json> data;
        try {
            data = co_await api->call("GetUserData", Json::object({{"PlayFabId", f.id}, {"Keys", Json::array({"LudisseyPresence"})}}));
        } catch (const std::exception&) {
        }
        double stamp = 0;
        if (data && (*data)["Data"]["LudisseyPresence"]["Value"].isString()) stamp = parseNumber((*data)["Data"]["LudisseyPresence"]["Value"].string()).value_or(0);
        double age = wall() - stamp;
        f.online = age >= 0 && age < rules::account::presenceSeconds;
    }
    try {
        co_await loadPortraits(loaded);
    } catch (const std::exception&) {
    }
    if (current != epoch) throw Cancelled();
    friends = loaded;
    requests = pending;
    syncNotifications();
    notify();
}

Async<void> Store::sendFriendRequest(std::string username) {
    auto self = shared_from_this();
    std::string name = trim(username);
    std::size_t length = utf8Length(name);
    if (length < 3 || length > 20) throw ServiceError(tr("Nome utente: 3–20 caratteri."));
    Json info = co_await api->call("GetAccountInfo", Json::object({{"Username", name}}));
    if (!info["AccountInfo"]["PlayFabId"].isString()) throw ServiceError(tr("Giocatore non trovato"));
    co_await backend->cloud("LudisseyRequestFriend", Json::object({{"id", info["AccountInfo"]["PlayFabId"].string()}}));
}

void Store::requestFriend(std::string username) {
    auto self = shared_from_this();
    perform([self, username]() -> Async<void> {
        co_await self->sendFriendRequest(username);
        self->notice = tr("Richiesta inviata");
        co_await self->reloadProfile();
    });
}

void Store::requestFriendById(std::string id) {
    auto self = shared_from_this();
    perform([self, id]() -> Async<void> {
        co_await self->backend->cloud("LudisseyRequestFriend", Json::object({{"id", id}}));
        self->notice = tr("Richiesta inviata");
        co_await self->reloadProfile();
    });
}

void Store::respond(std::string id, bool accept) {
    auto self = shared_from_this();
    perform([self, id, accept]() -> Async<void> {
        co_await self->backend->cloud("LudisseyRespondFriend", Json::object({{"id", id}, {"accept", accept}}));
        co_await self->refreshFriends();
        co_await self->reloadProfile();
    });
}

void Store::removeFriend(std::string id) {
    auto self = shared_from_this();
    perform([self, id]() -> Async<void> {
        co_await self->backend->cloud("LudisseyRemoveFriend", Json::object({{"id", id}}));
        co_await self->refreshFriends();
        co_await self->reloadProfile();
    });
}

void Store::openProfile(std::string id, std::string username) {
    if (id.empty()) return;
    if (route == Route::profile && profileId == id) return;
    history.push_back(currentPage());
    route = Route::profile;
    detail.reset();
    listing.reset();
    profileId = id;
    profileName = username;
    notificationsOpen = false;
    notify();
    loadProfile(id).start();
}

Async<void> Store::reloadProfile() {
    if (route == Route::profile && profileId) co_await loadProfile(*profileId);
}

Async<void> Store::loadProfile(std::string id) {
    auto self = shared_from_this();
    std::uint64_t current = epoch;
    profilesLoading.insert(id);
    notify();
    std::optional<PlayerPage> loaded;
    std::string failure;
    try {
        Json response = co_await backend->cloud("LudisseyPlayerProfile", Json::object({{"id", id}}));
        loaded = PlayerPage::fromJson(response);
        if (!loaded) throw ServiceError(tr("Giocatore non trovato"));
    } catch (const std::exception& e) {
        failure = std::string(e.what()).find("Giocatore non trovato") != std::string::npos ? tr("Giocatore non trovato") : tr("Non riusciamo a caricare il profilo. Riprova tra poco.");
    }
    if (current != epoch) co_return;
    profilesLoading.erase(id);
    if (loaded && !loaded->self) {
        try {
            Json look = co_await backend->cloud("LudisseyPlayerAvatar", Json::object({{"id", id}}));
            if (auto parsed = AvatarConfiguration::fromJson(look["avatar"])) profileLooks[id] = parsed->validated(loaded->username, loaded->gender);
        } catch (const std::exception&) {
        }
        if (current != epoch) co_return;
    }
    if (loaded) {
        profiles[id] = *loaded;
        profilesError.erase(id);
        std::vector<Friend> people = loaded->friendList.value_or(std::vector<Friend>{});
        people.push_back({loaded->id, loaded->username, loaded->online, std::nullopt, loaded->gender});
        try {
            co_await loadPortraits(people);
        } catch (const std::exception&) {
        }
    } else profilesError[id] = failure;
    notify();
}

void Store::deleteWorld(std::string id) {
    if (!ludissey_target::isStudio()) return;
    auto self = shared_from_this();
    perform([self, id]() -> Async<void> {
        co_await self->backend->cloud("LudisseyDeleteWorld", Json::object({{"id", id}}));
        co_await self->refresh();
    });
}

void Store::unpublish(std::string id) {
    auto self = shared_from_this();
    perform([self, id]() -> Async<void> {
        co_await self->backend->cloud("LudisseyUnpublishWorld", Json::object({{"id", id}}));
        co_await self->refreshWorlds();
        Json published = co_await self->backend->cloud("LudisseyDiscover", Json::object());
        self->setDiscover(summaries(published["worlds"]));
        co_await self->loadDetails(id);
        self->notice = tr("Esperienza resa privata");
    });
}

Async<void> Store::sendPasswordEmail(std::string email) { co_await api->call("SendAccountRecoveryEmail", Json::object({{"TitleId", PlayFab::titleId}, {"Email", email}}), false); }

void Store::passwordEmail(std::string email) {
    auto self = shared_from_this();
    perform([self, email]() -> Async<void> {
        co_await self->sendPasswordEmail(email);
        self->notice = tr("Se l’email è giusta, tra poco ti arriva il link.");
    });
}

void Store::rebuildCatalog() {
    std::map<std::string, ExperienceEntry> entries;
    auto describeWorld = [&](const std::string& id, const std::string& author) {
        auto m = metadata.find(id);
        if (m != metadata.end() && m->second.description) return *m->second.description;
        return tr("Un’esperienza di {0}.", author);
    };
    for (const auto& world : publicWorlds) {
        auto meta = metadata.find(world.id);
        std::set<ExperienceScope> categories{ExperienceScope::all, ExperienceScope::community};
        if (meta != metadata.end() && meta->second.featured == std::optional<bool>(true)) categories.insert(ExperienceScope::featured);
        entries[world.id] = ExperienceEntry{world.id, world.title, describeWorld(world.id, world.author), world.author, world.genre.value_or(std::string(rules::defaultGenre)),
                                            world.serverSize.value_or(rules::serverSize::standard), world.createdAt, world.updatedAt, categories, true,
                                            meta != metadata.end() ? meta->second.weeklyPlays : std::nullopt, world.icon, world.thumbnail, world.owner};
    }
    for (const auto& world : ownWorlds) {
        auto meta = metadata.find(world.id);
        ExperienceEntry entry = entries.count(world.id) ? entries[world.id]
                                                        : ExperienceEntry{world.id, world.title, describeWorld(world.id, world.author), world.author, world.genre, world.serverSize, world.createdAt,
                                                                          std::optional<std::string>(world.updatedAt), {ExperienceScope::all}, false, meta != metadata.end() ? meta->second.weeklyPlays : std::nullopt, world.icon, world.thumbnail, world.owner};
        entry.categories.insert(ExperienceScope::mine);
        entry.owner = world.owner;
        entry.icon = world.icon;
        entry.thumbnail = world.thumbnail;
        if (!entry.published) {
            entry.title = world.title;
            entry.genre = world.genre;
            entry.serverSize = world.serverSize;
            entry.updatedAt = world.updatedAt;
        }
        entries[world.id] = entry;
    }
    catalog.clear();
    for (auto& [id, entry] : entries) catalog.push_back(entry);
    std::stable_sort(catalog.begin(), catalog.end(), [](const ExperienceEntry& a, const ExperienceEntry& b) { return localizedCompare(a.title, b.title) < 0; });
}

std::string Store::searchText() const { return trim(searchQuery); }

bool Store::matchesSearch(std::string_view name) const {
    std::string text = searchText();
    return text.empty() || localizedContains(name, text);
}

int Store::unreadNotifications() const {
    return static_cast<int>(std::count_if(notifications.begin(), notifications.end(), [](const PlayerNotification& n) { return n.unread; }));
}

std::vector<AvatarItem> Store::items(const AvatarCategory& category) const {
    std::vector<AvatarItem> out;
    for (const auto& item : avatarCollection.items(category))
        if (matchesSearch(item.name)) out.push_back(item);
    return out;
}

PageState Store::currentPage() const { return PageState{route, detail, listing, profileId, profileName}; }

std::optional<PageState> Store::previousPage() const {
    if (history.empty()) return std::nullopt;
    return history.back();
}

void Store::showExperience(const ExperienceEntry& entry) {
    history.push_back(currentPage());
    detail = entry;
    profileId.reset();
    profileName.clear();
    experienceTab = "Informazioni";
    notificationsOpen = false;
    notify();
}

void Store::showListing(Shelf kind) {
    history.push_back(currentPage());
    detail.reset();
    profileId.reset();
    profileName.clear();
    listing = kind;
    route = Route::experiences;
    notificationsOpen = false;
    notify();
}

void Store::goBack() {
    if (history.empty()) {
        navigate(route);
        return;
    }
    PageState target = history.back();
    history.pop_back();
    route = target.route;
    detail = target.detail;
    listing = target.listing;
    profileId = target.profileId;
    profileName = target.profileName;
    notificationsOpen = false;
    notify();
}

void Store::openWebsite() {
    if (openUrl) openUrl(std::string(rules::site::url));
}

Async<void> Store::follow(ExperienceLink link) {
    auto self = shared_from_this();
    if (!link.valid()) {
        error = tr("Link all’esperienza non valido.");
        notify();
        co_return;
    }
    co_await loadDetails(link.id.text());
    auto known = details.find(link.id.text());
    if (known == details.end()) {
        auto reason = detailsError.find(link.id.text());
        error = reason != detailsError.end() ? tr("Esperienza non disponibile: {0}", reason->second) : tr("Esperienza non disponibile.");
        notify();
        co_return;
    }
    const auto& facts = known->second.experience;
    auto listed = std::find_if(catalog.begin(), catalog.end(), [&](const ExperienceEntry& e) { return e.id == link.id.text(); });
    ExperienceEntry entry = listed != catalog.end() ? *listed
                                                     : ExperienceEntry{facts.id, facts.title, "", facts.author, facts.genre, facts.serverSize, facts.createdAt, facts.updatedAt, {ExperienceScope::all}, facts.published, std::nullopt, std::nullopt, std::nullopt, facts.owner};
    navigate(Route::experiences);
    showExperience(entry);
    if (link.play) play(entry, link.server);
}

void Store::play(const ExperienceEntry& entry, std::optional<ServerId> server) { join->play(entry, std::move(server)); }

void Store::openStudio(std::optional<std::string> project) {
    if (!launchStudio) {
        error = tr("Non trovo Ludissey Studio. Mettilo nella cartella Applicazioni, accanto a Ludissey.");
        notify();
        return;
    }
    launchStudio(std::move(project));
}

void Store::confirmLogout() {
    if (busy) return;
    std::weak_ptr<Store> weak = shared_from_this();
    dialog = DialogRequest{tr("Esci dall’account?"), tr("Dovrai accedere di nuovo."),
                           {DialogButton{tr("Annulla"), DialogRole::secondary, [] {}}, DialogButton{tr("Esci"), DialogRole::destructive, [weak] {
                                                                                                 if (auto s = weak.lock()) s->logout();
                                                                                             }}}};
    notify();
}

Async<void> Store::loadPlayerState() {
    auto self = shared_from_this();
    std::uint64_t current = epoch;
    Json response = co_await backend->client("GetUserData", Json::object({{"Keys", Json::array({"LudisseyPreferences", "LudisseyDismissedNotifications", "LudisseyReadNotifications", "LudisseyPublicProfile"})}}));
    if (current != epoch) co_return;
    const Json& values = response["Data"];
    auto decoded = [&](const char* key) -> std::optional<Json> {
        if (!values[key]["Value"].isString()) return std::nullopt;
        auto parsed = Json::parse(values[key]["Value"].string());
        if (!parsed) return std::nullopt;
        return *parsed;
    };
    auto prefs = decoded("LudisseyPreferences");
    preferences = prefs ? PlayerPreferences::fromJson(*prefs) : PlayerPreferences();
    preferences.publicProfile = values.contains("LudisseyPublicProfile");
    auto names = [](const std::optional<Json>& list) {
        std::set<std::string> out;
        if (list && list->isArray())
            for (const auto& i : list->items())
                if (i.isString()) out.insert(i.string());
        return out;
    };
    dismissedNotifications = names(decoded("LudisseyDismissedNotifications"));
    readNotifications = names(decoded("LudisseyReadNotifications"));
}

Async<void> Store::loadAvatar() {
    if (!profile) co_return;
    auto self = shared_from_this();
    std::string account = profile->username;
    std::string body = profile->gender;
    auto cached = avatarCache.load(account);
    AvatarConfiguration current = cached ? cached->validated(account, body) : AvatarConfiguration::standard(account, body);
    avatar = current;
    avatarDraft = current;
    notify();
    std::optional<AvatarLoad> remote;
    try {
        remote = co_await avatars->load();
    } catch (const std::exception&) {
    }
    if (!remote || !profile || profile->username != account) co_return;
    avatarCollection = remote->collection;
    avatar_catalog::setItems(remote->collection.index());
    auto owned = remote->collection.owned();
    auto index = remote->collection.index();
    AvatarConfiguration checked = remote->avatar.value_or(current).validated(account, body, &owned, &index);
    if (avatarDraft == avatar) avatarDraft = checked;
    avatar = checked;
    try {
        avatarCache.save(checked, account);
    } catch (const std::exception&) {
    }
    notify();
}

void Store::saveAvatar() {
    if (!profile || !avatarDraft) return;
    auto owned = avatarCollection.owned();
    auto index = avatarCollection.index();
    AvatarConfiguration checked = avatarDraft->validated(profile->username, profile->gender, &owned, &index);
    auto self = shared_from_this();
    std::string account = profile->username;
    perform([self, checked, account]() -> Async<void> {
        self->avatarCache.save(checked, account);
        self->avatar = checked;
        self->avatarDraft = checked;
        std::optional<AvatarConfiguration> stored;
        std::string failure;
        try {
            stored = co_await self->avatars->save(checked);
        } catch (const std::exception& e) {
            failure = readable(e.what());
        }
        if (!stored) {
            self->notice = tr("Avatar salvato su questo Mac; sincronizzazione con l’account non riuscita: {0}", failure);
            co_return;
        }
        self->avatar = stored;
        self->avatarDraft = stored;
        self->avatarCache.save(*stored, account);
        self->notice = tr("Avatar salvato!");
        try {
            co_await self->publishPortraits(*stored);
        } catch (const std::exception&) {
        }
    });
}

Async<void> Store::publishPortraits(AvatarConfiguration look) {
    auto self = shared_from_this();
    if (!renderPortraits || !profile || !backend) co_return;
    auto images = renderPortraits(look);
    if (!images || images->bust.empty() || images->figure.empty()) co_return;
    std::string bust = asset_cache::identifier(images->bust, "png"), figure = asset_cache::identifier(images->figure, "png");
    asset_cache::store(images->bust, bust);
    asset_cache::store(images->figure, figure);
    Experience carrier;
    carrier.thumbnail = bust;
    carrier.icon = figure;
    co_await AssetService(backend).upload(carrier);
    co_await backend->cloud("LudisseyPortraits", Json::object({{"save", Json::object({{"bust", bust}, {"figure", figure}})}}));
    if (profile) portraits[profile->username] = bust;
    notify();
}

Async<void> Store::loadPortraits(std::vector<Friend>& people) {
    auto self = shared_from_this();
    if (people.empty() || !backend) co_return;
    Json ids = Json::array();
    for (const auto& f : people) ids.push(f.id);
    Json response = co_await backend->cloud("LudisseyPortraits", Json::object({{"players", ids}}));
    auto channel = backend->storage();
    for (auto& f : people) {
        const Json& entry = response["players"][f.id];
        f.gender = entry["gender"] == Json("female") ? "female" : "male";
        std::string asset = entry["bust"].stringOr(""), url = entry["bustUrl"].stringOr("");
        if (!isImageAsset(asset)) continue;
        if (!asset_cache::verified(asset)) {
            if (!channel || url.empty()) continue;
            HttpRequest request;
            request.url = url;
            HttpResponse reply = co_await background([channel, request] { return channel->perform(request); });
            if (!reply.success() || !asset_cache::matches(reply.body, asset)) continue;
            asset_cache::store(reply.body, asset);
        }
        f.portrait = asset;
        portraits[f.username] = asset;
    }
}

void Store::toggle(const AvatarItem& item) {
    auto base = avatarDraft ? avatarDraft : avatar;
    if (!base) return;
    AvatarConfiguration draft = *base;
    avatarRefusal.reset();
    if (draft.wearing(item.id)) draft.unequip(item.id);
    else {
        switch (draft.equip(item, avatarCollection.owned())) {
        case AvatarRefusal::none: break;
        case AvatarRefusal::categoryFull: avatarRefusal = tr("Puoi indossare al massimo {0} oggetti della categoria {1}.", item.category.limit(), item.category.title()); break;
        case AvatarRefusal::accessoriesFull: avatarRefusal = tr("Puoi indossare al massimo {0} accessori.", rules::avatar::limits::accessories); break;
        case AvatarRefusal::notOwned: avatarRefusal = tr("Non possiedi questo oggetto."); break;
        }
    }
    avatarDraft = draft;
    notify();
}

void Store::acquire(const AvatarItem& item) {
    auto self = shared_from_this();
    perform([self, item]() -> Async<void> {
        self->avatarCollection = co_await self->avatars->acquire(item.id);
        self->notice = tr("«{0}» aggiunto all’inventario", item.name);
    });
}

void Store::revertAvatar() {
    avatarDraft = avatar;
    notify();
}

std::optional<AvatarConfiguration> Store::currentAvatar() const {
    if (avatar) return avatar;
    if (profile) return AvatarConfiguration::standard(profile->username, profile->gender);
    return std::nullopt;
}

void Store::setPreference(bool PlayerPreferences::*key, bool value) {
    if (busy) return;
    PlayerPreferences next = preferences;
    next.*key = value;
    savePreferences(next);
}

void Store::setBio(std::string value) {
    if (busy) return;
    PlayerPreferences next = preferences;
    next.bio = utf8Prefix(value, bioCharacters);
    if (next == preferences) return;
    savePreferences(next);
}

void Store::savePreferences(PlayerPreferences next) {
    auto self = shared_from_this();
    perform([self, next]() -> Async<void> { co_await self->persistPreferences(next); });
}

Async<void> Store::persistPreferences(PlayerPreferences next) {
    auto self = shared_from_this();
    std::uint64_t current = epoch;
    std::string raw = next.json().dump();
    if (next.publicProfile && profile) {
        co_await backend->client("UpdateUserData", Json::object({{"Data", Json::object({{"LudisseyPublicProfile", profile->json().dump()}})}, {"Permission", "Public"}}));
    } else {
        co_await backend->client("UpdateUserData", Json::object({{"KeysToRemove", Json::array({"LudisseyPublicProfile"})}}));
    }
    co_await backend->client("UpdateUserData", Json::object({{"Data", Json::object({{"LudisseyPreferences", raw}})}, {"Permission", "Private"}}));
    if (epoch != current) throw Cancelled();
    preferences = next;
    syncNotifications();
    notify();
}

void Store::syncNotifications() {
    std::set<std::string> active;
    for (const auto& r : requests) active.insert(r.id);
    for (auto it = dismissedNotifications.begin(); it != dismissedNotifications.end();) it = active.count(*it) ? std::next(it) : dismissedNotifications.erase(it);
    for (auto it = readNotifications.begin(); it != readNotifications.end();) it = active.count(*it) ? std::next(it) : readNotifications.erase(it);
    notifications.clear();
    if (!preferences.friendNotifications) return;
    for (const auto& r : requests)
        if (!dismissedNotifications.count(r.id)) notifications.push_back({r.id, std::string(localization::localizable("Richiesta di amicizia")), wording(localization::localizable("{0} vuole aggiungerti agli amici."), r.username), !readNotifications.count(r.id), r.at});
}

void Store::dismissNotification(std::string id) {
    dismissedNotifications.insert(id);
    syncNotifications();
    persistNotifications();
    notify();
}

void Store::markNotificationsRead() {
    for (const auto& n : notifications) readNotifications.insert(n.id);
    syncNotifications();
    persistNotifications();
    notify();
}

void Store::persistNotifications() {
    std::vector<std::string> dismissed(dismissedNotifications.begin(), dismissedNotifications.end()), read(readNotifications.begin(), readNotifications.end());
    std::uint64_t token = ++notificationToken;
    auto saving = [](std::shared_ptr<Store> self, std::vector<std::string> dismissed, std::vector<std::string> read, std::uint64_t token) -> Async<void> {
        co_await sleepFor(0.25);
        if (token != self->notificationToken) co_return;
        Json a = Json::array(), b = Json::array();
        for (const auto& d : dismissed) a.push(d);
        for (const auto& r : read) b.push(r);
        try {
            co_await self->backend->client("UpdateUserData", Json::object({{"Data", Json::object({{"LudisseyDismissedNotifications", a.dump()}, {"LudisseyReadNotifications", b.dump()}})}, {"Permission", "Private"}}));
        } catch (const std::exception&) {
            if (self->profile) self->notice = tr("Notifiche aggiornate sul dispositivo; sincronizzazione non riuscita.");
        }
    };
    saving(shared_from_this(), dismissed, read, token).start();
}

void Store::startPresence() {
    *alive = false;
    alive = std::make_shared<bool>(true);
    auto loop = [](std::weak_ptr<Store> weak, std::shared_ptr<bool> token) -> Async<void> {
        if (auto self = weak.lock()) co_await self->updatePresence();
        while (*token) {
            co_await sleepFor(45);
            auto self = weak.lock();
            if (!self || !*token) co_return;
            if (self->profile) co_await self->updatePresence();
        }
    };
    loop(weak_from_this(), alive).start();
}

Async<void> Store::updatePresence() {
    auto self = shared_from_this();
    try {
        co_await backend->client("UpdateUserData", Json::object({{"Data", Json::object({{"LudisseyPresence", doubleText(wall())}})}, {"Permission", "Public"}}));
        if (!busy) co_await refreshFriends();
    } catch (const std::exception&) {
    }
}

}
