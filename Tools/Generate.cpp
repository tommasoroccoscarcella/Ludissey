#include "Engine/Core/Json.hpp"
#include "Engine/Core/Text.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using ld::Json;

namespace {

fs::path root;
bool checking = false;
std::vector<std::string> stale;
std::string glyphModule;

std::string read(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("File mancante: " + path.string());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

Json load(const fs::path& path) {
    auto parsed = Json::parse(read(path), 4096);
    if (!parsed) throw std::runtime_error("JSON non valido: " + path.string());
    return *parsed;
}

void write(const fs::path& path, const std::string& text) {
    std::string current;
    if (fs::exists(path)) current = read(path);
    if (current == text) return;
    if (checking) {
        stale.push_back(fs::relative(path, root).string());
        return;
    }
    std::ofstream(path, std::ios::binary) << text;
}

bool integral(double value) { return std::floor(value) == value && std::abs(value) < 1e15; }

std::string literal(const Json& value) {
    if (value.isBool()) return value.boolean() ? "true" : "false";
    if (value.isString()) return "std::string_view(" + ld::escapeJson(value.string()) + ")";
    return ld::formatNumber(value.number());
}

std::string kind(const Json& value) {
    if (value.isBool()) return "bool";
    if (value.isString()) return "std::string_view";
    return integral(value.number()) ? "int" : "double";
}

void members(const Json& values, const std::string& indent, std::vector<std::string>& lines) {
    for (const auto& [name, value] : values.members()) {
        if (value.isObject()) {
            lines.push_back(indent + "namespace " + name + " {");
            members(value, indent + "    ", lines);
            lines.push_back(indent + "}");
        } else if (value.isArray()) {
            bool strings = std::all_of(value.items().begin(), value.items().end(), [](const Json& j) { return j.isString(); });
            bool fractional = std::any_of(value.items().begin(), value.items().end(), [](const Json& j) { return j.isNumber() && !integral(j.number()); });
            std::string type = strings ? "std::string_view" : fractional ? "double" : "int";
            std::string items;
            for (const auto& item : value.items()) items += (items.empty() ? "" : ", ") + literal(item);
            lines.push_back(indent + "inline constexpr std::array<" + type + ", " + std::to_string(value.size()) + "> " + name + "{" + items + "};");
        } else lines.push_back(indent + "inline constexpr " + kind(value) + " " + name + " = " + literal(value) + ";");
    }
}

Json catalog() {
    Json api = load(root / "HistoricalAPI/2017.json");
    Json rules = load(root / "Resources/class-rules.json");
    std::map<std::string, const Json*> raw;
    for (const auto& entry : api["classes"].items()) raw[entry["name"].string()] = &entry;
    const Json& groups = rules["groups"];
    std::function<std::vector<std::string>(const Json&)> expand = [&](const Json& names) {
        std::vector<std::string> out;
        for (const auto& item : names.items()) {
            const std::string& name = item.string();
            if (!name.empty() && name[0] == '@') {
                auto inner = expand(groups[name.substr(1)]);
                out.insert(out.end(), inner.begin(), inner.end());
            } else out.push_back(name);
        }
        return out;
    };
    auto ancestry = [&](std::string name) {
        std::vector<std::string> chain;
        while (raw.count(name) && std::find(chain.begin(), chain.end(), name) == chain.end()) {
            chain.push_back(name);
            name = (*raw[name])["superclass"].stringOr("");
        }
        return chain;
    };
    auto typesOf = [&](const std::string& name) {
        std::map<std::string, const Json*> found;
        for (const auto& owner : ancestry(name))
            if (const Json* list = raw[owner]->find("properties"); list && list->isArray())
                for (const auto& entry : list->items()) found.emplace(entry["name"].string(), &entry["type"]);
        return found;
    };
    std::vector<std::string> services;
    for (const auto& entry : rules["services"].items()) services.push_back(entry["name"].string());
    std::map<std::string, std::vector<std::string>> limits;
    for (const auto& [name, values] : rules["serviceChildren"].members()) limits[name] = expand(values);
    const Json& classes = rules["classes"];
    auto contains = [](const std::vector<std::string>& list, const std::string& value) { return std::find(list.begin(), list.end(), value) != list.end(); };
    auto allowed = [&](const std::string& child, const std::string& parent) {
        auto lineage = ancestry(child);
        if (limits.count(parent) && std::none_of(limits[parent].begin(), limits[parent].end(), [&](const std::string& k) { return contains(lineage, k); })) return false;
        const Json* parents = classes[child].find("parents");
        auto above = ancestry(parent);
        for (const auto& k : parents ? expand(*parents) : std::vector<std::string>{})
            if (contains(above, k)) return true;
        return false;
    };
    std::vector<std::string> names;
    for (const auto& [name, rule] : classes.members()) names.push_back(name);
    std::sort(names.begin(), names.end());
    std::vector<std::string> candidates = services;
    candidates.insert(candidates.end(), names.begin(), names.end());
    std::map<std::string, std::vector<std::string>> enums;
    for (const auto& entry : api["enums"].items()) {
        std::vector<std::string> items;
        for (const auto& item : entry["items"].items()) items.push_back(item["name"].string());
        enums[entry["name"].string()] = items;
    }
    Json result = Json::object(), used = Json::object();
    for (const auto& name : names) {
        const Json& rule = classes[name];
        auto types = typesOf(name);
        Json properties = Json::object();
        if (const Json* list = rule.find("properties"))
            for (const auto& item : list->items()) {
                auto found = types.find(item.string());
                if (found == types.end()) continue;
                const Json& type = *found->second;
                properties.set(item.string(), Json::array({type["category"], type["name"]}));
                if (type["category"].string() == "Enum") {
                    Json values = Json::array();
                    for (const auto& v : enums[type["name"].string()]) values.push(v);
                    used.set(type["name"].string(), values);
                }
            }
        Json parents = Json::array();
        for (const auto& candidate : candidates)
            if (allowed(name, candidate)) parents.push(candidate);
        Json entry = Json::object({{"parents", parents}, {"properties", properties}});
        if (const Json* k = rule.find("kind")) entry.set("kind", *k);
        result.set(name, entry);
    }
    Json virtuals = Json::array();
    for (const auto& entry : rules["defaults"].items()) virtuals.push(Json::object({{"id", entry["parent"].string() + "." + entry["className"].string()}, {"className", entry["className"]}}));
    Json listed = Json::array();
    for (const auto& s : services) listed.push(s);
    return Json::object({{"classes", result}, {"enums", used}, {"services", listed}, {"virtual", virtuals}});
}

void rules() {
    Json values = load(root / "Resources/experience-rules.json");
    Json registry = catalog();
    Json payload = values;
    payload.set("registry", registry);
    std::string script = "var RULES = " + payload.dump(true) + ";\n";
    write(root / "CloudScript/Rules.js", script);
    std::string body = script + read(root / "CloudScript/Avatar.js");
    write(root / "Web/rules.js", "var LudisseyRules = (function() {\n" + body + "return {rules:RULES, avatar:avatarValidate, avatarLimit:avatarLimit};\n})();\n");
    std::vector<std::string> lines{"#pragma once", "", "#include <array>", "#include <string_view>", "", "namespace ld::rules {", ""};
    members(values, "", lines);
    lines.insert(lines.end(), {"", "}"});
    std::string header;
    for (const auto& line : lines) header += line + "\n";
    write(root / "Engine/Core/Rules.hpp", header);
    write(root / "Engine/Backend/ServerRegistry.hpp", "#pragma once\n\n#include <string_view>\n\nnamespace ld::server_rules {\ninline constexpr std::string_view registrySource = R\"REGISTRY(" + registry.dump(true) + ")REGISTRY\";\n}\n");
}

std::string kebab(const std::string& name) {
    std::string out;
    for (char c : name) {
        if (std::isupper(static_cast<unsigned char>(c))) {
            out += '-';
            out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        } else out += c;
    }
    return out;
}

void tokens() {
    Json values = load(root / "Resources/design-tokens.json");
    std::string css = ":root {\n";
    std::string header = "#pragma once\n\n#include <string_view>\n\nnamespace ui::tokens {\n\n";
    auto color = [](const std::string& hex) {
        std::string digits = !hex.empty() && hex[0] == '#' ? hex.substr(1) : hex;
        if (digits.size() != 6) throw std::runtime_error("Colore non valido nei token: " + hex);
        return digits;
    };
    auto rgbaOf = [&](const std::string& hex, double opacity) {
        std::string digits = color(hex);
        auto channel = [&](std::size_t at) { return std::to_string(std::stoi(digits.substr(at, 2), nullptr, 16)); };
        return "rgba(" + channel(0) + ", " + channel(2) + ", " + channel(4) + ", " + ld::formatNumber(opacity) + ")";
    };
    auto scalar = [&](const std::string& group, const std::string& name, const Json& value, const std::string& prefix, std::string& space) {
        std::string variable = "  --ludissey-" + prefix + kebab(name) + ": ";
        if (value.isString()) {
            std::string text = value.string();
            if (group == "font" && name == "family") {
                css += variable + "\"" + text + "\", Helvetica, Arial, sans-serif;\n";
                space += "inline constexpr std::string_view " + name + " = std::string_view(" + ld::escapeJson(text) + ");\n";
                return;
            }
            css += variable + text + ";\n";
            space += "inline constexpr std::string_view " + name + " = std::string_view(" + ld::escapeJson(!text.empty() && text[0] == '#' ? text.substr(1) : text) + ");\n";
        } else if (value.isBool()) {
            space += "inline constexpr bool " + name + " = " + (value.boolean() ? "true" : "false") + ";\n";
        } else {
            std::string number = ld::formatNumber(value.number());
            bool plain = group == "motion" || (group == "shadow" && name != "offset" && name != "blur" && name != "dy") || group == "elevation" || group == "opacity" || name == "opacity" || name == "position";
            std::string unit = group == "motion" ? "ms" : plain ? "" : "px";
            if (group == "radius" && value.number() < 1 && value.number() > 0) css += variable + ld::formatNumber(value.number() * 100) + "%;\n";
            else css += variable + number + unit + ";\n";
            space += "inline constexpr double " + name + " = " + number + ";\n";
        }
    };
    for (const auto& [group, entries] : values.members()) {
        header += "namespace " + group + " {\n";
        for (const auto& [name, value] : entries.members()) {
            if (!value.isObject()) {
                scalar(group, name, value, group == "color" ? "" : group + "-", header);
                continue;
            }
            std::string space;
            for (const auto& [field, inner] : value.members()) scalar(group, field, inner, group + "-" + kebab(name) + "-", space);
            std::string variable = "  --ludissey-" + group + "-" + kebab(name) + ": ";
            if (value.contains("top") && value.contains("bottom")) {
                std::string stops = "#" + color(value["top"].string());
                if (value.contains("mid")) stops += ", #" + color(value["mid"].string()) + " 50%";
                if (value.contains("split")) stops += ", #" + color(value["split"].string()) + " 50%";
                stops += ", #" + color(value["bottom"].string());
                css += variable + "linear-gradient(" + stops + ");\n";
            } else if (value.contains("opacity") && value.contains("color")) {
                std::string inset = value.contains("inset") && value["inset"].boolean() ? "inset " : "";
                double dy = value.contains("dy") ? value["dy"].number() : 0, blur = value.contains("blur") ? value["blur"].number() : 0;
                css += variable + inset + "0 " + ld::formatNumber(dy) + "px " + ld::formatNumber(blur) + "px " + rgbaOf(value["color"].string(), value["opacity"].number()) + ";\n";
            }
            header += "namespace " + name + " {\n" + space + "}\n";
        }
        if (group == "pattern") {
            std::string stripe = "#" + color(entries["stripe"].string()), base = "#" + color(entries["base"].string());
            std::string width = ld::formatNumber(entries["width"].number()), period = ld::formatNumber(entries["period"].number());
            css += "  --ludissey-pattern: repeating-linear-gradient(90deg, " + stripe + " 0, " + stripe + " " + width + "px, " + base + " " + width + "px, " + base + " " + period + "px);\n";
        }
        header += "}\n\n";
    }
    write(root / "Web/tokens.css", css + "}\n");
    write(root / "Interface/Tokens.hpp", header + "}\n");
}

std::string encoded(const std::string& text) {
    std::string out;
    for (unsigned char c : text) {
        if (std::isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~' || c == '/') out += static_cast<char>(c);
        else {
            char buffer[4];
            std::snprintf(buffer, sizeof buffer, "%%%02X", c);
            out += buffer;
        }
    }
    return out;
}

struct Step {
    char kind;
    double values[6];
};

std::vector<Step> outline(const std::string& name, const std::string& svg) {
    static const std::regex shape(R"re(^<svg xmlns="http://www\.w3\.org/2000/svg" viewBox="0 0 24 24"><path d="([^"]+)"/></svg>\n$)re");
    std::smatch found;
    if (!std::regex_match(svg, found, shape)) throw std::runtime_error("Glifo non valido (serve un solo path con viewBox 0 0 24 24): " + name);
    std::string data = found[1].str();
    std::vector<Step> steps;
    std::size_t at = 0;
    double x = 0, y = 0, startX = 0, startY = 0, controlX = 0, controlY = 0;
    char command = 0, previous = 0;
    auto skip = [&] {
        while (at < data.size() && (data[at] == ' ' || data[at] == ',')) ++at;
    };
    auto number = [&] {
        skip();
        std::size_t used = 0;
        double value = 0;
        try {
            value = std::stod(data.substr(at), &used);
        } catch (const std::exception&) {
            throw std::runtime_error("Numero non valido nel glifo: " + name);
        }
        at += used;
        return value;
    };
    while (true) {
        skip();
        if (at >= data.size()) break;
        if (std::isalpha(static_cast<unsigned char>(data[at]))) command = data[at++];
        else if (!command || command == 'Z' || command == 'z') throw std::runtime_error("Comando mancante nel glifo: " + name);
        bool relative = std::islower(static_cast<unsigned char>(command));
        char kind = static_cast<char>(std::toupper(static_cast<unsigned char>(command)));
        double baseX = relative ? x : 0, baseY = relative ? y : 0;
        if (kind == 'Z') {
            steps.push_back({'Z', {}});
            x = startX;
            y = startY;
        } else if (kind == 'M' || kind == 'L') {
            x = baseX + number();
            y = baseY + number();
            steps.push_back({kind, {0, 0, 0, 0, x, y}});
            if (kind == 'M') {
                startX = x;
                startY = y;
                command = relative ? 'l' : 'L';
            }
        } else if (kind == 'H' || kind == 'V') {
            if (kind == 'H') x = baseX + number();
            else y = baseY + number();
            steps.push_back({'L', {0, 0, 0, 0, x, y}});
        } else if (kind == 'C' || kind == 'S') {
            double x1 = previous == 'C' ? 2 * x - controlX : x, y1 = previous == 'C' ? 2 * y - controlY : y;
            if (kind == 'C') {
                x1 = baseX + number();
                y1 = baseY + number();
            }
            double x2 = baseX + number(), y2 = baseY + number();
            x = baseX + number();
            y = baseY + number();
            steps.push_back({'C', {x1, y1, x2, y2, x, y}});
            controlX = x2;
            controlY = y2;
            kind = 'C';
        } else if (kind == 'Q') {
            double x1 = baseX + number(), y1 = baseY + number();
            x = baseX + number();
            y = baseY + number();
            steps.push_back({'Q', {x1, y1, 0, 0, x, y}});
        } else throw std::runtime_error("Comando non ammesso nel glifo " + name + ": " + std::string(1, command));
        previous = kind;
    }
    if (steps.empty() || steps.front().kind != 'M') throw std::runtime_error("Glifo senza punto iniziale: " + name);
    return steps;
}

std::map<std::string, std::string> glyphs() {
    std::map<std::string, std::string> sources;
    for (const auto& entry : fs::directory_iterator(root / "Resources/Icons/Glyphs"))
        if (entry.path().extension() == ".svg") sources[entry.path().stem().string()] = read(entry.path());
    if (sources.empty()) throw std::runtime_error("Nessun glifo in Resources/Icons/Glyphs");
    std::string header = "#pragma once\n\n#include \"Interface/Geometry.hpp\"\n\n#include <string_view>\n\nnamespace ui::glyphs {\n\n", table, module = "const ludisseyGlyphs = {\n";
    bool first = true;
    int index = 0;
    for (const auto& [name, svg] : sources) {
        auto steps = outline(name, svg);
        std::string symbol = "glyph" + std::to_string(index++);
        header += "inline constexpr PathStep " + symbol + "[] = {";
        for (std::size_t i = 0; i < steps.size(); ++i) {
            header += std::string(i ? ", " : "") + "{'" + steps[i].kind + "'";
            for (double value : steps[i].values) header += ", " + ld::formatNumber(value);
            header += "}";
        }
        header += "};\n";
        table += "    {std::string_view(\"" + name + "\"), " + symbol + ", " + std::to_string(steps.size()) + "},\n";
        std::string markup = svg.substr(0, svg.size() - 1);
        module += std::string(first ? "" : ",\n") + "  " + ld::escapeJson(name) + ": " + ld::escapeJson(markup);
        first = false;
    }
    header += "\nstruct Glyph {\n    std::string_view name;\n    const PathStep* steps;\n    int count;\n};\n\ninline constexpr Glyph all[] = {\n" + table + "};\n\n}\n";
    write(root / "Interface/Glyphs.hpp", header);
    glyphModule = module + "\n};\n";
    return sources;
}

std::string preview(const std::string& name, const std::string& data) {
    auto big = [&](std::size_t at) {
        return (static_cast<std::uint32_t>(static_cast<unsigned char>(data[at])) << 24) | (static_cast<std::uint32_t>(static_cast<unsigned char>(data[at + 1])) << 16) |
               (static_cast<std::uint32_t>(static_cast<unsigned char>(data[at + 2])) << 8) | static_cast<std::uint32_t>(static_cast<unsigned char>(data[at + 3]));
    };
    const std::string signature("\x89PNG\r\n\x1a\n", 8);
    std::string best;
    std::uint32_t side = 0;
    for (std::size_t at = data.find(signature); at != std::string::npos; at = data.find(signature, at + 1)) {
        if (at + 24 > data.size()) break;
        std::uint32_t width = big(at + 16), height = big(at + 20);
        std::size_t cursor = at + 8, end = std::string::npos;
        while (cursor + 12 <= data.size()) {
            std::uint32_t length = big(cursor);
            if (cursor + 12 + length > data.size()) break;
            bool last = data.compare(cursor + 4, 4, "IEND") == 0;
            cursor += 12 + length;
            if (last) {
                end = cursor;
                break;
            }
        }
        if (end == std::string::npos || width != height || width <= side) continue;
        side = width;
        best = data.substr(at, end - at);
    }
    if (side < 256) throw std::runtime_error("Anteprima quadrata di almeno 256 px mancante nel file .af: " + name);
    return best;
}

void icons() {
    auto drawn = glyphs();
    fs::path designs = root / "Design/Icons", pack = root / "Resources/Icons/Pack";
    if (!checking) fs::create_directories(pack);
    std::map<std::string, std::string> masters;
    double smallest = 1e9;
    for (const auto& entry : fs::directory_iterator(designs)) {
        if (entry.path().extension() != ".af") continue;
        std::string stem = entry.path().stem().string(), file;
        for (char c : stem)
            if (c != ' ') file += c;
        file += ".png";
        std::string image = preview(stem, read(entry.path()));
        double side = (static_cast<unsigned char>(image[16]) << 24) | (static_cast<unsigned char>(image[17]) << 16) | (static_cast<unsigned char>(image[18]) << 8) | static_cast<unsigned char>(image[19]);
        smallest = std::min(smallest, side);
        write(pack / file, image);
        masters[stem] = file;
    }
    std::set<std::string> produced;
    for (const auto& [stem, file] : masters) produced.insert(file);
    if (fs::exists(pack))
        for (const auto& entry : fs::directory_iterator(pack))
            if (entry.path().filename().string()[0] != '.' && !produced.count(entry.path().filename().string()))
                throw std::runtime_error("Icona del pacchetto senza file .af: " + entry.path().filename().string());
    Json names = load(root / "Resources/Icons/icon-names.json");
    std::map<std::string, std::string> semantic, missing, site;
    auto master = [&](const std::string& key, const std::string& stem) {
        if (!masters.count(stem)) throw std::runtime_error("Nome semantico senza file .af: " + key);
        return masters[stem];
    };
    for (const auto& group : {"semantic", "app"})
        for (const auto& [key, value] : names[group].members()) {
            if (semantic.count(key)) throw std::runtime_error("Nome semantico ripetuto: " + key);
            semantic[key] = master(key, value.string());
        }
    for (const auto& [key, value] : names["app"].members()) {
        if (key.rfind("ld-icon-app-", 0) != 0 && key.rfind("ld-icon-status-", 0) != 0) throw std::runtime_error("Icona del gruppo app con nome non ammesso: " + key);
        site[key] = fs::path(semantic[key]).stem().string();
    }
    for (const auto& [key, value] : names["missing"].members()) {
        if (semantic.count(key)) throw std::runtime_error("Nome sia nel pacchetto sia fra le icone mancanti: " + key);
        if (!value.string().empty() && !drawn.count(value.string())) throw std::runtime_error("Icona mancante con glifo sostitutivo inesistente: " + key);
        missing[key] = value.string();
    }
    for (const auto& [key, value] : names["planned"].members()) {
        if (!semantic.count(key) && !missing.count(key)) throw std::runtime_error("Icona prevista senza icona attuale: " + key);
        if (masters.count(value.string())) {
            semantic[key] = masters[value.string()];
            missing.erase(key);
        } else if (checking)
            std::cerr << "Icona prevista non ancora disegnata: " << key << " (" << value.string() << ".af)\n";
    }
    std::set<std::string> used;
    std::regex pattern("ld-icon-[a-z]+(?:-[a-z0-9]+)+");
    auto scan = [&](const fs::path& path) {
        std::string text = read(path);
        for (std::sregex_iterator it(text.begin(), text.end(), pattern), end; it != end; ++it) used.insert(it->str());
    };
    for (const char* folder : {"Interface", "Player", "Studio", "Engine"})
        for (const auto& entry : fs::recursive_directory_iterator(root / folder)) {
            auto extension = entry.path().extension();
            if (entry.is_regular_file() && (extension == ".cpp" || extension == ".hpp" || extension == ".mm") && entry.path().filename() != "IconPack.cpp") scan(entry.path());
        }
    scan(root / "Resources/class-rules.json");
    scan(root / "Web/app.js");
    for (const char* file : {"Interface/ClientView.cpp", "Interface/ClientMedia.cpp", "Player/PlayerView.cpp", "Engine/Client/Models.cpp", "Web/app.js", "Web/index.html", "Web/style.css"}) {
        std::string text = read(root / file);
        for (std::sregex_iterator it(text.begin(), text.end(), pattern), end; it != end; ++it)
            if (it->str().rfind("ld-icon-app-", 0) != 0 && it->str().rfind("ld-icon-status-", 0) != 0)
                throw std::runtime_error(std::string("Icona dello Studio usata fuori dallo Studio: ") + it->str() + " in " + file);
    }
    for (const auto& name : used)
        if (!semantic.count(name) && !missing.count(name)) throw std::runtime_error("Nome semantico usato ma non registrato: " + name);
    for (const auto& [name, file] : semantic)
        if (!used.count(name)) throw std::runtime_error("Nome semantico registrato ma mai usato: " + name);
    for (const auto& [name, glyph] : missing)
        if (!used.count(name)) throw std::runtime_error("Nome semantico registrato ma mai usato: " + name);
    for (const auto& [name, value] : load(root / "Resources/design-tokens.json")["icon"].members())
        if (value.number() > smallest / 2) throw std::runtime_error("Dimensione icona oltre metà del master più piccolo: " + name);
    auto table = [](const std::map<std::string, std::string>& mapping) {
        std::string out;
        for (const auto& [key, value] : mapping) out += "        {\"" + key + "\", \"" + value + "\"},\n";
        return out;
    };
    write(root / "Interface/IconPack.cpp", "#include \"Interface/Components.hpp\"\n\nnamespace ui::icons {\n\nconst std::map<std::string, std::string>& semanticFiles() {\n    static const std::map<std::string, std::string> table = {\n" + table(semantic) +
                                                 "    };\n    return table;\n}\n\nconst std::map<std::string, std::string>& placeholders() {\n    static const std::map<std::string, std::string> table = {\n" + table(missing) +
                                                 "    };\n    return table;\n}\n\n}\n");
    std::string module = glyphModule + "\nconst ludisseyIcons = {\n";
    bool first = true;
    for (const auto& [key, stem] : site) {
        module += std::string(first ? "" : ",\n") + "  " + ld::escapeJson(key) + ": " + ld::escapeJson(stem);
        first = false;
    }
    write(root / "Web/icons.js", module + "\n};\n");
}

std::string unescape(const std::string& raw) {
    std::string out;
    for (std::size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] != '\\' || i + 1 == raw.size()) {
            out += raw[i];
            continue;
        }
        char next = raw[++i];
        out += next == 'n' ? '\n' : next == 't' ? '\t' : next;
    }
    return out;
}

std::string cppLiteral(const std::string& value) {
    std::string out = "\"";
    for (char c : value) {
        if (c == '"' || c == '\\') out += std::string("\\") + c;
        else if (c == '\n') out += "\\n";
        else if (c == '\t') out += "\\t";
        else out += c;
    }
    return out + "\"";
}

std::multiset<std::string> placeholders(const std::string& text) {
    std::multiset<std::string> found;
    static const std::regex pattern("\\{[0-9]\\}");
    for (std::sregex_iterator it(text.begin(), text.end(), pattern), end; it != end; ++it) found.insert(it->str());
    return found;
}

void localization() {
    std::map<std::string, std::set<std::string>> used;
    std::set<std::string> web;
    static const std::regex single(R"re(\b(tr|localizable)\(\s*"((?:[^"\\]|\\.)*)"\s*[,)])re"), plural(R"re(\btrn\(\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*,)re"),
        opening(R"re(\b(tr|trn|localizable)\(\s*")re"), script(R"re(\b(tr|localizable)\(\s*(?:'((?:[^'\\]|\\.)*)'|"((?:[^"\\]|\\.)*)")\s*[,)])re"), scriptOpening(R"re(\b(tr|localizable)\(\s*['"])re");
    auto scanNative = [&](const fs::path& path) {
        std::string text = read(path), where = fs::relative(path, root).string();
        bool server = where.rfind("Engine/Backend/Server", 0) == 0;
        std::size_t found = 0, opened = 0;
        for (std::sregex_iterator it(text.begin(), text.end(), single), end; it != end; ++it, ++found) {
            used[unescape((*it)[2].str())].insert(where);
            if (server) web.insert(unescape((*it)[2].str()));
        }
        for (std::sregex_iterator it(text.begin(), text.end(), plural), end; it != end; ++it, ++found) {
            used[unescape((*it)[1].str())].insert(where);
            used[unescape((*it)[2].str())].insert(where);
        }
        for (std::sregex_iterator it(text.begin(), text.end(), opening), end; it != end; ++it) ++opened;
        if (found != opened) throw std::runtime_error("Chiave di traduzione non letterale o spezzata in " + where);
    };
    auto scanScript = [&](const fs::path& path, bool site) {
        std::string text = read(path), where = fs::relative(path, root).string();
        std::size_t found = 0, opened = 0;
        for (std::sregex_iterator it(text.begin(), text.end(), script), end; it != end; ++it, ++found) {
            std::string key = unescape((*it)[2].matched ? (*it)[2].str() : (*it)[3].str());
            used[key].insert(where);
            if (site) web.insert(key);
        }
        for (std::sregex_iterator it(text.begin(), text.end(), scriptOpening), end; it != end; ++it) ++opened;
        if (found != opened) throw std::runtime_error("Chiave di traduzione non letterale o spezzata in " + where);
    };
    for (const char* folder : {"Engine", "Interface", "Lim", "Platform", "Player", "Studio"})
        for (const auto& entry : fs::recursive_directory_iterator(root / folder)) {
            auto extension = entry.path().extension();
            if (entry.is_regular_file() && (extension == ".cpp" || extension == ".hpp" || extension == ".mm")) scanNative(entry.path());
        }
    for (const auto& entry : fs::directory_iterator(root / "Web"))
        if (entry.path().extension() == ".js" && entry.path().filename() != "localization.js") scanScript(entry.path(), true);
    for (const auto& entry : fs::directory_iterator(root / "CloudScript"))
        if (entry.path().extension() == ".js") scanScript(entry.path(), true);
    Json rules = load(root / "Resources/experience-rules.json");
    auto shared = [&](const Json& value) {
        if (value.isString()) {
            used[value.string()].insert("Resources/experience-rules.json");
            web.insert(value.string());
        } else
            for (const auto& item : value.items()) {
                used[item.string()].insert("Resources/experience-rules.json");
                web.insert(item.string());
            }
    };
    for (const Json* value : {&rules["genres"], &rules["serverList"]["emptyTitle"], &rules["serverList"]["emptyDetail"], &rules["avatar"]["bodyPresetTitles"], &rules["avatar"]["bodyPartTitles"], &rules["avatar"]["categoryTitles"]}) shared(*value);
    Json catalog = load(root / "Resources/Localization/en.json");
    std::map<std::string, std::string> english;
    for (const auto& [key, value] : catalog.members()) {
        if (!value.isString() || value.string().empty()) throw std::runtime_error("Traduzione inglese vuota: " + key);
        english[key] = value.string();
    }
    std::vector<std::string> problems;
    for (const auto& [key, files] : used)
        if (!english.count(key)) problems.push_back("Traduzione inglese mancante (" + *files.begin() + "): " + ld::escapeJson(key));
    for (const auto& [key, value] : english) {
        if (!used.count(key)) problems.push_back("Traduzione inglese mai usata: " + ld::escapeJson(key));
        else if (placeholders(key) != placeholders(value)) problems.push_back("Segnaposto diversi fra italiano e inglese: " + ld::escapeJson(key));
    }
    if (!problems.empty()) {
        std::string message;
        for (const auto& problem : problems) message += problem + "\n";
        throw std::runtime_error(message + std::to_string(problems.size()) + " problemi nel catalogo di localizzazione");
    }
    std::string table;
    for (const auto& [key, value] : english) table += "    {" + cppLiteral(key) + ", " + cppLiteral(value) + "},\n";
    write(root / "Localization/Catalog.cpp", "#include \"Localization/Localization.hpp\"\n\nnamespace ld::localization::catalog {\n\nextern const std::pair<std::string_view, std::string_view> english[] = {\n" + table + "};\n\nextern const std::size_t englishCount = " + std::to_string(english.size()) + ";\n\n}\n");
    std::string module = "const ludisseyEnglish = {\n";
    bool first = true;
    for (const auto& key : web) {
        module += std::string(first ? "" : ",\n") + "  " + ld::escapeJson(key) + ": " + ld::escapeJson(english[key]);
        first = false;
    }
    write(root / "Web/localization.js", module + "\n};\n");
}

}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Uso: LudisseyGenerate <radice del repository> [--check]\n";
        return 2;
    }
    root = fs::weakly_canonical(argv[1]);
    checking = argc > 2 && std::string(argv[2]) == "--check";
    try {
        rules();
        tokens();
        icons();
        localization();
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
    if (!stale.empty()) {
        for (const auto& path : stale) std::cerr << "File generato non aggiornato: " << path << "\n";
        return 1;
    }
    return 0;
}
