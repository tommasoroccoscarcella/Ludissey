#include "Url.hpp"

#include "Engine/Core/Text.hpp"

#include <cctype>

namespace ld {

std::string percentEncode(std::string_view text, bool query) {
    std::string out;
    for (unsigned char c : text) {
        bool safe = std::isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~' || (!query && (c == '/' || c == ':'));
        if (safe) out += static_cast<char>(c);
        else {
            out += '%';
            out += uppercase(hexByte(c));
        }
    }
    return out;
}

std::optional<std::string> percentDecode(std::string_view text, bool plusAsSpace) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (c == '%') {
            if (i + 2 >= text.size() || !std::isxdigit(static_cast<unsigned char>(text[i + 1])) || !std::isxdigit(static_cast<unsigned char>(text[i + 2]))) return std::nullopt;
            out += static_cast<char>(std::stoi(std::string(text.substr(i + 1, 2)), nullptr, 16));
            i += 2;
        } else if (c == '+' && plusAsSpace) out += ' ';
        else out += c;
    }
    return out;
}

std::optional<Url> Url::parse(std::string_view text) {
    Url url;
    auto colon = text.find(':');
    if (colon == std::string_view::npos || colon == 0) return std::nullopt;
    for (std::size_t i = 0; i < colon; ++i) {
        char c = text[i];
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '+' || c == '-' || c == '.')) return std::nullopt;
    }
    url.scheme = lowercase(text.substr(0, colon));
    std::string_view rest = text.substr(colon + 1);
    if (auto hash = rest.find('#'); hash != std::string_view::npos) {
        auto decoded = percentDecode(rest.substr(hash + 1));
        if (!decoded) return std::nullopt;
        url.fragment = *decoded;
        rest = rest.substr(0, hash);
    }
    std::string_view queryText;
    if (auto mark = rest.find('?'); mark != std::string_view::npos) {
        queryText = rest.substr(mark + 1);
        rest = rest.substr(0, mark);
    }
    if (startsWith(rest, "//")) {
        rest = rest.substr(2);
        auto slash = rest.find('/');
        std::string_view host = slash == std::string_view::npos ? rest : rest.substr(0, slash);
        auto decoded = percentDecode(host);
        if (!decoded) return std::nullopt;
        url.host = *decoded;
        rest = slash == std::string_view::npos ? std::string_view() : rest.substr(slash);
    }
    auto path = percentDecode(rest);
    if (!path) return std::nullopt;
    url.path = *path;
    if (!queryText.empty()) {
        for (const auto& item : split(queryText, '&')) {
            if (item.empty()) continue;
            auto equals = item.find('=');
            auto name = percentDecode(item.substr(0, equals));
            auto value = percentDecode(equals == std::string::npos ? std::string_view() : std::string_view(item).substr(equals + 1));
            if (!name || !value) return std::nullopt;
            url.query.emplace_back(*name, *value);
        }
    }
    return url;
}

std::string Url::text() const {
    std::string out = scheme + "://" + percentEncode(host, false) + percentEncode(path, false);
    if (!query.empty()) {
        out += '?';
        for (std::size_t i = 0; i < query.size(); ++i) {
            if (i) out += '&';
            out += percentEncode(query[i].first) + "=" + percentEncode(query[i].second);
        }
    }
    if (fragment) out += "#" + percentEncode(*fragment);
    return out;
}

std::optional<std::string> Url::value(std::string_view name) const {
    for (const auto& [key, value] : query)
        if (key == name) return value;
    return std::nullopt;
}

std::vector<std::string> Url::pathComponents() const {
    std::vector<std::string> out;
    for (auto& part : split(path, '/'))
        if (!part.empty()) out.push_back(part);
    return out;
}

}
