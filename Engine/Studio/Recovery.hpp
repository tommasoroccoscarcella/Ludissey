#pragma once

#include "Engine/Core/Result.hpp"
#include "Engine/Experience/Experience.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ld::studio_recovery {

struct Entry {
    std::string id;
    std::string title;
    std::string savedAt;
    std::size_t parts = 0;
};

inline constexpr double interval = 30;

std::filesystem::path folder();
void setFolder(std::optional<std::filesystem::path> value);
Result<void> write(const Experience& world, const std::string& savedAt);
std::vector<Entry> list();
Result<Experience> load(const std::string& id);
bool exists(const std::string& id);
void discard(const std::string& id);

}
