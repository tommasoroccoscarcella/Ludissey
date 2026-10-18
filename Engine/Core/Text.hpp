#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ld {

std::string makeUuid();
bool isUuid(std::string_view text);
std::string lowercase(std::string_view text);
std::string uppercase(std::string_view text);
std::string trim(std::string_view text);
bool startsWith(std::string_view text, std::string_view prefix);
bool endsWith(std::string_view text, std::string_view suffix);
std::vector<std::string> split(std::string_view text, char separator);
std::string join(const std::vector<std::string>& parts, std::string_view separator);
std::string replaceAll(std::string text, std::string_view from, std::string_view to);
std::size_t utf8Length(std::string_view text);
std::string utf8Prefix(std::string_view text, std::size_t characters);
std::vector<std::uint32_t> utf8Decode(std::string_view text);
std::string utf8Encode(const std::vector<std::uint32_t>& codes);
std::string base64Encode(const std::uint8_t* data, std::size_t size);
std::string base64Encode(std::string_view data);
std::optional<std::vector<std::uint8_t>> base64Decode(std::string_view text);
std::string sha256Hex(const std::uint8_t* data, std::size_t size);
std::string sha256Hex(std::string_view data);
std::string hexByte(unsigned value);
std::string formatFixed(double value, int decimals);
std::string formatGeneral(double value);
std::string doubleText(double value);

}
