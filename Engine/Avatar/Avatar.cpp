#include "Avatar.hpp"

#include "Engine/Core/Text.hpp"
#include "Localization/Localization.hpp"

#include <algorithm>
#include <functional>
#include <mutex>

namespace ld {

namespace {

template <std::size_t N>
bool listed(const std::array<std::string_view, N>& list, std::string_view value) {
    return std::find(list.begin(), list.end(), value) != list.end();
}

}

std::string_view groupName(AvatarGroup group) {
    switch (group) {
    case AvatarGroup::clothing: return "clothing";
    case AvatarGroup::body: return "body";
    case AvatarGroup::animations: return "animations";
    }
    return "clothing";
}

std::string_view groupTitle(AvatarGroup group) {
    switch (group) {
    case AvatarGroup::clothing: return "Abbigliamento";
    case AvatarGroup::body: return "Corpo";
    case AvatarGroup::animations: return "Animazioni";
    }
    return "Abbigliamento";
}

std::vector<AvatarCategory> AvatarCategory::all() {
    std::vector<AvatarCategory> out;
    for (auto name : rules::avatar::categories) out.push_back({std::string(name)});
    return out;
}

std::optional<AvatarCategory> AvatarCategory::named(std::string_view name) {
    if (!listed(rules::avatar::categories, name)) return std::nullopt;
    return AvatarCategory{std::string(name)};
}

std::string AvatarCategory::title() const {
    for (std::size_t i = 0; i < rules::avatar::categories.size(); ++i)
        if (rules::avatar::categories[i] == name) return localization::translate(rules::avatar::categoryTitles[i]);
    return name;
}

AvatarGroup AvatarCategory::group() const {
    if (listed(rules::avatar::groups::body, name)) return AvatarGroup::body;
    if (listed(rules::avatar::groups::animations, name)) return AvatarGroup::animations;
    return AvatarGroup::clothing;
}

bool AvatarCategory::accessory() const { return listed(rules::avatar::accessories, name); }
int AvatarCategory::limit() const { return name == "hat" ? rules::avatar::limits::hats : accessory() ? rules::avatar::limits::accessories : 1; }

std::vector<AvatarCategory> categoriesIn(AvatarGroup group) {
    std::vector<AvatarCategory> out;
    for (auto& category : AvatarCategory::all())
        if (category.group() == group) out.push_back(category);
    return out;
}

std::string_view bodyPartName(BodyPart part) { return rules::avatar::bodyParts[static_cast<std::size_t>(part)]; }
std::string bodyPartTitle(BodyPart part) { return localization::translate(rules::avatar::bodyPartTitles[static_cast<std::size_t>(part)]); }

std::string_view bodyPartStandard(BodyPart part) {
    using namespace rules::avatar::standard::bodyColors;
    switch (part) {
    case BodyPart::head: return head;
    case BodyPart::torso: return torso;
    case BodyPart::leftArm: return leftArm;
    case BodyPart::rightArm: return rightArm;
    case BodyPart::leftLeg: return leftLeg;
    case BodyPart::rightLeg: return rightLeg;
    }
    return head;
}

std::vector<BodyPart> allBodyParts() { return {BodyPart::head, BodyPart::torso, BodyPart::leftArm, BodyPart::rightArm, BodyPart::leftLeg, BodyPart::rightLeg}; }

std::optional<BodyPart> bodyPartOfMesh(std::string_view mesh) {
    if (startsWith(mesh, "Head_")) return BodyPart::head;
    if (startsWith(mesh, "Body_")) return BodyPart::torso;
    if (startsWith(mesh, "Left_Arm_")) return BodyPart::leftArm;
    if (startsWith(mesh, "Right_Arm_")) return BodyPart::rightArm;
    if (startsWith(mesh, "Left_Leg_")) return BodyPart::leftLeg;
    if (startsWith(mesh, "Right_Leg_")) return BodyPart::rightLeg;
    return std::nullopt;
}

Json AvatarItem::json() const {
    Json out = Json::object({{"id", id}, {"name", name}, {"category", category.name}, {"creator", creator}});
    if (asset) out.set("asset", *asset);
    if (description) out.set("description", *description);
    if (free) out.set("free", *free);
    return out;
}

Result<AvatarItem> AvatarItem::fromJson(const Json& value) {
    AvatarItem item;
    if (!value["id"].isString() || !value["name"].isString() || !value["creator"].isString()) return fail(ErrorCode::corrupted, "Oggetto avatar non valido");
    auto category = AvatarCategory::named(value["category"].string());
    if (!category) return fail(ErrorCode::corrupted, "Categoria avatar sconosciuta");
    item.id = value["id"].string();
    item.name = value["name"].string();
    item.category = *category;
    item.creator = value["creator"].string();
    item.asset = value.optionalString("asset");
    item.description = value.optionalString("description");
    item.free = value.optionalBool("free");
    return item;
}

Json InventoryEntry::json() const { return Json::object({{"id", id}, {"itemID", itemID}, {"acquiredAt", acquiredAt}}); }

Result<InventoryEntry> InventoryEntry::fromJson(const Json& value) {
    if (!value["id"].isString() || !value["itemID"].isString() || !value["acquiredAt"].isString()) return fail(ErrorCode::corrupted, "Voce di inventario non valida");
    return InventoryEntry{value["id"].string(), value["itemID"].string(), value["acquiredAt"].string()};
}

AvatarConfiguration AvatarConfiguration::standard(const std::string& name, const std::string& body) {
    AvatarConfiguration config;
    config.displayName = name;
    config.bodyPreset = listed(rules::avatar::bodyPresets, body) ? body : std::string(rules::avatar::standard::bodyPreset);
    for (auto part : allBodyParts()) config.bodyColors[std::string(bodyPartName(part))] = std::string(bodyPartStandard(part));
    return config;
}

std::string AvatarConfiguration::color(BodyPart part) const {
    auto it = bodyColors.find(std::string(bodyPartName(part)));
    return it != bodyColors.end() ? it->second : std::string(bodyPartStandard(part));
}

std::vector<std::string> AvatarConfiguration::items(const AvatarCategory& category) const {
    std::vector<std::string> out;
    for (const auto& e : equipped)
        if (e.category == category) out.push_back(e.itemID);
    return out;
}

bool AvatarConfiguration::wearing(std::string_view item) const {
    return std::any_of(equipped.begin(), equipped.end(), [&](const EquippedAvatarItem& e) { return e.itemID == item; });
}

AvatarRefusal AvatarConfiguration::equip(const AvatarItem& item, const std::set<std::string>& owned) {
    if (!owned.count(item.id)) return AvatarRefusal::notOwned;
    if (wearing(item.id)) return AvatarRefusal::none;
    auto same = items(item.category);
    if (item.category.limit() == 1) equipped.erase(std::remove_if(equipped.begin(), equipped.end(), [&](const EquippedAvatarItem& e) { return e.category == item.category; }), equipped.end());
    else if (static_cast<int>(same.size()) >= item.category.limit()) return AvatarRefusal::categoryFull;
    if (item.category.accessory() && std::count_if(equipped.begin(), equipped.end(), [](const EquippedAvatarItem& e) { return e.category.accessory(); }) >= rules::avatar::limits::accessories)
        return AvatarRefusal::accessoriesFull;
    equipped.push_back({item.id, item.category});
    return AvatarRefusal::none;
}

void AvatarConfiguration::unequip(std::string_view item) {
    equipped.erase(std::remove_if(equipped.begin(), equipped.end(), [&](const EquippedAvatarItem& e) { return e.itemID == item; }), equipped.end());
}

AvatarConfiguration AvatarConfiguration::validated(const std::string& name, const std::string& body, const std::set<std::string>* owned, const std::map<std::string, AvatarItem>* catalog) const {
    AvatarConfiguration result = standard(name, body);
    std::string shown = trim(displayName);
    std::size_t length = utf8Length(shown);
    if (length >= 3 && length <= static_cast<std::size_t>(rules::avatar::nameCharacters)) result.displayName = shown;
    if (listed(rules::avatar::bodyPresets, bodyPreset)) result.bodyPreset = bodyPreset;
    for (auto part : allBodyParts()) {
        std::string c = color(part);
        if (listed(rules::avatar::palette, c)) result.bodyColors[std::string(bodyPartName(part))] = c;
    }
    for (const auto& entry : equipped) {
        if (owned && !owned->count(entry.itemID)) continue;
        if (catalog) {
            auto found = catalog->find(entry.itemID);
            if (found == catalog->end() || !(found->second.category == entry.category)) continue;
        }
        AvatarItem item;
        item.id = entry.itemID;
        item.category = entry.category;
        result.equip(item, {entry.itemID});
    }
    return result;
}

Json AvatarConfiguration::json() const {
    Json colors = Json::object();
    for (const auto& [part, hex] : bodyColors) colors.set(part, hex);
    Json list = Json::array();
    for (const auto& e : equipped) list.push(Json::object({{"itemID", e.itemID}, {"category", e.category.name}}));
    return Json::object({{"displayName", displayName}, {"bodyPreset", bodyPreset}, {"bodyColors", colors}, {"equipped", list}});
}

Result<AvatarConfiguration> AvatarConfiguration::fromJson(const Json& value) {
    if (!value.isObject()) return fail(ErrorCode::corrupted, "Avatar non valido");
    AvatarConfiguration config;
    config.displayName = value["displayName"].stringOr("");
    config.bodyPreset = value["bodyPreset"].stringOr(std::string(rules::avatar::standard::bodyPreset));
    if (const Json* list = value.find("equipped"); list && !list->isNull()) {
        if (!list->isArray()) return fail(ErrorCode::corrupted, "Avatar non valido");
        for (const auto& item : list->items()) {
            auto category = AvatarCategory::named(item["category"].string());
            if (!item["itemID"].isString() || !category) return fail(ErrorCode::corrupted, "Oggetto indossato non valido");
            config.equipped.push_back({item["itemID"].string(), *category});
        }
    }
    if (const Json* colors = value.find("bodyColors"); colors && colors->isObject()) {
        for (const auto& [part, hex] : colors->members())
            if (hex.isString()) config.bodyColors[part] = hex.string();
    } else {
        std::string skin = value["skinTone"].stringOr(std::string(bodyPartStandard(BodyPart::head)));
        std::string shirt = value["shirtColor"].stringOr(std::string(bodyPartStandard(BodyPart::torso)));
        std::string pants = value["pantsColor"].stringOr(std::string(bodyPartStandard(BodyPart::leftLeg)));
        config.bodyColors = {{"head", skin}, {"leftArm", skin}, {"rightArm", skin}, {"torso", shirt}, {"leftLeg", pants}, {"rightLeg", pants}};
    }
    return config;
}

std::size_t AvatarConfiguration::signature() const { return std::hash<std::string>{}(json().dump(true)); }

std::set<std::string> AvatarCollection::owned() const {
    std::set<std::string> out;
    for (const auto& entry : inventory) out.insert(entry.itemID);
    return out;
}

std::map<std::string, AvatarItem> AvatarCollection::index() const {
    std::map<std::string, AvatarItem> out;
    for (const auto& item : catalog) out[item.id] = item;
    return out;
}

std::vector<AvatarItem> AvatarCollection::items(const AvatarCategory& category) const {
    auto known = index();
    std::vector<AvatarItem> out;
    for (const auto& entry : inventory) {
        auto it = known.find(entry.itemID);
        if (it != known.end() && it->second.category == category) out.push_back(it->second);
    }
    return out;
}

Result<AvatarCollection> AvatarCollection::fromResponse(const Json& response) {
    AvatarCollection collection;
    for (const auto& item : response["catalog"].items()) {
        auto parsed = AvatarItem::fromJson(item);
        if (!parsed) return parsed.error();
        collection.catalog.push_back(*parsed);
    }
    for (const auto& item : response["inventory"].items()) {
        auto parsed = InventoryEntry::fromJson(item);
        if (!parsed) return parsed.error();
        collection.inventory.push_back(*parsed);
    }
    return collection;
}

namespace avatar_catalog {

namespace {

std::mutex& lock() {
    static std::mutex m;
    return m;
}

std::map<std::string, AvatarItem>& table() {
    static std::map<std::string, AvatarItem> t;
    return t;
}

}

std::map<std::string, AvatarItem> items() {
    std::lock_guard<std::mutex> guard(lock());
    return table();
}

void setItems(std::map<std::string, AvatarItem> value) {
    std::lock_guard<std::mutex> guard(lock());
    table() = std::move(value);
}

}

}
