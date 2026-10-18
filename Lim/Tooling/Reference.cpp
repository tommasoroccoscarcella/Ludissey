#include "Lim/Tooling/Reference.hpp"

#include "Localization/Localization.hpp"

#include <algorithm>
#include <cctype>
#include <tuple>

namespace lim::tooling {

namespace {

std::string typeText(const TypePtr& type) { return type ? type->describe() : "Any"; }

std::string parameters(const std::vector<std::string>& names, const std::vector<TypePtr>& types, bool variadic) {
    std::string text;
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (i) text += ", ";
        text += names[i];
        if (i < types.size() && types[i]) text += ": " + types[i]->describe();
    }
    if (variadic) text += names.empty() ? "..." : ", ...";
    return text;
}

std::string lower(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

}

std::string apiKindTitle(ApiKind kind) {
    switch (kind) {
    case ApiKind::global: return ld::tr("globale");
    case ApiKind::function: return ld::tr("funzione");
    case ApiKind::enumeration: return ld::tr("enum");
    case ApiKind::classType: return ld::tr("classe");
    case ApiKind::constructor: return ld::tr("costruttore");
    case ApiKind::property: return ld::tr("proprietà");
    case ApiKind::method: return ld::tr("metodo");
    case ApiKind::event: return ld::tr("evento");
    case ApiKind::staticMethod: return ld::tr("metodo statico");
    case ApiKind::constant: return ld::tr("costante");
    }
    return ld::tr("globale");
}

std::string signature(const HostMethod& method) {
    std::string text = method.async ? "async fn " : "fn ";
    text += method.name + "(" + parameters(method.parameterNames, method.parameters, method.variadic) + ")";
    if (method.result && method.result->kind != Type::Kind::none) text += " -> " + method.result->describe();
    return text;
}

std::string signature(const HostEvent& event) {
    std::string text = "on " + event.name;
    if (!event.parameterNames.empty() || event.variadic) text += "(" + parameters(event.parameterNames, event.parameters, event.variadic) + ")";
    return text;
}

std::vector<ApiEntry> apiCatalog(const TypeRegistry& registry) {
    std::vector<ApiEntry> out;
    for (const auto& [name, global] : registry.globals()) out.push_back({ApiKind::global, "", name, name + ": " + typeText(global.type), global.doc, global.only, false, false, ""});
    for (const auto& [name, function] : registry.functions()) out.push_back({ApiKind::function, "", name, signature(function), function.doc, function.only, false, function.deprecated, function.replacement});
    for (const auto& [name, items] : registry.enums()) {
        std::string cases;
        for (const auto& item : items) cases += (cases.empty() ? "." : ", .") + item;
        out.push_back({ApiKind::enumeration, "", name, "enum " + name, cases, ExecutionContext::shared, false, false, ""});
    }
    for (const auto& [name, type] : registry.classes()) {
        std::string heading = "class " + name + (type.base.empty() ? "" : " : " + type.base);
        out.push_back({ApiKind::classType, "", name, heading, type.doc, ExecutionContext::shared, false, false, ""});
        if (type.constructor) out.push_back({ApiKind::constructor, name, name, name + "(" + parameters(type.constructor->parameterNames, type.constructor->parameters, type.constructor->variadic) + ")", type.constructor->doc, type.constructor->only, false, false, ""});
        const std::string& base = type.base;
        for (const auto& [member, property] : type.properties)
            if (base.empty() || !registry.property(base, member))
                out.push_back({ApiKind::property, name, member, member + ": " + typeText(property.type), property.doc, property.only, property.readOnly, property.deprecated, property.replacement});
        for (const auto& [member, method] : type.methods)
            if (base.empty() || !registry.method(base, member)) out.push_back({ApiKind::method, name, member, signature(method), method.doc, method.only, false, method.deprecated, method.replacement});
        for (const auto& [member, event] : type.events)
            if (base.empty() || !registry.event(base, member)) out.push_back({ApiKind::event, name, member, signature(event), event.doc, ExecutionContext::shared, false, event.deprecated, event.replacement});
        for (const auto& [member, method] : type.statics) out.push_back({ApiKind::staticMethod, name, member, "static " + signature(method), method.doc, method.only, false, method.deprecated, method.replacement});
        for (const auto& [member, constant] : type.constants) out.push_back({ApiKind::constant, name, member, member + ": " + typeText(constant.type), constant.doc, constant.only, true, false, ""});
    }
    return out;
}

std::vector<const ApiEntry*> searchApi(const std::vector<ApiEntry>& catalog, std::string_view query, std::size_t limit) {
    std::string needle = lower(query);
    std::vector<std::tuple<int, std::size_t, const ApiEntry*>> ranked;
    for (const auto& entry : catalog) {
        std::string name = lower(entry.name), qualified = lower(entry.qualified());
        int rank = -1;
        if (needle.empty()) rank = entry.kind == ApiKind::classType || entry.kind == ApiKind::global ? 0 : -1;
        else if (name == needle || qualified == needle) rank = 0;
        else if (name.rfind(needle, 0) == 0 || qualified.rfind(needle, 0) == 0) rank = 1;
        else if (qualified.find(needle) != std::string::npos) rank = 2;
        else if (lower(entry.doc).find(needle) != std::string::npos) rank = 3;
        if (rank < 0) continue;
        if (entry.deprecated) rank += 4;
        ranked.emplace_back(rank, entry.qualified().size(), &entry);
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
        if (std::get<0>(a) != std::get<0>(b)) return std::get<0>(a) < std::get<0>(b);
        if (std::get<1>(a) != std::get<1>(b)) return std::get<1>(a) < std::get<1>(b);
        return std::get<2>(a)->qualified() < std::get<2>(b)->qualified();
    });
    std::vector<const ApiEntry*> out;
    for (const auto& item : ranked) {
        if (out.size() >= limit) break;
        out.push_back(std::get<2>(item));
    }
    return out;
}

}
