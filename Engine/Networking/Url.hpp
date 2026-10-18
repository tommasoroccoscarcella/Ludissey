#pragma once

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ld {

struct Url {
    std::string scheme;
    std::string host;
    std::string path;
    std::vector<std::pair<std::string, std::string>> query;
    std::optional<std::string> fragment;

    static std::optional<Url> parse(std::string_view text);
    std::string text() const;
    std::optional<std::string> value(std::string_view name) const;
    std::vector<std::string> pathComponents() const;
};

std::string percentEncode(std::string_view text, bool query = true);
std::optional<std::string> percentDecode(std::string_view text, bool plusAsSpace = false);

}
