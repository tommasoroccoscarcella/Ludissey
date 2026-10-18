#include "Localization/Localization.hpp"

#include <CoreFoundation/CoreFoundation.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <regex>
#include <vector>

namespace ld::localization {

namespace catalog {
extern const std::pair<std::string_view, std::string_view> english[];
extern const std::size_t englishCount;
}

namespace {

std::atomic<int> forcedLanguage{-1};

Language detect() {
    if (const char* value = std::getenv("LUDISSEY_LANGUAGE"))
        if (auto parsed = parseLanguage(value)) return *parsed;
    Language result = Language::english;
    if (CFArrayRef preferred = CFLocaleCopyPreferredLanguages()) {
        if (CFArrayGetCount(preferred) > 0) {
            auto first = static_cast<CFStringRef>(CFArrayGetValueAtIndex(preferred, 0));
            char buffer[64] = {0};
            if (CFStringGetCString(first, buffer, sizeof buffer, kCFStringEncodingUTF8))
                if (auto parsed = parseLanguage(buffer)) result = *parsed;
        }
        CFRelease(preferred);
    }
    return result;
}

Language system() {
    static const Language value = detect();
    return value;
}

std::string grouped(long long value, char separator) {
    auto magnitude = static_cast<unsigned long long>(value);
    if (value < 0) magnitude = 0ULL - magnitude;
    std::string digits = std::to_string(magnitude);
    std::string out;
    int count = 0;
    for (auto it = digits.rbegin(); it != digits.rend(); ++it) {
        if (count > 0 && count % 3 == 0) out.insert(out.begin(), separator);
        out.insert(out.begin(), *it);
        ++count;
    }
    return value < 0 ? "-" + out : out;
}

}

Language language() {
    int forced = forcedLanguage.load();
    return forced >= 0 ? static_cast<Language>(forced) : system();
}

void setLanguage(std::optional<Language> forced) { forcedLanguage = forced ? static_cast<int>(*forced) : -1; }

std::optional<Language> parseLanguage(std::string_view code) {
    if (code.size() >= 2) {
        std::string prefix{static_cast<char>(std::tolower(static_cast<unsigned char>(code[0]))), static_cast<char>(std::tolower(static_cast<unsigned char>(code[1])))};
        if (code.size() == 2 || code[2] == '-' || code[2] == '_') {
            if (prefix == "it") return Language::italian;
            if (prefix == "en") return Language::english;
        }
    }
    return std::nullopt;
}

std::string_view code() { return language() == Language::italian ? "it" : "en"; }

std::string translate(std::string_view key) {
    std::string_view source = key.substr(0, key.find("@@"));
    if (language() == Language::italian) return std::string(source);
    const auto* begin = catalog::english;
    const auto* end = catalog::english + catalog::englishCount;
    const auto* found = std::lower_bound(begin, end, key, [](const auto& entry, std::string_view value) { return entry.first < value; });
    return found != end && found->first == key ? std::string(found->second) : std::string(source);
}

std::string translateMessage(std::string_view message) {
    if (language() == Language::italian || message.empty()) return std::string(message);
    std::string prefix, body(message);
    if (body.size() > 2 && body[0] == '[') {
        auto end = body.find("] ");
        if (end != std::string::npos) {
            prefix = body.substr(0, end + 2);
            body = body.substr(end + 2);
        }
    }
    std::string exact = translate(body);
    if (exact != body) return prefix + exact;
    struct Template {
        std::regex pattern;
        std::vector<int> order;
        std::string_view english;
    };
    static std::once_flag once;
    static std::vector<Template> templates;
    std::call_once(once, [] {
        for (std::size_t i = 0; i < catalog::englishCount; ++i) {
            std::string_view key = catalog::english[i].first;
            if (key.find('{') == std::string_view::npos || key.find("@@") != std::string_view::npos) continue;
            std::string expression = "^";
            std::vector<int> order;
            for (std::size_t c = 0; c < key.size(); ++c) {
                if (key[c] == '{' && c + 2 < key.size() && key[c + 2] == '}' && key[c + 1] >= '0' && key[c + 1] <= '9') {
                    expression += "(.+?)";
                    order.push_back(key[c + 1] - '0');
                    c += 2;
                } else if (std::strchr("\\^$.|?*+()[]{}", key[c])) expression += std::string("\\") + key[c];
                else expression += key[c];
            }
            templates.push_back({std::regex(expression + "$"), order, catalog::english[i].second});
        }
    });
    std::smatch match;
    for (const auto& candidate : templates)
        if (std::regex_match(body, match, candidate.pattern)) {
            std::vector<std::string> arguments(10);
            for (std::size_t g = 0; g < candidate.order.size(); ++g) arguments[static_cast<std::size_t>(candidate.order[g])] = match[g + 1].str();
            return prefix + format(candidate.english, arguments);
        }
    return std::string(message);
}

bool untranslated(std::string_view text) {
    if (language() == Language::italian || text.empty()) return false;
    for (std::size_t i = 0; i < catalog::englishCount; ++i) {
        std::string_view key = catalog::english[i].first;
        std::string_view source = key.substr(0, key.find("@@"));
        if (source == text && catalog::english[i].second != source) return true;
    }
    return false;
}

std::string format(std::string_view pattern, const std::vector<std::string>& arguments) {
    std::string out;
    out.reserve(pattern.size());
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        if (pattern[i] == '{' && i + 2 < pattern.size() && pattern[i + 2] == '}' && pattern[i + 1] >= '0' && pattern[i + 1] <= '9') {
            auto index = static_cast<std::size_t>(pattern[i + 1] - '0');
            if (index < arguments.size()) {
                out += arguments[index];
                i += 2;
                continue;
            }
        }
        out += pattern[i];
    }
    return out;
}

std::string number(long long value) { return grouped(value, language() == Language::italian ? '.' : ','); }

std::string decimal(double value, int digits) {
    if (!std::isfinite(value)) return "—";
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%.*f", std::max(0, digits), value);
    std::string text = buffer;
    auto point = text.find('.');
    std::string whole = text.substr(0, point), fraction = point == std::string::npos ? "" : text.substr(point + 1);
    bool negative = !whole.empty() && whole[0] == '-';
    std::string result = grouped(std::strtoll(whole.c_str() + (negative ? 1 : 0), nullptr, 10), language() == Language::italian ? '.' : ',');
    if (negative) result = "-" + result;
    if (!fraction.empty()) result += (language() == Language::italian ? "," : ".") + fraction;
    return result;
}

std::string date(std::string_view iso) {
    if (iso.size() < 10 || iso[4] != '-' || iso[7] != '-') return std::string(iso);
    int year = std::atoi(std::string(iso.substr(0, 4)).c_str()), month = std::atoi(std::string(iso.substr(5, 2)).c_str()), day = std::atoi(std::string(iso.substr(8, 2)).c_str());
    if (year <= 0 || month < 1 || month > 12 || day < 1 || day > 31) return std::string(iso);
    static const char* italian[] = {"gen", "feb", "mar", "apr", "mag", "giu", "lug", "ago", "set", "ott", "nov", "dic"};
    static const char* english[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    if (language() == Language::italian) return std::to_string(day) + " " + italian[month - 1] + " " + std::to_string(year);
    return std::string(english[month - 1]) + " " + std::to_string(day) + ", " + std::to_string(year);
}

std::string_view localizable(std::string_view key) { return key; }

std::size_t entries() { return catalog::englishCount; }

}
