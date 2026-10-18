#include "ServerRules.hpp"

#include "Engine/Core/Async.hpp"
#include "Engine/Core/Rules.hpp"
#include "Engine/Core/Text.hpp"
#include "Engine/Terrain/Terrain.hpp"
#include "Localization/Localization.hpp"
#include "ServerRegistry.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <regex>

namespace ld::server_rules {
namespace {

using localization::localizable;

[[noreturn]] void fail(const std::string& message) { throw ServiceError(message); }
bool finite(const Json& v) { return v.isNumber() && std::isfinite(v.number()); }
template <typename Range> bool has(const Range& range, const std::string& value) { return std::find(range.begin(), range.end(), value) != range.end(); }
bool includes(const Json& array, const Json& value) { return std::find(array.items().begin(), array.items().end(), value) != array.items().end(); }
bool matches(const Json& value, const char* pattern) { return value.isString() && std::regex_match(value.string(), std::regex(pattern)); }
std::string blockClass(const Json& kind) {
    for (const auto& [name, entry] : registry()["classes"].members()) if (entry["kind"] == kind) return name;
    fail(wording(localizable("Oggetto non valido")));
}
bool space(std::uint32_t c) {
    return c == 9 || c == 10 || c == 11 || c == 12 || c == 13 || c == 32 || c == 160 || c == 0x1680 ||
        (c >= 0x2000 && c <= 0x200a) || c == 0x2028 || c == 0x2029 || c == 0x202f || c == 0x205f || c == 0x3000 || c == 0xfeff;
}
std::string trimmed(std::string_view value) {
    auto points = utf8Decode(value);
    auto first = std::find_if_not(points.begin(), points.end(), space);
    auto last = std::find_if_not(points.rbegin(), points.rend(), space).base();
    return first < last ? utf8Encode({first, last}) : "";
}
}

const Json& registry() {
    static const Json value = *Json::parse(registrySource);
    return value;
}
bool identifier(const Json& value) { return matches(value, "[a-f0-9-]{36}"); }
bool asset(const Json& value) { return matches(value, "[a-f0-9]{64}\\.[a-z0-9]{1,5}"); }
std::size_t textLength(std::string_view value) {
    std::size_t result = 0;
    for (auto c : utf8Decode(value)) result += c > 0xffff ? 2 : 1;
    return result;
}
std::string textPrefix(std::string_view value, std::size_t limit) {
    std::vector<std::uint32_t> result;
    std::size_t count = 0;
    for (auto c : utf8Decode(value)) {
        count += c > 0xffff ? 2 : 1;
        if (count > limit) break;
        result.push_back(c);
    }
    return utf8Encode(result);
}
std::string text(const Json& value, std::size_t maximum) {
    if (!value.isString() || textLength(value.string()) > maximum) fail(wording(localizable("Testo non valido")));
    auto result = trimmed(value.string());
    if (result.empty()) fail(wording(localizable("Testo non valido")));
    return result;
}
std::string digest(std::string_view value) {
    std::string bytes;
    for (auto c : utf8Decode(value)) {
        if (c > 0xffff) {
            c -= 0x10000;
            bytes += static_cast<char>((0xd800 + (c >> 10)) & 255);
            bytes += static_cast<char>((0xdc00 + (c & 1023)) & 255);
        } else bytes += static_cast<char>(c & 255);
    }
    return sha256Hex(bytes);
}
std::string encode(const Json& value) {
    if (value.isNumber()) {
        auto number = formatNumber(value.number());
        auto e = number.find('e');
        if (e == std::string::npos) return number;
        int exponent = std::stoi(number.substr(e + 1));
        if (exponent < -6 || exponent >= 21) return number;
        bool negative = number[0] == '-';
        std::string digits = number.substr(negative ? 1 : 0, e - (negative ? 1 : 0));
        digits.erase(std::remove(digits.begin(), digits.end(), '.'), digits.end());
        int point = exponent + 1;
        if (point <= 0) digits = "0." + std::string(static_cast<std::size_t>(-point), '0') + digits;
        else if (static_cast<std::size_t>(point) >= digits.size()) digits.append(static_cast<std::size_t>(point) - digits.size(), '0');
        else digits.insert(static_cast<std::size_t>(point), ".");
        return (negative ? "-" : "") + digits;
    }
    if (value.isArray()) {
        std::string result = "[";
        for (const auto& entry : value.items()) { if (result.size() > 1) result += ','; result += encode(entry); }
        return result + ']';
    }
    if (value.isObject()) {
        std::vector<const Json::Member*> numeric, other;
        auto index = [](const std::string& key) -> std::optional<unsigned long> {
            if (key.empty() || key.size() > 10 || (key.size() > 1 && key[0] == '0') || key.find_first_not_of("0123456789") != std::string::npos) return std::nullopt;
            auto n = std::stoul(key);
            return n < 4294967295UL ? std::optional<unsigned long>(n) : std::nullopt;
        };
        for (const auto& member : value.members()) (index(member.first) ? numeric : other).push_back(&member);
        std::sort(numeric.begin(), numeric.end(), [&](auto a, auto b) { return *index(a->first) < *index(b->first); });
        numeric.insert(numeric.end(), other.begin(), other.end());
        std::string result = "{";
        for (const auto* member : numeric) { if (result.size() > 1) result += ','; result += escapeJson(member->first) + ':' + encode(member->second); }
        return result + '}';
    }
    return value.dump();
}
Json lighting(const Json& value) {
    auto span = [&](const char* key, double low, double high, double standard) {
        return finite(value[key]) ? std::clamp(value[key].number(), low, high) : standard;
    };
    auto color = [&](const char* key, std::string_view fallback) -> Json { return matches(value[key], "[0-9A-Fa-f]{6}") ? value[key] : Json(fallback); };
    using namespace rules::lighting;
    return Json::object({{"clockTime", span("clockTime", clockTime::minimum, clockTime::maximum, clockTime::standard)},
        {"brightness", span("brightness", brightness::minimum, brightness::maximum, brightness::standard)},
        {"fogEnd", span("fogEnd", fogEnd::minimum, fogEnd::maximum, fogEnd::standard)},
        {"ambient", color("ambient", ambient)}, {"skyTop", color("skyTop", skyTop)}, {"skyHorizon", color("skyHorizon", skyHorizon)}});
}
Json baseplate(const std::string& experience) {
    using namespace rules::baseplate;
    auto suffix = replaceAll(experience, "-", "");
    suffix = suffix.substr(suffix.size() > 12 ? suffix.size() - 12 : 0);
    return Json::object({{"id", "00000000-0000-4000-8000-" + suffix}, {"kind", "cube"}, {"name", name}, {"color", rules::part::color},
        {"x", 0}, {"y", -height / 2.0}, {"z", 0}, {"size", width}, {"sizeX", width}, {"sizeY", height}, {"sizeZ", width},
        {"rotationX", 0}, {"rotationY", 0}, {"rotationZ", 0}, {"material", "plastic"}, {"transparency", 0}, {"reflectance", 0},
        {"anchored", true}, {"canCollide", true}, {"locked", locked}, {"parent", "Workspace"}});
}
Json properties(const std::string& className, const Json& raw) {
    Json result = Json::object();
    if (raw.isNull()) return result;
    const auto& known = registry()["classes"][className];
    if (!raw.isObject() || known.isNull()) fail(wording(localizable("Proprietà non valide")));
    for (const auto& [name, v] : raw.members()) {
        const auto& type = known["properties"][name];
        if (type.isNull() && !(name == "PrimaryPart" && className == "Model")) fail(wording(localizable("Proprietà {0} non valida per {1}"), name, className));
        auto category = type.isNull() ? "Class" : type[0].string();
        auto typeName = type.isNull() ? "BasePart" : type[1].string();
        if (category == "Primitive" && typeName == "bool") {
            if (!v.isBool()) fail(wording(localizable("Valore non valido per {0}"), name));
        } else if ((category == "Primitive" && typeName == "string") || typeName == "ProtectedString") {
            auto limit = name == "Source" ? rules::instances::sourceCharacters : rules::instances::textCharacters;
            if (!v["String"].isString() || textLength(v["String"].string()) > static_cast<std::size_t>(limit)) fail(wording(localizable("Testo non valido per {0}"), name));
        } else if (category == "Primitive") {
            if (!finite(v)) fail(wording(localizable("Numero non valido per {0}"), name));
        } else if (typeName == "Content") {
            if (!v["Content"].isString() || textLength(v["Content"].string()) > rules::instances::textCharacters) fail(wording(localizable("Contenuto non valido per {0}"), name));
        } else if (typeName == "Vector3" || typeName == "Vector2" || typeName == "UDim" || typeName == "UDim2") {
            std::size_t count = typeName == "Vector3" ? 3 : typeName == "UDim2" ? 4 : 2;
            const auto& list = v[typeName];
            if (!list.isArray() || list.size() != count || !std::all_of(list.items().begin(), list.items().end(), finite)) fail(wording(localizable("{0} non valido per {1}"), typeName, name));
        } else if (typeName == "Color3" || typeName == "BrickColor") {
            if (!matches(v["Color3"], "[0-9A-F]{6}")) fail(wording(localizable("Colore non valido per {0}"), name));
        } else if (category == "Enum") {
            if (!includes(registry()["enums"][typeName], v["Enum"])) fail(wording(localizable("Valore {0} non valido per {1}"), typeName, name));
        } else if (category == "Class") {
            if (!v.contains("Ref") || !(v["Ref"].isNull() || identifier(v["Ref"]))) fail(wording(localizable("Riferimento non valido per {0}"), name));
        } else fail(wording(localizable("Proprietà {0} non supportata"), name));
        result.set(name, v);
    }
    return result;
}
Json part(const Json& b, std::set<std::string>& ids) {
    using namespace rules::world;
    if (!identifier(b["id"]) || !ids.insert(b["id"].string()).second) fail(wording(localizable("Identificatore oggetto non valido")));
    if (!has(rules::kinds, b["kind"].string()) || !matches(b["color"], "[0-9A-Fa-f]{6}")) fail(wording(localizable("Oggetto non valido")));
    for (const auto* k : {"x", "y", "z", "size"}) if (!finite(b[k])) fail(wording(localizable("Dimensioni non valide")));
    if (std::abs(b["x"].number()) > horizontal || std::abs(b["z"].number()) > horizontal || std::abs(b["y"].number()) > vertical) fail(wording(localizable("Oggetto fuori dai limiti")));
    Json result = Json::object();
    for (const auto* k : {"id", "kind", "color", "x", "y", "z"}) result.set(k, b[k]);
    for (const auto* k : {"size", "sizeX", "sizeY", "sizeZ"}) {
        double n = finite(b[k]) ? b[k].number() : b["size"].number();
        if (n < minimumSize || n > maximumSize) fail(wording(localizable("Oggetto fuori dai limiti")));
        result.set(k, n);
    }
    for (const auto* k : {"rotationX", "rotationY", "rotationZ"}) {
        double n = finite(b[k]) ? b[k].number() : 0;
        if (n < -180 || n > 180) fail(wording(localizable("Orientamento non valido")));
        result.set(k, n);
    }
    result.set("material", has(rules::materials, b["material"].string()) ? b["material"] : Json("plastic"));
    for (const auto* k : {"transparency", "reflectance"}) result.set(k, finite(b[k]) ? std::clamp(b[k].number(), 0.0, 1.0) : 0);
    result.set("anchored", b["anchored"] != Json(false));
    result.set("canCollide", b["canCollide"] != Json(false));
    result.set("locked", b["locked"] == Json(true));
    result.set("parent", b["parent"].isString() && !b["parent"].string().empty() ? b["parent"] : Json("Workspace"));
    result.set("name", textPrefix(b["name"].string(), nameCharacters));
    auto extra = properties(blockClass(b["kind"]), b["properties"]);
    if (extra.size()) result.set("properties", extra);
    if (b["kind"] == Json("mesh")) {
        if (!asset(b["meshAsset"])) fail("Modello senza riferimento asset");
        result.set("meshAsset", b["meshAsset"]);
    }
    return result;
}
Json instances(const Json& value, std::set<std::string>& ids) {
    Json result = Json::array();
    if (value.isNull()) return result;
    if (!value.isArray() || value.size() > rules::instances::maximum) fail(wording(localizable("Massimo {0} istanze per esperienza"), std::to_string(rules::instances::maximum)));
    for (const auto& r : value.items()) {
        if (!r.isObject() || !identifier(r["id"]) || !ids.insert(r["id"].string()).second) fail(wording(localizable("Identificatore istanza non valido")));
        const auto& known = registry()["classes"][r["className"].string()];
        if (known.isNull() || known.contains("kind")) fail(wording(localizable("Classe non supportata: {0}"), (r.contains("className") ? r["className"].string() : "undefined")));
        if (!r["name"].isString() || r["name"].string().empty() || textLength(r["name"].string()) > rules::instances::nameCharacters) fail(wording(localizable("Nome istanza non valido")));
        if (!r["parent"].isString() || r["parent"].string().empty()) fail(wording(localizable("Genitore istanza non valido")));
        Json item = Json::object();
        for (const auto* k : {"id", "className", "name", "parent"}) item.set(k, r[k]);
        auto extra = properties(r["className"].string(), r["properties"]);
        if (extra.size()) item.set("properties", extra);
        result.push(item);
    }
    return result;
}
void hierarchy(const Json& parts, const Json& records) {
    Json nodes = Json::object();
    auto node = [](const Json& type, const Json& parent) { return Json::object({{"className", type}, {"parent", parent}}); };
    for (const auto& service : registry()["services"].items()) nodes.set(service.string(), node(service, nullptr));
    for (const auto& entry : registry()["virtual"].items()) nodes.set(entry["id"].string(), node(entry["className"], nullptr));
    for (const auto& b : parts.items()) nodes.set(b["id"].string(), node(blockClass(b["kind"]), b["parent"]));
    for (const auto& r : records.items()) nodes.set(r["id"].string(), node(r["className"], r["parent"]));
    const auto& all = std::as_const(nodes);
    for (const auto& [id, entry] : all.members()) {
        if (entry["parent"].isNull()) continue;
        auto parent = entry["parent"].string();
        const auto& owner = all[parent];
        if (owner.isNull()) fail(wording(localizable("Genitore non valido: {0}"), parent));
        auto type = entry["className"].string();
        if (!includes(registry()["classes"][type]["parents"], owner["className"])) fail(wording(localizable("{0} non può essere inserito in {1}"), type, owner["className"].string()));
        std::size_t steps = 0;
        while (!all[parent].isNull() && !all[parent]["parent"].isNull()) {
            parent = all[parent]["parent"].string();
            if (parent == id || ++steps > parts.size() + records.size()) fail("Gerarchia ciclica");
        }
    }
}
Json water(const Json& value) {
    namespace w = rules::terrain::water;
    Json result = Json::object({{"color", std::string(w::color)}, {"transparency", w::transparency::standard}, {"reflectance", w::reflectance::standard}, {"waveSize", w::waveSize::standard}, {"waveSpeed", w::waveSpeed::standard}});
    if (value.isNull()) return result;
    if (!value.isObject()) fail(wording(localizable("Terreno non valido")));
    if (!value["color"].isNull()) {
        auto color = value["color"].string();
        if (!value["color"].isString() || color.size() != 6 || color.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) fail(wording(localizable("Terreno non valido")));
        for (char& c : color) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        result.set("color", color);
    }
    auto bounded = [&](const char* key, double low, double high) {
        if (value[key].isNull()) return;
        if (!finite(value[key]) || value[key].number() < low || value[key].number() > high) fail(wording(localizable("Terreno non valido")));
        result.set(key, value[key].number());
    };
    bounded("transparency", w::transparency::minimum, w::transparency::maximum);
    bounded("reflectance", w::reflectance::minimum, w::reflectance::maximum);
    bounded("waveSize", w::waveSize::minimum, w::waveSize::maximum);
    bounded("waveSpeed", w::waveSpeed::minimum, w::waveSpeed::maximum);
    return result;
}
Json terrain(const Json& value) {
    Json result = Json::object({{"format", rules::terrain::format}, {"settings", nullptr}, {"water", water(nullptr)}, {"chunks", Json::object()}});
    if (value.isNull() || value == Json(false) || value == Json(0) || value == Json("")) return result;
    if ((!value.isObject() && !value.isArray()) || (!value["chunks"].isNull() && !value["chunks"].isObject() && !value["chunks"].isArray())) fail(wording(localizable("Terreno non valido")));
    int format = 1;
    if (!value["format"].isNull()) {
        if (value["format"] != Json(rules::terrain::format)) fail(wording(localizable("Terreno non valido")));
        format = rules::terrain::format;
    }
    result.set("water", water(value["water"]));
    const auto& s = value["settings"];
    if (s.isObject() || s.isArray()) {
        Json settings = Json::object();
        for (const auto* key : {"seed", "amplitude", "baseHeight", "waterLevel"}) {
            double low = std::string(key) == "seed" ? 1 : std::string(key) == "amplitude" ? 0 : -16;
            double high = std::string(key) == "seed" ? 999999 : std::string(key) == "amplitude" ? 12 : 16;
            if (!finite(s[key]) || s[key].number() < low || s[key].number() > high) fail(wording(localizable("Terreno non valido")));
            settings.set(key, std::string(key) == "seed" ? std::floor(s[key].number()) : s[key].number());
        }
        result.set("settings", settings);
    }
    std::map<std::string, Json> sorted;
    for (const auto& [key, encoded] : value["chunks"].members()) sorted.emplace(key, encoded);
    for (const auto& [key, encoded] : sorted) {
        auto axes = split(key, ',');
        if (axes.size() != 3) fail(wording(localizable("Terreno non valido")));
        int counts[] = {Terrain::chunksX, Terrain::chunksY, Terrain::chunksZ};
        for (std::size_t i = 0; i < 3; ++i) {
            if (axes[i].empty() || axes[i].find_first_not_of("0123456789") != std::string::npos) fail(wording(localizable("Terreno non valido")));
            auto n = parseNumber(axes[i]);
            if (!n || *n >= counts[i]) fail(wording(localizable("Terreno non valido")));
        }
        if (!encoded.isString()) fail(wording(localizable("Terreno non valido")));
        auto cells = Terrain::decodeChunk(encoded.string(), format);
        if (!cells) fail(wording(localizable("Terreno non valido")));
        if (std::any_of(cells->begin(), cells->end(), [](auto c) { return c != 0; })) result["chunks"].set(key, Terrain::encodeChunk(*cells));
    }
    return result;
}
std::string terrainText(const Json& value) {
    return "{\"format\":" + encode(value["format"]) + ",\"settings\":" + encode(value["settings"]) + ",\"water\":" + encode(value["water"]) + ",\"chunks\":" + value["chunks"].dump(true) + "}";
}
void migrateWater(Json& world) {
    Json blocks = Json::array();
    std::map<std::string, std::vector<std::uint16_t>> decoded;
    bool changed = false;
    for (const auto& b : std::as_const(world)["blocks"].items()) {
        if (b["material"] != Json("water")) { blocks.push(b); continue; }
        if (!changed) {
            for (const auto& [key, value] : std::as_const(world)["terrain"]["chunks"].members()) {
                auto cells = Terrain::decodeChunk(value.string());
                if (!cells) fail(wording(localizable("Terreno non valido")));
                decoded[key] = *cells;
            }
            changed = true;
        }
        double hx = (b["sizeX"].number() ? b["sizeX"].number() : b["size"].number()) / 2;
        double hy = (b["sizeY"].number() ? b["sizeY"].number() : b["size"].number()) / 2;
        double hz = (b["sizeZ"].number() ? b["sizeZ"].number() : b["size"].number()) / 2;
        for (int i = 0; i < Terrain::cellsX; ++i) for (int j = 0; j < Terrain::cellsY; ++j) for (int k = 0; k < Terrain::cellsZ; ++k) {
            auto c = Terrain::center(i, j, k);
            if (std::abs(c.x - b["x"].number()) > hx || std::abs(c.y - b["y"].number()) > hy || std::abs(c.z - b["z"].number()) > hz) continue;
            auto& cells = decoded[Terrain::key(i, j, k).text()];
            if (cells.empty()) cells.resize(Terrain::volume);
            auto& cell = cells[Terrain::offset(i, j, k)];
            if (!Terrain::occupancy(cell)) cell = Terrain::compose(TerrainMaterial::air, 0, Terrain::levels);
        }
    }
    if (!changed) return;
    for (const auto& [key, cells] : decoded) if (std::any_of(cells.begin(), cells.end(), [](auto c) { return c != 0; })) world["terrain"]["chunks"].set(key, Terrain::encodeChunk(cells));
    world.set("blocks", blocks);
    world.set("terrainDigest", digest(terrainText(std::as_const(world)["terrain"])).substr(0, 16));
}
Json avatar(const Json& raw, const std::string& name, const std::string& body, const Json& inventory, const Json& catalog) {
    using namespace rules::avatar;
    Json result = Json::object({{"displayName", name}, {"bodyPreset", has(bodyPresets, body) ? Json(body) : Json(standard::bodyPreset)}, {"bodyColors", Json::object()}, {"equipped", Json::array()}});
    auto shown = trimmed(raw["displayName"].string());
    if (textLength(shown) >= 3 && textLength(shown) <= nameCharacters) result.set("displayName", shown);
    if (has(bodyPresets, raw["bodyPreset"].string())) result.set("bodyPreset", raw["bodyPreset"]);
    Json colors = raw["bodyColors"];
    if (!colors.isObject() && !colors.isArray()) colors = Json::object({{"head", raw["skinTone"]}, {"leftArm", raw["skinTone"]}, {"rightArm", raw["skinTone"]}, {"torso", raw["shirtColor"]}, {"leftLeg", raw["pantsColor"]}, {"rightLeg", raw["pantsColor"]}});
    const std::map<std::string_view, std::string_view> defaults = {{"head", standard::bodyColors::head}, {"torso", standard::bodyColors::torso}, {"leftArm", standard::bodyColors::leftArm}, {"rightArm", standard::bodyColors::rightArm}, {"leftLeg", standard::bodyColors::leftLeg}, {"rightLeg", standard::bodyColors::rightLeg}};
    for (auto key : bodyParts) result["bodyColors"].set(std::string(key), has(palette, colors[key].string()) ? colors[key] : Json(defaults.at(key)));
    std::set<std::string> owned;
    std::map<std::string, Json> known;
    for (const auto& entry : inventory.items()) owned.insert(entry["itemID"].string());
    for (const auto& item : catalog.items()) known[item["id"].string()] = item;
    Json equipped = Json::array();
    for (const auto& e : raw["equipped"].items()) {
        auto id = e["itemID"].string(), category = e["category"].string();
        auto found = known.find(id);
        if (!owned.contains(id) || found == known.end() || found->second["category"] != e["category"]) continue;
        if (std::any_of(equipped.items().begin(), equipped.items().end(), [&](const auto& v) { return v["itemID"] == e["itemID"]; })) continue;
        int limit = category == "hat" ? limits::hats : has(accessories, category) ? limits::accessories : 1;
        if (limit == 1) std::erase_if(equipped.items(), [&](const auto& v) { return v["category"] == e["category"]; });
        else if (std::count_if(equipped.items().begin(), equipped.items().end(), [&](const auto& v) { return v["category"] == e["category"]; }) >= limit) continue;
        if (has(accessories, category) && std::count_if(equipped.items().begin(), equipped.items().end(), [&](const auto& v) { return has(accessories, v["category"].string()); }) >= limits::accessories) continue;
        equipped.push(Json::object({{"itemID", e["itemID"]}, {"category", e["category"]}}));
    }
    result.set("equipped", equipped);
    return result;
}
}
