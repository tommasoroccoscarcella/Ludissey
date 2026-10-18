#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace hygiene {

struct Finding {
    std::string kind;
    std::string text;
};

std::vector<int> comments(const std::string& text, const std::string& extension);
std::vector<Finding> verify(const std::filesystem::path& root);
std::vector<Finding> bundle(const std::filesystem::path& application);
std::vector<std::string> sweep(const std::filesystem::path& root);

}
