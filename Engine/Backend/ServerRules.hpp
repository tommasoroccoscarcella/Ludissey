#pragma once

#include "Engine/Core/Json.hpp"

#include <set>
#include <string>

namespace ld::server_rules {

const Json& registry();
bool identifier(const Json& value);
bool asset(const Json& value);
std::size_t textLength(std::string_view value);
std::string textPrefix(std::string_view value, std::size_t limit);
std::string text(const Json& value, std::size_t maximum);
std::string digest(std::string_view value);
std::string encode(const Json& value);
Json lighting(const Json& value);
Json baseplate(const std::string& experience);
Json properties(const std::string& className, const Json& raw);
Json part(const Json& value, std::set<std::string>& ids);
Json instances(const Json& value, std::set<std::string>& ids);
void hierarchy(const Json& parts, const Json& records);
Json terrain(const Json& value);
std::string terrainText(const Json& value);
void migrateWater(Json& world);
Json avatar(const Json& raw, const std::string& name, const std::string& body, const Json& inventory, const Json& catalog);

}
