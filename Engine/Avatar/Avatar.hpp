#pragma once

#include "Engine/Core/Json.hpp"
#include "Engine/Core/Result.hpp"
#include "Engine/Core/Rules.hpp"

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace ld {

enum class AvatarGroup { clothing, body, animations };

std::string_view groupName(AvatarGroup group);
std::string_view groupTitle(AvatarGroup group);

struct AvatarCategory {
    std::string name;
    static std::vector<AvatarCategory> all();
    static std::optional<AvatarCategory> named(std::string_view name);
    std::string title() const;
    AvatarGroup group() const;
    bool accessory() const;
    int limit() const;
    bool operator==(const AvatarCategory&) const = default;
    bool operator<(const AvatarCategory& other) const { return name < other.name; }
};

std::vector<AvatarCategory> categoriesIn(AvatarGroup group);

enum class BodyPart { head, torso, leftArm, rightArm, leftLeg, rightLeg };

std::string_view bodyPartName(BodyPart part);
std::string bodyPartTitle(BodyPart part);
std::string_view bodyPartStandard(BodyPart part);
std::vector<BodyPart> allBodyParts();
std::optional<BodyPart> bodyPartOfMesh(std::string_view mesh);

struct AvatarItem {
    std::string id;
    std::string name;
    AvatarCategory category;
    std::string creator;
    std::optional<std::string> asset;
    std::optional<std::string> description;
    std::optional<bool> free;
    Json json() const;
    static Result<AvatarItem> fromJson(const Json& value);
};

struct InventoryEntry {
    std::string id;
    std::string itemID;
    std::string acquiredAt;
    Json json() const;
    static Result<InventoryEntry> fromJson(const Json& value);
};

struct EquippedAvatarItem {
    std::string itemID;
    AvatarCategory category;
    bool operator==(const EquippedAvatarItem&) const = default;
};

enum class AvatarRefusal { none, notOwned, categoryFull, accessoriesFull };

struct AvatarConfiguration {
    std::string displayName;
    std::string bodyPreset{rules::avatar::standard::bodyPreset};
    std::map<std::string, std::string> bodyColors;
    std::vector<EquippedAvatarItem> equipped;

    bool operator==(const AvatarConfiguration&) const = default;
    static AvatarConfiguration standard(const std::string& name, const std::string& body);
    std::string color(BodyPart part) const;
    std::vector<std::string> items(const AvatarCategory& category) const;
    bool wearing(std::string_view item) const;
    AvatarRefusal equip(const AvatarItem& item, const std::set<std::string>& owned);
    void unequip(std::string_view item);
    AvatarConfiguration validated(const std::string& name, const std::string& body, const std::set<std::string>* owned = nullptr, const std::map<std::string, AvatarItem>* catalog = nullptr) const;
    Json json() const;
    static Result<AvatarConfiguration> fromJson(const Json& value);
    std::size_t signature() const;
};

namespace avatar_catalog {
std::map<std::string, AvatarItem> items();
void setItems(std::map<std::string, AvatarItem> items);
}

struct AvatarCollection {
    std::vector<AvatarItem> catalog;
    std::vector<InventoryEntry> inventory;
    std::set<std::string> owned() const;
    std::map<std::string, AvatarItem> index() const;
    std::vector<AvatarItem> items(const AvatarCategory& category) const;
    static Result<AvatarCollection> fromResponse(const Json& response);
};

}
