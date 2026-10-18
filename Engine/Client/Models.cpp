#include "Models.hpp"

#include "Localization/Localization.hpp"

#include <CoreFoundation/CoreFoundation.h>

namespace ld {

std::string_view groupTitle(NavigationGroup group) {
    switch (group) {
    case NavigationGroup::content: return "Contenuto";
    case NavigationGroup::utilities: return "Utilità";
    case NavigationGroup::identity: return "Identità";
    }
    return "";
}

std::string_view routeName(Route route) {
    switch (route) {
    case Route::home: return "home";
    case Route::experiences: return "experiences";
    case Route::avatar: return "avatar";
    case Route::friends: return "friends";
    case Route::settings: return "settings";
    case Route::profile: return "profile";
    }
    return "";
}

std::string routeTitle(Route route) {
    switch (route) {
    case Route::home: return tr("Home");
    case Route::experiences: return tr("Esperienze");
    case Route::avatar: return tr("Avatar");
    case Route::friends: return tr("Amici");
    case Route::settings: return tr("Impostazioni");
    case Route::profile: return tr("Profilo");
    }
    return "";
}

NavigationGroup routeGroup(Route route) {
    switch (route) {
    case Route::home:
    case Route::experiences:
    case Route::avatar: return NavigationGroup::content;
    case Route::friends: return NavigationGroup::utilities;
    case Route::settings:
    case Route::profile: return NavigationGroup::identity;
    }
    return NavigationGroup::content;
}

const std::vector<Route>& allRoutes() {
    static const std::vector<Route> list = {Route::home, Route::experiences, Route::avatar, Route::friends, Route::settings};
    return list;
}

std::vector<Route> routes(NavigationGroup group) {
    std::vector<Route> out;
    for (auto r : allRoutes())
        if (routeGroup(r) == group) out.push_back(r);
    return out;
}

ExperienceMetadata ExperienceMetadata::fromJson(const Json& value) {
    ExperienceMetadata m;
    if (value["description"].isString()) m.description = value["description"].string();
    if (value["featured"].isBool()) m.featured = value["featured"].boolean();
    if (value["weeklyPlays"].isNumber()) m.weeklyPlays = static_cast<int>(value["weeklyPlays"].integer());
    return m;
}

Json PlayerPreferences::json() const { return Json::object({{"sounds", sounds}, {"friendNotifications", friendNotifications}, {"publicProfile", publicProfile}, {"bio", bio}}); }

PlayerPreferences PlayerPreferences::fromJson(const Json& value) {
    PlayerPreferences p;
    if (!value.isObject() || !value["sounds"].isBool() || !value["friendNotifications"].isBool() || !value["publicProfile"].isBool() || !value["bio"].isString()) return p;
    p.sounds = value["sounds"].boolean();
    p.friendNotifications = value["friendNotifications"].boolean();
    p.publicProfile = value["publicProfile"].boolean();
    p.bio = value["bio"].string();
    return p;
}

namespace {

struct CFText {
    CFStringRef value;
    explicit CFText(std::string_view text) : value(CFStringCreateWithBytes(nullptr, reinterpret_cast<const UInt8*>(text.data()), static_cast<CFIndex>(text.size()), kCFStringEncodingUTF8, false)) {}
    ~CFText() {
        if (value) CFRelease(value);
    }
};

CFLocaleRef currentLocale() {
    static CFLocaleRef locale = CFLocaleCopyCurrent();
    return locale;
}

}

int localizedCompare(std::string_view a, std::string_view b) {
    CFText left(a), right(b);
    if (!left.value || !right.value) return a.compare(b);
    return static_cast<int>(CFStringCompareWithOptionsAndLocale(left.value, right.value, CFRangeMake(0, CFStringGetLength(left.value)), kCFCompareCaseInsensitive | kCFCompareLocalized, currentLocale()));
}

bool localizedContains(std::string_view text, std::string_view query) {
    if (query.empty()) return true;
    CFText haystack(text), needle(query);
    if (!haystack.value || !needle.value) return false;
    CFRange found;
    return CFStringFindWithOptionsAndLocale(haystack.value, needle.value, CFRangeMake(0, CFStringGetLength(haystack.value)), kCFCompareCaseInsensitive, currentLocale(), &found);
}

std::optional<PlayerPage> PlayerPage::fromJson(const Json& value) {
    if (!value["id"].isString() || !value["username"].isString()) return std::nullopt;
    PlayerPage page;
    page.id = value["id"].string();
    page.username = value["username"].string();
    page.gender = value["gender"] == Json("female") ? "female" : "male";
    if (value["createdAt"].isString()) page.createdAt = value["createdAt"].string();
    page.self = value["self"] == Json(true);
    page.isFriend = value["friend"] == Json(true);
    page.requested = value["requested"] == Json(true);
    page.requesting = value["requesting"] == Json(true);
    page.publicProfile = value["publicProfile"] == Json(true);
    page.online = value["online"] == Json(true);
    page.friends = static_cast<int>(value["friends"].number());
    if (value["bio"].isString()) page.bio = value["bio"].string();
    if (value["friendList"].isArray()) {
        std::vector<Friend> list;
        for (const auto& item : value["friendList"].items())
            if (item["id"].isString() && item["username"].isString()) list.push_back({item["id"].string(), item["username"].string(), false, std::nullopt, item["gender"] == Json("female") ? "female" : "male"});
        page.friendList = list;
    }
    for (const auto& item : value["experiences"].items()) page.experiences.push_back(WorldSummary::fromJson(item));
    return page;
}

}
