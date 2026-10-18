#pragma once

#include "Engine/Core/Json.hpp"
#include "Engine/Experience/Experience.hpp"

#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace ld {

enum class NavigationGroup { content, utilities, identity };
enum class Route { home, experiences, avatar, friends, settings, profile };
enum class ExperienceScope { featured, all, mine, community };

std::string_view groupTitle(NavigationGroup group);
std::string_view routeName(Route route);
std::string routeTitle(Route route);
NavigationGroup routeGroup(Route route);
const std::vector<Route>& allRoutes();
std::vector<Route> routes(NavigationGroup group);

struct ExperienceMetadata {
    std::optional<std::string> description;
    std::optional<bool> featured;
    std::optional<int> weeklyPlays;
    static ExperienceMetadata fromJson(const Json& value);
};

struct ExperienceEntry {
    std::string id;
    std::string title;
    std::string description;
    std::string author;
    std::string genre;
    int serverSize = 0;
    std::optional<std::string> createdAt;
    std::optional<std::string> updatedAt;
    std::set<ExperienceScope> categories;
    bool published = false;
    std::optional<int> weeklyPlays;
    std::optional<std::string> icon;
    std::optional<std::string> thumbnail;
    std::string owner;
    bool operator==(const ExperienceEntry&) const = default;
};

struct Friend {
    std::string id;
    std::string username;
    bool online = false;
    std::optional<std::string> portrait;
    std::string gender = "male";
    bool operator==(const Friend&) const = default;
};

struct PlayerPage {
    std::string id;
    std::string username;
    std::string gender = "male";
    std::optional<std::string> createdAt;
    bool self = false;
    bool isFriend = false;
    bool requested = false;
    bool requesting = false;
    bool publicProfile = false;
    bool online = false;
    int friends = 0;
    std::optional<std::string> bio;
    std::optional<std::vector<Friend>> friendList;
    std::vector<WorldSummary> experiences;
    bool hidden() const { return !bio.has_value(); }
    static std::optional<PlayerPage> fromJson(const Json& value);
};

struct FriendRequest {
    std::string id;
    std::string username;
    std::string at;
    bool operator==(const FriendRequest&) const = default;
};

struct PlayerPreferences {
    bool sounds = true;
    bool friendNotifications = true;
    bool publicProfile = false;
    std::string bio;
    bool operator==(const PlayerPreferences&) const = default;
    Json json() const;
    static PlayerPreferences fromJson(const Json& value);
};

struct PlayerNotification {
    std::string id;
    std::string title;
    std::string body;
    bool unread = true;
    std::string at;
    bool operator==(const PlayerNotification&) const = default;
};

enum class DialogRole { primary, secondary, destructive };

struct DialogButton {
    std::string label;
    DialogRole role = DialogRole::primary;
    std::function<void()> action;
};

struct DialogRequest {
    std::string title;
    std::string message;
    std::vector<DialogButton> buttons;
    bool warning = false;
};

int localizedCompare(std::string_view a, std::string_view b);
bool localizedContains(std::string_view text, std::string_view query);

}
