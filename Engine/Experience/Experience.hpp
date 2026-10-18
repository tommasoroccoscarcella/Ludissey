#pragma once

#include "Engine/Core/Json.hpp"
#include "Engine/Core/Math.hpp"
#include "Engine/Core/Result.hpp"
#include "Engine/Core/Rules.hpp"
#include "Engine/Terrain/Terrain.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace ld {

std::string genreTitle(std::string_view genre);

struct PropertyValue {
    enum class Kind { string, number, boolean, vector3, vector2, color3, udim, udim2, enumItem, reference, content };
    Kind kind = Kind::string;
    std::string text;
    std::array<double, 4> numbers{0, 0, 0, 0};
    bool flag = false;
    bool present = true;

    static PropertyValue string(std::string value) { PropertyValue v; v.kind = Kind::string; v.text = std::move(value); return v; }
    static PropertyValue number(double value) { PropertyValue v; v.kind = Kind::number; v.numbers[0] = value; return v; }
    static PropertyValue boolean(bool value) { PropertyValue v; v.kind = Kind::boolean; v.flag = value; return v; }
    static PropertyValue vector3(double x, double y, double z) { PropertyValue v; v.kind = Kind::vector3; v.numbers = {x, y, z, 0}; return v; }
    static PropertyValue vector2(double x, double y) { PropertyValue v; v.kind = Kind::vector2; v.numbers = {x, y, 0, 0}; return v; }
    static PropertyValue color3(std::string hex) { PropertyValue v; v.kind = Kind::color3; v.text = std::move(hex); return v; }
    static PropertyValue udim(double scale, double offset) { PropertyValue v; v.kind = Kind::udim; v.numbers = {scale, offset, 0, 0}; return v; }
    static PropertyValue udim2(double xs, double xo, double ys, double yo) { PropertyValue v; v.kind = Kind::udim2; v.numbers = {xs, xo, ys, yo}; return v; }
    static PropertyValue enumItem(std::string item) { PropertyValue v; v.kind = Kind::enumItem; v.text = std::move(item); return v; }
    static PropertyValue reference(std::optional<std::string> id) { PropertyValue v; v.kind = Kind::reference; v.present = id.has_value(); v.text = id.value_or(""); return v; }
    static PropertyValue content(std::string value) { PropertyValue v; v.kind = Kind::content; v.text = std::move(value); return v; }

    bool operator==(const PropertyValue& other) const;
    std::string typeName() const;
    std::string display() const;
    Json json() const;
    static Result<PropertyValue> fromJson(const Json& value);
    std::optional<std::string> referenceId() const { return kind == Kind::reference && present ? std::optional<std::string>(text) : std::nullopt; }
};

using PropertyMap = std::map<std::string, PropertyValue>;

Json propertiesJson(const PropertyMap& properties);
Result<PropertyMap> propertiesFromJson(const Json& value);

struct WorldBlock {
    std::string id;
    std::string kind = "cube";
    std::string name;
    std::string parent = "Workspace";
    double x = 0, y = 0.5, z = 0;
    double rotationX = 0, rotationY = 0, rotationZ = 0;
    double sizeX = 1, sizeY = 1, sizeZ = 1;
    std::string color = std::string(rules::part::color);
    std::string material = "plastic";
    double transparency = 0;
    double reflectance = 0;
    bool anchored = true;
    bool canCollide = true;
    bool locked = false;
    std::optional<std::string> meshAsset;
    PropertyMap properties;

    WorldBlock();
    bool operator==(const WorldBlock&) const = default;
    double size() const { return std::max(sizeX, std::max(sizeY, sizeZ)); }
    void setSize(double value) { sizeX = sizeY = sizeZ = value; }
    Vec3 position() const { return {x, y, z}; }
    Mat3 rotationMatrix() const;
    Vec3 halfExtents() const;
    std::string className() const;
    std::string kindTitle() const;
    std::string kindSymbol() const;
    std::string displayName() const;
    Json json() const;
    static Result<WorldBlock> fromJson(const Json& value);
};

struct InstanceRecord {
    std::string id;
    std::string className;
    std::string name;
    std::string parent;
    PropertyMap properties;

    bool operator==(const InstanceRecord&) const = default;
    std::string source() const;
    void setSource(std::string text);
    bool disabled() const;
    Json json() const;
    static Result<InstanceRecord> fromJson(const Json& value);
};

struct WorldLighting {
    double clockTime = rules::lighting::clockTime::standard;
    double brightness = rules::lighting::brightness::standard;
    double fogEnd = rules::lighting::fogEnd::standard;
    std::string ambient{rules::lighting::ambient};
    std::string skyTop{rules::lighting::skyTop};
    std::string skyHorizon{rules::lighting::skyHorizon};
    bool operator==(const WorldLighting&) const = default;
    double daylight() const { return std::max(0.0, std::sin((clockTime - 6) / 12 * pi)); }
    Json json() const;
    static WorldLighting fromJson(const Json& value);
};

struct Experience {
    std::string id;
    int schemaVersion = rules::schemaVersion;
    std::string languageVersion{rules::lim::languageVersion};
    int engineApiVersion = rules::lim::engineApiVersion;
    std::string title = "La mia esperienza";
    std::vector<WorldBlock> blocks;
    std::string owner;
    std::string author;
    std::string genre{rules::defaultGenre};
    int serverSize = rules::serverSize::standard;
    std::optional<std::string> createdAt;
    std::string updatedAt;
    int revision = 0;
    WorldLighting lighting;
    Terrain terrain;
    std::vector<InstanceRecord> instances;
    std::optional<std::string> icon;
    std::optional<std::string> thumbnail;

    Experience();
    bool operator==(const Experience& other) const;
    static Experience blank();
    std::optional<std::string> compatibility() const;
    std::vector<std::string> legacyScripts() const;
    static WorldBlock baseplate(const std::string& experience);
    Experience copy(const std::string& title) const;
    Json json() const;
    static Result<Experience> fromJson(const Json& value);
    static Result<Experience> parse(std::string_view text);

    const WorldBlock* block(std::string_view id) const;
    WorldBlock* block(std::string_view id);
    const InstanceRecord* record(std::string_view id) const;
    InstanceRecord* record(std::string_view id);
    std::optional<std::string> parentOf(std::string_view id) const;
    bool contains(std::string_view id) const { return block(id) || record(id); }
    std::string root(const WorldBlock& block) const;
    std::vector<std::string> childIds(std::string_view parent) const;
    std::vector<std::string> descendants(std::string_view id) const;
    std::vector<WorldBlock> workspaceParts() const;
};

bool isImageAsset(std::string_view asset);

struct WorldSummary {
    std::string id, title, owner, author;
    std::optional<std::string> genre;
    std::optional<int> serverSize;
    std::optional<std::string> createdAt, updatedAt;
    std::optional<std::string> icon, thumbnail;
    static WorldSummary fromJson(const Json& value);
};

enum class ServerLifecycle { provisioning, ready, joinable, full, draining, terminated };
std::string_view lifecycleName(ServerLifecycle state);
std::optional<ServerLifecycle> lifecycleNamed(std::string_view name);

struct ExperienceServer {
    std::string id;
    int sequence = 0, capacity = 0, players = 0, reserved = 0;
    ServerLifecycle state = ServerLifecycle::joinable;
    std::string region = "global";
    std::optional<std::string> observedAt;
    bool full() const { return players + reserved >= capacity; }
    bool operator==(const ExperienceServer&) const = default;
    static Result<ExperienceServer> fromJson(const Json& value);
};

struct ExperienceFacts {
    std::string id, title, owner, author, genre;
    int serverSize = 0;
    std::optional<std::string> createdAt, updatedAt;
    int revision = 0;
    bool published = false;
    int visits = 0;
    bool operator==(const ExperienceFacts&) const = default;
    static ExperienceFacts fromJson(const Json& value);
};

struct ExperienceDetails {
    ExperienceFacts experience;
    std::vector<ExperienceServer> servers;
    int players() const;
    bool operator==(const ExperienceDetails&) const = default;
    static Result<ExperienceDetails> fromJson(const Json& value);
};

std::string kindTitle(std::string_view kind);
std::string kindSymbol(std::string_view kind);

}
