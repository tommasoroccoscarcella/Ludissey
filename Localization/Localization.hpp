#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ld {

enum class Language { italian, english };

namespace localization {

Language language();
void setLanguage(std::optional<Language> forced);
std::optional<Language> parseLanguage(std::string_view code);
std::string_view code();
std::string translate(std::string_view key);
std::string translateMessage(std::string_view message);
bool untranslated(std::string_view text);
std::string format(std::string_view pattern, const std::vector<std::string>& arguments);
std::string number(long long value);
std::string decimal(double value, int digits);
std::string date(std::string_view iso);
std::string_view localizable(std::string_view key);
std::size_t entries();

inline std::string argument(const std::string& value) { return value; }
inline std::string argument(std::string_view value) { return std::string(value); }
inline std::string argument(const char* value) { return value ? value : ""; }
inline std::string argument(int value) { return std::to_string(value); }
inline std::string argument(long value) { return std::to_string(value); }
inline std::string argument(long long value) { return std::to_string(value); }
inline std::string argument(unsigned value) { return std::to_string(value); }
inline std::string argument(unsigned long value) { return std::to_string(value); }
inline std::string argument(unsigned long long value) { return std::to_string(value); }

}

template <class... Arguments>
std::string tr(std::string_view key, const Arguments&... arguments) {
    if constexpr (sizeof...(Arguments) == 0) return localization::translate(key);
    else return localization::format(localization::translate(key), {localization::argument(arguments)...});
}

template <class... Arguments>
std::string wording(std::string_view key, const Arguments&... arguments) {
    if constexpr (sizeof...(Arguments) == 0) return std::string(key);
    else return localization::format(key, {localization::argument(arguments)...});
}

template <class... Arguments>
std::string trn(std::string_view one, std::string_view many, long long count, const Arguments&... arguments) {
    return localization::format(localization::translate(count == 1 ? one : many), {localization::number(count), localization::argument(arguments)...});
}

}
