#include "ClassRegistry.hpp"

#include <algorithm>
#include <fstream>
#include <functional>
#include <mutex>
#include <sstream>
#include <tuple>

namespace ld {

namespace {

std::shared_ptr<ClassRegistry>& storage() {
    static std::shared_ptr<ClassRegistry> registry;
    return registry;
}

Result<Json> readJson(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return fail(ErrorCode::notFound, "Registro non trovato: " + path.filename().string());
    std::stringstream buffer;
    buffer << file.rdbuf();
    return Json::parse(buffer.str(), 512);
}

std::optional<PropertyValue> defaultValue(const Json* json) {
    if (!json || json->isNull()) return std::nullopt;
    if (json->isBool()) return PropertyValue::boolean(json->boolean());
    if (json->isNumber()) return PropertyValue::number(json->number());
    auto parsed = PropertyValue::fromJson(*json);
    if (!parsed) return std::nullopt;
    return *parsed;
}

std::vector<std::string> strings(const Json& list) {
    std::vector<std::string> out;
    for (const auto& item : list.items())
        if (item.isString()) out.push_back(item.string());
    return out;
}

}

bool ClassDescriptor::hasTag(std::string_view tag) const { return std::find(tags.begin(), tags.end(), tag) != tags.end(); }
bool ClassDescriptor::creatable() const { return !hasTag("NotCreatable") && !hasTag("Service"); }

Result<std::shared_ptr<ClassRegistry>> ClassRegistry::load(const std::filesystem::path& apiFile, const std::filesystem::path& rulesFile) {
    auto api = readJson(apiFile);
    if (!api) return api.error();
    auto rulesJson = readJson(rulesFile);
    if (!rulesJson) return rulesJson.error();
    auto registry = std::make_shared<ClassRegistry>();
    registry->datasetYear = static_cast<int>((*api)["compatibility"].integer(0));
    std::map<std::string, const Json*> raw;
    for (const auto& entry : (*api)["classes"].items()) raw[entry["name"].string()] = &entry;
    const Json& classRules = (*rulesJson)["classes"];
    const Json& propertyDefaults = (*rulesJson)["propertyDefaults"];
    for (const auto& [name, list] : (*rulesJson)["groups"].members()) registry->groupTable[name] = strings(list);
    std::function<std::vector<std::string>(const std::vector<std::string>&, int)> expand = [&](const std::vector<std::string>& names, int depth) {
        std::vector<std::string> out;
        for (const auto& name : names) {
            if (!name.empty() && name[0] == '@') {
                if (depth > 16) continue;
                auto group = registry->groupTable.find(name.substr(1));
                if (group == registry->groupTable.end()) continue;
                for (auto& item : expand(group->second, depth + 1)) out.push_back(item);
            } else out.push_back(name);
        }
        return out;
    };
    auto ancestry = [&](const std::string& name) {
        std::vector<std::string> chain;
        std::optional<std::string> current = name;
        while (current) {
            auto it = raw.find(*current);
            if (it == raw.end() || std::find(chain.begin(), chain.end(), *current) != chain.end()) break;
            chain.push_back(*current);
            const Json& super = (*it->second)["superclass"];
            current = super.isString() ? std::optional<std::string>(super.string()) : std::nullopt;
        }
        return chain;
    };
    for (const auto& [name, entry] : raw) {
        ClassDescriptor d;
        d.className = name;
        auto chain = ancestry(name);
        for (const auto& owner : chain) {
            const Json& source = *raw[owner];
            for (const auto& property : source["properties"].items()) {
                std::string propertyName = property["name"].string();
                if (d.properties.count(propertyName)) continue;
                PropertyDescriptor p;
                p.name = propertyName;
                p.category = property["type"]["category"].string();
                p.typeName = property["type"]["name"].string();
                p.readOnly = property["readOnly"].boolean(false);
                auto tags = strings(property["tags"]);
                p.deprecated = std::find(tags.begin(), tags.end(), "Deprecated") != tags.end();
                p.owner = owner;
                d.properties[propertyName] = p;
            }
            for (const char* list : {"functions", "yields"})
                for (const auto& member : source[list].items()) d.functions.insert(member["name"].string());
            for (const auto& member : source["events"].items()) {
                d.events.insert(member["name"].string());
                d.eventParameters[member["name"].string()] = strings(member["parameters"]);
            }
        }
        const Json* rule = classRules.find(name);
        if (rule) {
            for (const auto& property : (*rule)["properties"].items()) {
                std::optional<PropertyValue> resolved;
                for (const auto& ancestor : chain) {
                    resolved = defaultValue(propertyDefaults.find(property.string() + "@" + ancestor));
                    if (resolved) break;
                }
                if (!resolved) resolved = defaultValue(propertyDefaults.find(property.string()));
                if (resolved) d.defaults[property.string()] = *resolved;
            }
            for (const auto& [property, source] : (*rule)["defaults"].members())
                if (source.isString()) d.defaults[property] = PropertyValue::string(source.string());
        }
        const Json* super = entry->find("superclass");
        if (super && super->isString()) d.superclass = super->string();
        d.tags = strings((*entry)["tags"]);
        d.category = (*entry)["category"].string();
        d.explorerOrder = static_cast<int>((*entry)["explorerOrder"].integer(999));
        d.preferredParent = (*entry)["preferredParent"].string();
        d.deprecated = (*entry)["deprecated"].boolean(false);
        d.metadataInsertable = (*entry)["insertable"].boolean(true);
        d.implemented = rule != nullptr;
        if (rule) {
            if ((*rule)["runtime"].isString()) d.runtime = (*rule)["runtime"].string();
            if ((*rule)["icon"].isString()) d.icon = (*rule)["icon"].string();
            d.parents = expand(strings((*rule)["parents"]), 0);
            d.supported = strings((*rule)["properties"]);
            if ((*rule)["kind"].isString()) d.kind = (*rule)["kind"].string();
            d.insertFlag = (*rule)["insert"].boolean(true);
        }
        registry->classes[name] = std::move(d);
    }
    for (const auto& service : (*rulesJson)["services"].items()) registry->serviceList.push_back({service["name"].string(), service["icon"].string()});
    for (const auto& [name, list] : (*rulesJson)["serviceChildren"].members()) registry->childrenOfServices[name] = expand(strings(list), 0);
    for (const auto& item : (*rulesJson)["defaults"].items()) registry->defaultInstances.emplace_back(item["className"].string(), item["parent"].string());
    for (const auto& entry : (*api)["enums"].items()) {
        std::vector<std::string> items;
        for (const auto& item : entry["items"].items())
            if (item["name"].isString()) items.push_back(item["name"].string());
        registry->enumeration[entry["name"].string()] = items;
    }
    return registry;
}

void ClassRegistry::setShared(std::shared_ptr<ClassRegistry> registry) { storage() = std::move(registry); }
bool ClassRegistry::hasShared() { return storage() != nullptr; }

const ClassRegistry& ClassRegistry::shared() {
    static const ClassRegistry empty;
    return storage() ? *storage() : empty;
}

const ClassDescriptor* ClassRegistry::find(std::string_view className) const {
    auto it = classes.find(std::string(className));
    return it == classes.end() ? nullptr : &it->second;
}

bool ClassRegistry::isA(std::string_view className, std::string_view ancestor) const {
    std::optional<std::string> current{std::string(className)};
    for (int steps = 0; current && steps < 64; ++steps) {
        if (*current == ancestor) return true;
        const ClassDescriptor* d = find(*current);
        current = d ? d->superclass : std::nullopt;
    }
    return false;
}

bool ClassRegistry::isService(std::string_view name) const {
    return std::any_of(serviceList.begin(), serviceList.end(), [&](const ServiceDescriptor& s) { return s.name == name; });
}

bool ClassRegistry::canParent(std::string_view child, std::string_view parent) const {
    const ClassDescriptor* d = find(child);
    if (!d || !d->implemented) return false;
    auto allowed = childrenOfServices.find(std::string(parent));
    if (allowed != childrenOfServices.end() && std::none_of(allowed->second.begin(), allowed->second.end(), [&](const std::string& a) { return isA(child, a); })) return false;
    return std::any_of(d->parents.begin(), d->parents.end(), [&](const std::string& p) { return isA(parent, p); });
}

std::vector<const ClassDescriptor*> ClassRegistry::insertable(std::string_view parent) const {
    std::vector<const ClassDescriptor*> out;
    for (const auto& [name, d] : classes)
        if (d.insertable() && canParent(name, parent)) out.push_back(&d);
    std::sort(out.begin(), out.end(), [](const ClassDescriptor* a, const ClassDescriptor* b) {
        return std::tie(a->category, a->explorerOrder, a->className) < std::tie(b->category, b->explorerOrder, b->className);
    });
    return out;
}

PropertyMap ClassRegistry::defaults(std::string_view className) const {
    const ClassDescriptor* d = find(className);
    return d ? d->defaults : PropertyMap{};
}

}
