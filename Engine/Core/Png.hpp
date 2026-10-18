#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ld {

std::string stripPng(const std::string& png);
std::optional<std::string> encodePng(const std::vector<std::uint8_t>& rgba, int width, int height);

}
