#pragma once

#include <optional>
#include <string>

namespace ld::local_preferences {

std::optional<double> number(const std::string& key);
void setNumber(const std::string& key, double value);
void remove(const std::string& key);
void useMemory(bool enabled);

namespace keys {
inline constexpr const char* masterVolume = "LudisseyMasterVolume";
inline constexpr const char* chatVisible = "LudisseyChatVisible";
inline constexpr const char* graphicsChoice = "LudisseyGraphicsChoice";
inline constexpr const char* language = "LudisseyLanguage";
}

}
