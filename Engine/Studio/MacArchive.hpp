#pragma once

#include "Engine/Backend/Backend.hpp"
#include "Engine/Experience/Experience.hpp"
#include "Engine/Studio/StudioSync.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ld::mac_archive {

std::filesystem::path folder();
void setFolder(std::optional<std::filesystem::path> value);
std::vector<Experience> projects(const std::string& username);
Async<StudioReadBack> upload(std::shared_ptr<Backend> backend, const std::string& username, Experience world);
void remove(const std::string& username, const std::string& id);

}
