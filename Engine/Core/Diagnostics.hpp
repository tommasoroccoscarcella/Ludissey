#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace ld::diagnostics {

void install(const std::filesystem::path& folder, const std::string& application);
std::vector<std::filesystem::path> reports(const std::filesystem::path& folder);

}
