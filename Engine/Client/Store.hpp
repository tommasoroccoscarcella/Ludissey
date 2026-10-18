#pragma once

#include "Engine/Avatar/Avatar.hpp"
#include "Engine/Backend/Assets.hpp"
#include "Engine/Backend/Backend.hpp"
#include "Engine/Client/JoinCoordinator.hpp"
#include "Engine/Client/Models.hpp"
#include "Engine/Experience/Experience.hpp"

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace ld {

inline constexpr int bioCharacters = rules::account::bioCharacters;

struct PortraitImages {
    std::string bust;
    std::string figure;
};

enum class Shelf { featured, popular, recent, community, mine };

struct PageState {
    Route route = Route::home;
    std::optional<ExperienceEntry> detail;
    std::optional<Shelf> listing;
    std::optional<std::string> profileId;
    std::string profileName;
};

class Store : public std::enable_shared_from_this<Store> {
public:
    Store(std::shared_ptr<PlayFab> api, std::shared_ptr<Backend> backend = nullptr, std::shared_ptr<AvatarRepository> avatars = nullptr, bool restoreSession = true);
    ~Store();

    bool restoringSession;
    std::optional<PlayerProfile> profile;
    bool needsProfile = false;
    bool busy = false;
    std::optional<std::string> error;
    std::optional<DialogRequest> dialog;
    std::string notice;
    std::vector<Friend> friends;
    std::map<std::string, std::string> portraits;
    std::function<std::optional<PortraitImages>(const AvatarConfiguration&)> renderPortraits;
    std::vector<FriendRequest> requests;
    AvatarCollection avatarCollection;
    std::optional<ExperienceEntry> detail;
    std::optional<Shelf> listing;
    std::optional<std::string> profileId;
    std::string profileName;
    std::vector<PageState> history;
    std::map<std::string, PlayerPage> profiles;
    std::set<std::string> profilesLoading;
    std::map<std::string, std::string> profilesError;
    std::map<std::string, AvatarConfiguration> profileLooks;
    std::string playerId;
    std::map<std::string, ExperienceDetails> details;
    std::set<std::string> detailsLoading;
    std::map<std::string, std::string> detailsError;
    std::string experienceTab = "Informazioni";
    Route route = Route::home;
    ExperienceScope experienceScope = ExperienceScope::community;
    std::string searchQuery;
    std::string experienceQuery;
    std::string inventoryQuery;
    std::string friendQuery;
    AvatarCategory avatarCategory{"hat"};
    std::optional<std::string> avatarRefusal;
    std::optional<AvatarConfiguration> avatar;
    std::optional<AvatarConfiguration> avatarDraft;
    PlayerPreferences preferences;
    std::vector<PlayerNotification> notifications;
    bool notificationsOpen = false;
    std::set<std::string> dismissedNotifications;
    std::set<std::string> readNotifications;
    bool preferencesLoaded = false;
    BackendMode serverMode;

    const std::shared_ptr<PlayFab> api;
    const std::shared_ptr<Backend> backend;
    const std::shared_ptr<JoinCoordinator> join;
    const std::shared_ptr<AvatarRepository> avatars;
    AvatarCache avatarCache;
    std::function<void()> changed;
    std::function<void(const std::string&)> openUrl;
    std::function<void(std::optional<std::string>)> launchStudio;

    const std::vector<Experience>& worlds() const { return ownWorlds; }
    void setWorlds(std::vector<Experience> value);
    const std::vector<WorldSummary>& discover() const { return publicWorlds; }
    void setDiscover(std::vector<WorldSummary> value);
    const std::map<std::string, ExperienceMetadata>& experienceMetadata() const { return metadata; }
    void setExperienceMetadata(std::map<std::string, ExperienceMetadata> value);
    const std::vector<ExperienceEntry>& experienceCatalog() const { return catalog; }

    Async<void> connectBackend();
    void reconnect();
    void openDownloads();
    std::function<void()> quit;
    Async<void> restoreSession();
    void navigate(Route destination, std::optional<ExperienceScope> scope = std::nullopt);
    void perform(std::function<Async<void>()> action);
    void authenticate(std::string username, std::string password, bool registering, std::string gender);
    void finishProfile(std::string gender);
    void logout();
    Async<void> refresh();
    Async<void> refreshWorlds();
    Async<void> loadDetails(std::string id);
    Async<void> refreshFriends();
    Async<void> publishPortraits(AvatarConfiguration look);
    Async<void> loadPortraits(std::vector<Friend>& people);
    Async<void> sendFriendRequest(std::string username);
    void requestFriend(std::string username);
    void respond(std::string id, bool accept);
    void removeFriend(std::string id);
    void deleteWorld(std::string id);
    void unpublish(std::string id);
    Async<void> sendPasswordEmail(std::string email);
    void passwordEmail(std::string email);
    void rebuildCatalog();
    std::string searchText() const;
    bool matchesSearch(std::string_view name) const;
    int unreadNotifications() const;
    std::vector<AvatarItem> items(const AvatarCategory& category) const;
    void showExperience(const ExperienceEntry& entry);
    void openProfile(std::string id, std::string username);
    void requestFriendById(std::string id);
    Async<void> loadProfile(std::string id);
    Async<void> reloadProfile();
    void showListing(Shelf kind);
    void goBack();
    PageState currentPage() const;
    std::optional<PageState> previousPage() const;
    void openWebsite();
    Async<void> follow(ExperienceLink link);
    void play(const ExperienceEntry& entry, std::optional<ServerId> server = std::nullopt);
    void openStudio(std::optional<std::string> project = std::nullopt);
    void confirmLogout();
    Async<void> loadPlayerState();
    Async<void> loadAvatar();
    void saveAvatar();
    void toggle(const AvatarItem& item);
    void acquire(const AvatarItem& item);
    void revertAvatar();
    std::optional<AvatarConfiguration> currentAvatar() const;
    void setPreference(bool PlayerPreferences::*key, bool value);
    void setBio(std::string value);
    void savePreferences(PlayerPreferences next);
    Async<void> persistPreferences(PlayerPreferences next);
    void syncNotifications();
    void dismissNotification(std::string id);
    void markNotificationsRead();
    void persistNotifications();
    void startPresence();
    Async<void> updatePresence();

private:
    std::vector<Experience> ownWorlds;
    std::vector<WorldSummary> publicWorlds;
    std::map<std::string, ExperienceMetadata> metadata;
    std::vector<ExperienceEntry> catalog;
    bool didRestoreSession;
    std::uint64_t epoch = 1;
    std::uint64_t notificationToken = 0;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    void notify();
};

}
