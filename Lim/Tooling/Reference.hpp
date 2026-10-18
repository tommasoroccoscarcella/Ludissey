#pragma once

#include "Lim/Semantic/Types.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace lim::tooling {

enum class ApiKind { global, function, enumeration, classType, constructor, property, method, event, staticMethod, constant };

struct ApiEntry {
    ApiKind kind = ApiKind::global;
    std::string owner;
    std::string name;
    std::string signature;
    std::string doc;
    ExecutionContext only = ExecutionContext::shared;
    bool readOnly = false;
    bool deprecated = false;
    std::string replacement;

    std::string qualified() const { return owner.empty() ? name : owner + "." + name; }
};

std::string apiKindTitle(ApiKind kind);
std::string signature(const HostMethod& method);
std::string signature(const HostEvent& event);
std::vector<ApiEntry> apiCatalog(const TypeRegistry& registry);
std::vector<const ApiEntry*> searchApi(const std::vector<ApiEntry>& catalog, std::string_view query, std::size_t limit);

}
