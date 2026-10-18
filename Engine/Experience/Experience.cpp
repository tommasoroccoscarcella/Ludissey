#include "Experience.hpp"

#include "Engine/Core/Rotation.hpp"
#include "Engine/Core/Text.hpp"
#include "Localization/Localization.hpp"

#include <algorithm>
#include <deque>
#include <set>

namespace ld {

std::string genreTitle(std::string_view genre) { return localization::translate(genre); }

namespace {

Error corrupt(const std::string& what) { return fail(ErrorCode::corrupted, tr("Dati non validi: {0}", what)); }

Result<double> requireNumber(const Json& object, std::string_view key) {
    const Json* value = object.find(key);
    if (!value || !value->isNumber()) return corrupt(std::string(key));
    return value->number();
}

Result<std::string> requireString(const Json& object, std::string_view key) {
    const Json* value = object.find(key);
    if (!value || !value->isString()) return corrupt(std::string(key));
    return value->string();
}

Result<std::optional<double>> optionalNumber(const Json& object, std::string_view key) {
    const Json* value = object.find(key);
    if (!value || value->isNull()) return std::optional<double>();
    if (!value->isNumber()) return corrupt(std::string(key));
    return std::optional<double>(value->number());
}

Result<std::optional<std::string>> optionalString(const Json& object, std::string_view key) {
    const Json* value = object.find(key);
    if (!value || value->isNull()) return std::optional<std::string>();
    if (!value->isString()) return corrupt(std::string(key));
    return std::optional<std::string>(value->string());
}

Result<std::optional<bool>> optionalBool(const Json& object, std::string_view key) {
    const Json* value = object.find(key);
    if (!value || value->isNull()) return std::optional<bool>();
    if (!value->isBool()) return corrupt(std::string(key));
    return std::optional<bool>(value->boolean());
}

Result<std::optional<int>> optionalInt(const Json& object, std::string_view key) {
    const Json* value = object.find(key);
    if (!value || value->isNull()) return std::optional<int>();
    if (!value->isNumber() || std::floor(value->number()) != value->number() || std::abs(value->number()) > 2147483647.0) return corrupt(std::string(key));
    return std::optional<int>(static_cast<int>(value->number()));
}

std::string formatDisplay(double value) {
    if (value == std::round(value) && std::abs(value) < 1e9) return std::to_string(static_cast<long long>(value));
    return formatGeneral(value);
}

}

bool PropertyValue::operator==(const PropertyValue& o) const {
    if (kind != o.kind) return false;
    switch (kind) {
    case Kind::string:
    case Kind::color3:
    case Kind::enumItem:
    case Kind::content: return text == o.text;
    case Kind::number: return numbers[0] == o.numbers[0];
    case Kind::boolean: return flag == o.flag;
    case Kind::vector3: return numbers[0] == o.numbers[0] && numbers[1] == o.numbers[1] && numbers[2] == o.numbers[2];
    case Kind::vector2:
    case Kind::udim: return numbers[0] == o.numbers[0] && numbers[1] == o.numbers[1];
    case Kind::udim2: return numbers == o.numbers;
    case Kind::reference: return present == o.present && (!present || text == o.text);
    }
    return false;
}

std::string PropertyValue::typeName() const {
    switch (kind) {
    case Kind::string: return "string";
    case Kind::number: return "number";
    case Kind::boolean: return "bool";
    case Kind::vector3: return "Vector3";
    case Kind::vector2: return "Vector2";
    case Kind::color3: return "Color3";
    case Kind::udim: return "UDim";
    case Kind::udim2: return "UDim2";
    case Kind::enumItem: return "Enum";
    case Kind::reference: return "Instance";
    case Kind::content: return "Content";
    }
    return "string";
}

std::string PropertyValue::display() const {
    switch (kind) {
    case Kind::string:
    case Kind::content:
    case Kind::enumItem:
    case Kind::color3: return text;
    case Kind::number: return formatDisplay(numbers[0]);
    case Kind::boolean: return flag ? "true" : "false";
    case Kind::vector3: return formatDisplay(numbers[0]) + ", " + formatDisplay(numbers[1]) + ", " + formatDisplay(numbers[2]);
    case Kind::vector2: return formatDisplay(numbers[0]) + ", " + formatDisplay(numbers[1]);
    case Kind::udim: return "{" + formatDisplay(numbers[0]) + ", " + formatDisplay(numbers[1]) + "}";
    case Kind::udim2: return "{" + formatDisplay(numbers[0]) + ", " + formatDisplay(numbers[1]) + "},{" + formatDisplay(numbers[2]) + ", " + formatDisplay(numbers[3]) + "}";
    case Kind::reference: return present ? text : "";
    }
    return "";
}

Json PropertyValue::json() const {
    switch (kind) {
    case Kind::boolean: return Json(flag);
    case Kind::number: return Json(numbers[0]);
    case Kind::string: return Json::object({{"String", text}});
    case Kind::vector3: return Json::object({{"Vector3", Json::array({numbers[0], numbers[1], numbers[2]})}});
    case Kind::vector2: return Json::object({{"Vector2", Json::array({numbers[0], numbers[1]})}});
    case Kind::color3: return Json::object({{"Color3", text}});
    case Kind::udim: return Json::object({{"UDim", Json::array({numbers[0], numbers[1]})}});
    case Kind::udim2: return Json::object({{"UDim2", Json::array({numbers[0], numbers[1], numbers[2], numbers[3]})}});
    case Kind::enumItem: return Json::object({{"Enum", text}});
    case Kind::reference: return Json::object({{"Ref", present ? Json(text) : Json(nullptr)}});
    case Kind::content: return Json::object({{"Content", text}});
    }
    return Json();
}

Result<PropertyValue> PropertyValue::fromJson(const Json& value) {
    if (value.isBool()) return boolean(value.boolean());
    if (value.isNumber()) return number(value.number());
    if (!value.isObject()) return corrupt(tr("proprietà"));
    auto numbers = [&](std::string_view key, std::size_t count) -> Result<std::optional<std::vector<double>>> {
        const Json* list = value.find(key);
        if (!list) return std::optional<std::vector<double>>();
        if (!list->isArray() || list->size() != count) return corrupt(tr("proprietà {0}", key));
        std::vector<double> out;
        for (const auto& item : list->items()) {
            if (!item.isNumber() || !std::isfinite(item.number())) return corrupt(tr("proprietà {0}", key));
            out.push_back(item.number());
        }
        return std::optional<std::vector<double>>(out);
    };
    if (const Json* text = value.find("String"); text && !text->isNull()) {
        if (!text->isString()) return corrupt("String");
        return string(text->string());
    }
    if (auto list = numbers("Vector3", 3); !list) return list.error();
    else if (*list) return vector3((**list)[0], (**list)[1], (**list)[2]);
    if (auto list = numbers("Vector2", 2); !list) return list.error();
    else if (*list) return vector2((**list)[0], (**list)[1]);
    if (const Json* hex = value.find("Color3"); hex && !hex->isNull()) {
        if (!hex->isString()) return corrupt("Color3");
        return color3(uppercase(hex->string()));
    }
    if (auto list = numbers("UDim", 2); !list) return list.error();
    else if (*list) return udim((**list)[0], (**list)[1]);
    if (auto list = numbers("UDim2", 4); !list) return list.error();
    else if (*list) return udim2((**list)[0], (**list)[1], (**list)[2], (**list)[3]);
    if (const Json* item = value.find("Enum"); item && !item->isNull()) {
        if (!item->isString()) return corrupt("Enum");
        return enumItem(item->string());
    }
    if (const Json* ref = value.find("Ref")) {
        if (ref->isNull()) return reference(std::nullopt);
        if (!ref->isString()) return corrupt("Ref");
        return reference(ref->string());
    }
    if (const Json* c = value.find("Content"); c && !c->isNull()) {
        if (!c->isString()) return corrupt("Content");
        return content(c->string());
    }
    return corrupt(tr("tipo di proprietà sconosciuto"));
}

Json propertiesJson(const PropertyMap& properties) {
    Json out = Json::object();
    for (const auto& [key, value] : properties) out.set(key, value.json());
    return out;
}

Result<PropertyMap> propertiesFromJson(const Json& value) {
    if (!value.isObject()) return corrupt("properties");
    PropertyMap out;
    for (const auto& [key, item] : value.members()) {
        auto parsed = PropertyValue::fromJson(item);
        if (!parsed) return parsed.error();
        out[key] = *parsed;
    }
    return out;
}

WorldBlock::WorldBlock() : id(makeUuid()) {}

Mat3 WorldBlock::rotationMatrix() const { return rotation::matrix(rotationX, rotationY, rotationZ); }

Vec3 WorldBlock::halfExtents() const {
    double half[3] = {sizeX / 2, sizeY / 2, sizeZ / 2};
    Mat3 m = rotationMatrix();
    auto extent = [&](int row) { return std::abs(m[row][0]) * half[0] + std::abs(m[row][1]) * half[1] + std::abs(m[row][2]) * half[2]; };
    return {extent(0), extent(1), extent(2)};
}

std::string WorldBlock::className() const {
    if (kind == "cube" || kind == "sphere" || kind == "cylinder") return "Part";
    if (kind == "wedge") return "WedgePart";
    if (kind == "mesh") return "MeshPart";
    if (kind == "model") return "Model";
    if (kind == "spawn") return "SpawnLocation";
    if (kind == "seat") return "Seat";
    return "Part";
}

std::string kindTitle(std::string_view kind) {
    if (kind == "model") return "Modello";
    if (kind == "sphere") return "Sfera";
    if (kind == "cylinder") return "Cilindro";
    if (kind == "mesh") return "Mesh";
    if (kind == "wedge") return "Cuneo";
    if (kind == "spawn") return "Punto di partenza";
    if (kind == "seat") return "Sedile";
    return "Cubo";
}

std::string kindSymbol(std::string_view kind) {
    if (kind == "sphere") return "ld-icon-object-sphere";
    if (kind == "cylinder") return "ld-icon-object-cylinder";
    if (kind == "wedge") return "ld-icon-object-wedge";
    if (kind == "mesh") return "ld-icon-object-mesh";
    if (kind == "model") return "ld-icon-object-model";
    return "ld-icon-object-part";
}

std::string WorldBlock::kindTitle() const { return ld::kindTitle(kind); }
std::string WorldBlock::kindSymbol() const { return ld::kindSymbol(kind); }
std::string WorldBlock::displayName() const { return name.empty() ? kindTitle() : name; }

Json WorldBlock::json() const {
    WorldBlock base;
    Json out = Json::object();
    out.set("id", id);
    out.set("kind", kind);
    out.set("color", color);
    out.set("x", x);
    out.set("y", y);
    out.set("z", z);
    double s = size();
    out.set("size", s);
    if (sizeX != s) out.set("sizeX", sizeX);
    if (sizeY != s) out.set("sizeY", sizeY);
    if (sizeZ != s) out.set("sizeZ", sizeZ);
    if (rotationX != base.rotationX) out.set("rotationX", rotationX);
    if (rotationY != base.rotationY) out.set("rotationY", rotationY);
    if (rotationZ != base.rotationZ) out.set("rotationZ", rotationZ);
    if (!name.empty()) out.set("name", name);
    if (parent != base.parent) out.set("parent", parent);
    if (material != base.material) out.set("material", material);
    if (transparency != base.transparency) out.set("transparency", transparency);
    if (reflectance != base.reflectance) out.set("reflectance", reflectance);
    if (anchored != base.anchored) out.set("anchored", anchored);
    if (canCollide != base.canCollide) out.set("canCollide", canCollide);
    if (locked != base.locked) out.set("locked", locked);
    if (meshAsset) out.set("meshAsset", *meshAsset);
    if (!properties.empty()) out.set("properties", propertiesJson(properties));
    return out;
}

Result<WorldBlock> WorldBlock::fromJson(const Json& value) {
    if (!value.isObject()) return corrupt("blocco");
    WorldBlock b;
    auto id = requireString(value, "id");
    if (!id) return id.error();
    b.id = *id;
    auto kind = requireString(value, "kind");
    if (!kind) return kind.error();
    b.kind = *kind;
    auto color = requireString(value, "color");
    if (!color) return color.error();
    b.color = *color;
    for (auto [key, target] : {std::pair{"x", &b.x}, std::pair{"y", &b.y}, std::pair{"z", &b.z}}) {
        auto n = requireNumber(value, key);
        if (!n) return n.error();
        *target = *n;
    }
    auto legacy = optionalNumber(value, "size");
    if (!legacy) return legacy.error();
    double fallback = legacy->value_or(1);
    for (auto [key, target, standard] : {std::tuple{"sizeX", &b.sizeX, fallback}, std::tuple{"sizeY", &b.sizeY, fallback}, std::tuple{"sizeZ", &b.sizeZ, fallback},
                                         std::tuple{"rotationX", &b.rotationX, 0.0}, std::tuple{"rotationY", &b.rotationY, 0.0}, std::tuple{"rotationZ", &b.rotationZ, 0.0},
                                         std::tuple{"transparency", &b.transparency, 0.0}, std::tuple{"reflectance", &b.reflectance, 0.0}}) {
        auto n = optionalNumber(value, key);
        if (!n) return n.error();
        *target = n->value_or(standard);
    }
    for (auto [key, target, standard] : {std::tuple{"name", &b.name, std::string()}, std::tuple{"parent", &b.parent, std::string("Workspace")}, std::tuple{"material", &b.material, std::string("plastic")}}) {
        auto s = optionalString(value, key);
        if (!s) return s.error();
        *target = s->value_or(standard);
    }
    for (auto [key, target, standard] : {std::tuple{"anchored", &b.anchored, true}, std::tuple{"canCollide", &b.canCollide, true}, std::tuple{"locked", &b.locked, false}}) {
        auto f = optionalBool(value, key);
        if (!f) return f.error();
        *target = f->value_or(standard);
    }
    auto mesh = optionalString(value, "meshAsset");
    if (!mesh) return mesh.error();
    b.meshAsset = *mesh;
    if (const Json* properties = value.find("properties"); properties && !properties->isNull()) {
        auto parsed = propertiesFromJson(*properties);
        if (!parsed) return parsed.error();
        b.properties = std::move(*parsed);
    }
    return b;
}

std::string InstanceRecord::source() const {
    auto it = properties.find("Source");
    return it != properties.end() && it->second.kind == PropertyValue::Kind::string ? it->second.text : std::string();
}

void InstanceRecord::setSource(std::string text) { properties["Source"] = PropertyValue::string(std::move(text)); }

bool InstanceRecord::disabled() const {
    auto it = properties.find("Disabled");
    return it != properties.end() && it->second.kind == PropertyValue::Kind::boolean && it->second.flag;
}

Json InstanceRecord::json() const {
    return Json::object({{"id", id}, {"className", className}, {"name", name}, {"parent", parent}, {"properties", propertiesJson(properties)}});
}

Result<InstanceRecord> InstanceRecord::fromJson(const Json& value) {
    if (!value.isObject()) return corrupt("istanza");
    InstanceRecord r;
    for (auto [key, target] : {std::pair{"id", &r.id}, std::pair{"className", &r.className}, std::pair{"name", &r.name}, std::pair{"parent", &r.parent}}) {
        auto s = requireString(value, key);
        if (!s) return s.error();
        *target = *s;
    }
    const Json* properties = value.find("properties");
    if (!properties) return corrupt("properties");
    auto parsed = propertiesFromJson(*properties);
    if (!parsed) return parsed.error();
    r.properties = std::move(*parsed);
    return r;
}

Json WorldLighting::json() const {
    return Json::object({{"clockTime", clockTime}, {"brightness", brightness}, {"fogEnd", fogEnd}, {"ambient", ambient}, {"skyTop", skyTop}, {"skyHorizon", skyHorizon}});
}

WorldLighting WorldLighting::fromJson(const Json& value) {
    WorldLighting l;
    l.clockTime = value["clockTime"].number(l.clockTime);
    l.brightness = value["brightness"].number(l.brightness);
    l.fogEnd = value["fogEnd"].number(l.fogEnd);
    l.ambient = value["ambient"].stringOr(l.ambient);
    l.skyTop = value["skyTop"].stringOr(l.skyTop);
    l.skyHorizon = value["skyHorizon"].stringOr(l.skyHorizon);
    return l;
}

Experience::Experience() : id(makeUuid()) {}

bool Experience::operator==(const Experience& o) const {
    return id == o.id && schemaVersion == o.schemaVersion && languageVersion == o.languageVersion && engineApiVersion == o.engineApiVersion && title == o.title && blocks == o.blocks && owner == o.owner && author == o.author && genre == o.genre &&
           serverSize == o.serverSize && createdAt == o.createdAt && updatedAt == o.updatedAt && revision == o.revision && lighting == o.lighting && terrain == o.terrain &&
           instances == o.instances && icon == o.icon && thumbnail == o.thumbnail;
}

bool isImageAsset(std::string_view asset) {
    auto dot = asset.find('.');
    if (dot != 64 || asset.size() > 70) return false;
    for (std::size_t i = 0; i < 64; ++i)
        if (!((asset[i] >= '0' && asset[i] <= '9') || (asset[i] >= 'a' && asset[i] <= 'f'))) return false;
    auto extension = asset.substr(65);
    return std::find(rules::assets::imageExtensions.begin(), rules::assets::imageExtensions.end(), extension) != rules::assets::imageExtensions.end();
}

std::optional<std::string> Experience::compatibility() const {
    bool known = std::find(rules::lim::languageVersions.begin(), rules::lim::languageVersions.end(), languageVersion) != rules::lim::languageVersions.end();
    if (!known) return tr("L’esperienza usa Lim {0}, non supportato da questa versione di Ludissey (Lim {1}). Aggiorna Ludissey.", languageVersion, rules::lim::languageVersion);
    if (engineApiVersion < 1 || engineApiVersion > rules::lim::engineApiVersion)
        return tr("L’esperienza richiede l’API del motore {0}, questa versione di Ludissey supporta fino alla {1}. Aggiorna Ludissey.", static_cast<long long>(engineApiVersion), static_cast<long long>(rules::lim::engineApiVersion));
    return std::nullopt;
}

WorldBlock Experience::baseplate(const std::string& experience) {
    WorldBlock plate;
    std::string compact = replaceAll(experience, "-", "");
    plate.id = "00000000-0000-4000-8000-" + (compact.size() > 12 ? compact.substr(compact.size() - 12) : compact);
    plate.kind = "cube";
    plate.name = std::string(rules::baseplate::name);
    plate.color = std::string(rules::part::color);
    plate.sizeX = rules::baseplate::width;
    plate.sizeY = rules::baseplate::height;
    plate.sizeZ = rules::baseplate::width;
    plate.y = -static_cast<double>(rules::baseplate::height) / 2;
    plate.locked = rules::baseplate::locked;
    return plate;
}

Experience Experience::copy(const std::string& name) const {
    Experience out = *this;
    out.id = Experience().id;
    out.title = utf8Prefix(trim(name).empty() ? title : trim(name), rules::world::titleCharacters);
    out.owner.clear();
    out.author.clear();
    out.createdAt.reset();
    out.updatedAt.clear();
    out.revision = 0;
    std::string previous = baseplate(id).id, next = baseplate(out.id).id;
    auto remap = [&](std::string& value) {
        if (value == previous) value = next;
    };
    auto references = [&](PropertyMap& properties) {
        for (auto& [name, value] : properties)
            if (value.referenceId() == std::optional<std::string>(previous)) value.text = next;
    };
    for (auto& block : out.blocks) {
        remap(block.id);
        remap(block.parent);
        references(block.properties);
    }
    for (auto& record : out.instances) {
        remap(record.parent);
        references(record.properties);
    }
    return out;
}

Experience Experience::blank() {
    Experience e;
    e.title = tr("La mia esperienza");
    e.blocks = {baseplate(e.id)};
    return e;
}

Json Experience::json() const {
    Json out = Json::object();
    out.set("id", id);
    out.set("schemaVersion", schemaVersion);
    out.set("languageVersion", languageVersion);
    out.set("engineApiVersion", engineApiVersion);
    out.set("title", title);
    Json list = Json::array();
    for (const auto& b : blocks) list.push(b.json());
    out.set("blocks", list);
    out.set("owner", owner);
    out.set("author", author);
    out.set("genre", genre);
    out.set("serverSize", serverSize);
    if (createdAt) out.set("createdAt", *createdAt);
    out.set("updatedAt", updatedAt);
    out.set("revision", revision);
    out.set("lighting", lighting.json());
    out.set("terrain", terrain.json());
    Json records = Json::array();
    for (const auto& r : instances) records.push(r.json());
    out.set("instances", records);
    if (icon) out.set("icon", *icon);
    if (thumbnail) out.set("thumbnail", *thumbnail);
    return out;
}

Result<Experience> Experience::fromJson(const Json& value) {
    if (!value.isObject()) return corrupt("esperienza");
    Experience e;
    auto id = requireString(value, "id");
    if (!id) return id.error();
    e.id = *id;
    auto schema = optionalInt(value, "schemaVersion");
    if (!schema) return schema.error();
    e.schemaVersion = schema->value_or(1);
    auto title = optionalString(value, "title");
    if (!title) return title.error();
    e.title = title->value_or(tr("La mia esperienza"));
    auto language = optionalString(value, "languageVersion");
    if (!language) return language.error();
    e.languageVersion = language->value_or(std::string(rules::lim::languageVersion));
    auto api = optionalInt(value, "engineApiVersion");
    if (!api) return api.error();
    e.engineApiVersion = api->value_or(rules::lim::engineApiVersion);
    if (const Json* list = value.find("blocks"); list && !list->isNull()) {
        if (!list->isArray()) return corrupt("blocks");
        for (const auto& item : list->items()) {
            auto b = WorldBlock::fromJson(item);
            if (!b) return b.error();
            e.blocks.push_back(std::move(*b));
        }
    }
    for (auto [key, target, standard] : {std::tuple{"owner", &e.owner, std::string()}, std::tuple{"author", &e.author, std::string()},
                                         std::tuple{"genre", &e.genre, std::string(rules::defaultGenre)}, std::tuple{"updatedAt", &e.updatedAt, std::string()}}) {
        auto s = optionalString(value, key);
        if (!s) return s.error();
        *target = s->value_or(standard);
    }
    auto size = optionalInt(value, "serverSize");
    if (!size) return size.error();
    e.serverSize = size->value_or(rules::serverSize::standard);
    auto created = optionalString(value, "createdAt");
    if (!created) return created.error();
    e.createdAt = *created;
    auto revision = optionalInt(value, "revision");
    if (!revision) return revision.error();
    e.revision = revision->value_or(0);
    if (const Json* l = value.find("lighting"); l && !l->isNull()) {
        if (!l->isObject()) return corrupt("lighting");
        e.lighting = WorldLighting::fromJson(*l);
    }
    if (const Json* t = value.find("terrain"); t && !t->isNull()) {
        auto terrain = Terrain::fromJson(*t);
        if (!terrain) return terrain.error();
        e.terrain = std::move(*terrain);
    }
    if (const Json* list = value.find("instances"); list && !list->isNull()) {
        if (!list->isArray()) return corrupt("instances");
        for (const auto& item : list->items()) {
            auto r = InstanceRecord::fromJson(item);
            if (!r) return r.error();
            e.instances.push_back(std::move(*r));
        }
    }
    if (e.schemaVersion < 3 && std::none_of(e.blocks.begin(), e.blocks.end(), [](const WorldBlock& b) { return b.name == rules::baseplate::name; }))
        e.blocks.insert(e.blocks.begin(), baseplate(e.id));
    if (e.schemaVersion < 4) {
        if (const Json* list = value.find("blocks"); list && list->isArray()) {
            for (const auto& legacy : list->items()) {
                const Json* scripts = legacy.find("scripts");
                if (!scripts || !scripts->isArray() || !legacy["id"].isString()) continue;
                for (const auto& script : scripts->items()) {
                    if (!script["id"].isString()) continue;
                    InstanceRecord record;
                    record.id = script["id"].string();
                    record.className = "Script";
                    record.name = script["name"].stringOr("Script");
                    record.parent = legacy["id"].string();
                    record.properties["Source"] = PropertyValue::string(script["source"].stringOr(""));
                    record.properties["Disabled"] = PropertyValue::boolean(false);
                    e.instances.push_back(std::move(record));
                }
            }
        }
    }
    for (auto [key, target] : {std::pair{"icon", &e.icon}, std::pair{"thumbnail", &e.thumbnail}})
        if (auto asset = value.optionalString(key); asset && isImageAsset(*asset)) *target = *asset;
    e.schemaVersion = std::max(4, std::min(e.schemaVersion, rules::schemaVersion));
    for (const auto& id : e.legacyScripts())
        if (auto* record = e.record(id)) record->properties["Disabled"] = PropertyValue::boolean(true);
    return e;
}

std::vector<std::string> Experience::legacyScripts() const {
    std::vector<std::string> out;
    if (schemaVersion >= rules::lim::schemaVersion) return out;
    for (const auto& record : instances)
        if ((record.className == "Script" || record.className == "LocalScript" || record.className == "ModuleScript") && !trim(record.source()).empty()) out.push_back(record.id);
    return out;
}

Result<Experience> Experience::parse(std::string_view text) {
    auto json = Json::parse(text);
    if (!json) return json.error();
    return fromJson(*json);
}

const WorldBlock* Experience::block(std::string_view key) const {
    for (const auto& b : blocks)
        if (b.id == key) return &b;
    return nullptr;
}

WorldBlock* Experience::block(std::string_view key) {
    for (auto& b : blocks)
        if (b.id == key) return &b;
    return nullptr;
}

const InstanceRecord* Experience::record(std::string_view key) const {
    for (const auto& r : instances)
        if (r.id == key) return &r;
    return nullptr;
}

InstanceRecord* Experience::record(std::string_view key) {
    for (auto& r : instances)
        if (r.id == key) return &r;
    return nullptr;
}

std::optional<std::string> Experience::parentOf(std::string_view key) const {
    if (const WorldBlock* b = block(key)) return b->parent;
    if (const InstanceRecord* r = record(key)) return r->parent;
    return std::nullopt;
}

std::string Experience::root(const WorldBlock& b) const {
    std::string current = b.parent;
    std::size_t steps = 0, limit = blocks.size() + instances.size();
    while (auto next = parentOf(current)) {
        if (steps > limit) break;
        current = *next;
        ++steps;
    }
    return steps > limit ? std::string() : current;
}

std::vector<std::string> Experience::childIds(std::string_view parent) const {
    std::vector<std::string> out;
    for (const auto& b : blocks)
        if (b.parent == parent) out.push_back(b.id);
    for (const auto& r : instances)
        if (r.parent == parent) out.push_back(r.id);
    return out;
}

std::vector<std::string> Experience::descendants(std::string_view id) const {
    std::vector<std::string> found;
    std::set<std::string> seen{std::string(id)};
    std::deque<std::string> queue{std::string(id)};
    while (!queue.empty()) {
        std::string next = queue.front();
        queue.pop_front();
        for (auto& child : childIds(next)) {
            if (seen.count(child)) continue;
            seen.insert(child);
            found.push_back(child);
            queue.push_back(child);
        }
    }
    return found;
}

std::vector<WorldBlock> Experience::workspaceParts() const {
    std::vector<WorldBlock> out;
    for (const auto& b : blocks)
        if (b.kind != "model" && root(b) == "Workspace") out.push_back(b);
    return out;
}

WorldSummary WorldSummary::fromJson(const Json& value) {
    WorldSummary s;
    s.id = value["id"].string();
    s.title = value["title"].string();
    s.owner = value["owner"].string();
    s.author = value["author"].string();
    s.genre = value.optionalString("genre");
    if (auto n = value.optionalNumber("serverSize")) s.serverSize = static_cast<int>(*n);
    s.createdAt = value.optionalString("createdAt");
    s.updatedAt = value.optionalString("updatedAt");
    if (auto asset = value.optionalString("icon"); asset && isImageAsset(*asset)) s.icon = asset;
    if (auto asset = value.optionalString("thumbnail"); asset && isImageAsset(*asset)) s.thumbnail = asset;
    return s;
}

std::string_view lifecycleName(ServerLifecycle state) {
    switch (state) {
    case ServerLifecycle::provisioning: return rules::serverStates::provisioning;
    case ServerLifecycle::ready: return rules::serverStates::ready;
    case ServerLifecycle::joinable: return rules::serverStates::joinable;
    case ServerLifecycle::full: return rules::serverStates::full;
    case ServerLifecycle::draining: return rules::serverStates::draining;
    case ServerLifecycle::terminated: return rules::serverStates::terminated;
    }
    return rules::serverStates::terminated;
}

std::optional<ServerLifecycle> lifecycleNamed(std::string_view name) {
    for (auto state : {ServerLifecycle::provisioning, ServerLifecycle::ready, ServerLifecycle::joinable, ServerLifecycle::full, ServerLifecycle::draining, ServerLifecycle::terminated})
        if (lifecycleName(state) == name) return state;
    return std::nullopt;
}

Result<ExperienceServer> ExperienceServer::fromJson(const Json& value) {
    ExperienceServer s;
    auto id = requireString(value, "id");
    if (!id) return id.error();
    s.id = *id;
    for (auto [key, target] : {std::pair{"sequence", &s.sequence}, std::pair{"capacity", &s.capacity}, std::pair{"players", &s.players}}) {
        auto n = requireNumber(value, key);
        if (!n) return n.error();
        *target = static_cast<int>(*n);
    }
    s.reserved = static_cast<int>(value["reserved"].number(0));
    if (value["state"].isString()) {
        auto state = lifecycleNamed(value["state"].string());
        if (!state) return fail(ErrorCode::corrupted, tr("Stato del server sconosciuto"));
        s.state = *state;
    } else s.state = s.full() ? ServerLifecycle::full : ServerLifecycle::joinable;
    s.region = value["region"].stringOr("global");
    s.observedAt = value.optionalString("observedAt");
    return s;
}

ExperienceFacts ExperienceFacts::fromJson(const Json& value) {
    ExperienceFacts f;
    f.id = value["id"].string();
    f.title = value["title"].string();
    f.owner = value["owner"].string();
    f.author = value["author"].string();
    f.genre = value["genre"].string();
    f.serverSize = static_cast<int>(value["serverSize"].number(0));
    f.createdAt = value.optionalString("createdAt");
    f.updatedAt = value.optionalString("updatedAt");
    f.revision = static_cast<int>(value["revision"].number(0));
    f.published = value["published"].boolean(false);
    f.visits = static_cast<int>(value["visits"].number(0));
    return f;
}

int ExperienceDetails::players() const {
    int total = 0;
    for (const auto& s : servers) total += s.players;
    return total;
}

Result<ExperienceDetails> ExperienceDetails::fromJson(const Json& value) {
    ExperienceDetails d;
    if (!value["experience"].isObject() || !value["servers"].isArray()) return corrupt(tr("dettagli esperienza"));
    d.experience = ExperienceFacts::fromJson(value["experience"]);
    for (const auto& item : value["servers"].items()) {
        auto s = ExperienceServer::fromJson(item);
        if (!s) return s.error();
        d.servers.push_back(*s);
    }
    return d;
}

}
