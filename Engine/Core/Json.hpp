#pragma once

#include "Result.hpp"

#include <concepts>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace ld {

class Json {
public:
    using Array = std::vector<Json>;
    using Member = std::pair<std::string, Json>;
    using Object = std::vector<Member>;

    enum class Type { null, boolean, number, string, array, object };

    Json() = default;
    Json(std::nullptr_t) {}
    Json(bool value) : data(value) {}
    Json(int value) : data(static_cast<double>(value)) {}
    Json(long value) : data(static_cast<double>(value)) {}
    Json(long long value) : data(static_cast<double>(value)) {}
    Json(unsigned value) : data(static_cast<double>(value)) {}
    Json(unsigned long value) : data(static_cast<double>(value)) {}
    Json(double value) : data(value) {}
    Json(const char* value) : data(std::string(value)) {}
    Json(std::string value) : data(std::move(value)) {}
    Json(std::string_view value) : data(std::string(value)) {}
    Json(Array value) : data(std::make_shared<Array>(std::move(value))) {}
    Json(Object value) : data(std::make_shared<Object>(std::move(value))) {}

    static Json array(std::initializer_list<Json> items = {}) { return Json(Array(items)); }
    static Json object(std::initializer_list<Member> members = {}) { return Json(Object(members)); }

    Type type() const;
    bool isNull() const { return type() == Type::null; }
    bool isBool() const { return type() == Type::boolean; }
    bool isNumber() const { return type() == Type::number; }
    bool isString() const { return type() == Type::string; }
    bool isArray() const { return type() == Type::array; }
    bool isObject() const { return type() == Type::object; }

    bool boolean(bool fallback = false) const;
    double number(double fallback = 0) const;
    long long integer(long long fallback = 0) const;
    const std::string& string() const;
    std::string stringOr(std::string fallback) const;
    const Array& items() const;
    Array& items();
    const Object& members() const;
    Object& members();

    const Json* find(std::string_view key) const;
    Json* find(std::string_view key);
    bool contains(std::string_view key) const { return find(key) != nullptr; }
    template <typename Key>
        requires(std::is_convertible_v<const Key&, std::string_view> && !std::is_integral_v<Key>)
    const Json& operator[](const Key& key) const { return member(std::string_view(key)); }
    template <typename Key>
        requires(std::is_convertible_v<const Key&, std::string_view> && !std::is_integral_v<Key>)
    Json& operator[](const Key& key) { return member(std::string_view(key)); }
    template <std::integral Index>
    const Json& operator[](Index index) const { return element(static_cast<std::size_t>(index)); }
    const Json& member(std::string_view key) const;
    Json& member(std::string_view key);
    const Json& element(std::size_t index) const;
    std::size_t size() const;
    void set(std::string key, Json value);
    bool erase(std::string_view key);
    void push(Json value);

    std::optional<std::string> optionalString(std::string_view key) const;
    std::optional<double> optionalNumber(std::string_view key) const;
    std::optional<bool> optionalBool(std::string_view key) const;

    std::string dump(bool sortKeys = false) const;
    static Result<Json> parse(std::string_view text, std::size_t maximumDepth = 256);

    bool operator==(const Json& other) const;
    bool operator!=(const Json& other) const { return !(*this == other); }

private:
    std::variant<std::nullptr_t, bool, double, std::string, std::shared_ptr<Array>, std::shared_ptr<Object>> data;
    void detach();
    void write(std::string& out, bool sortKeys, int indent, int depth) const;
};

std::string formatNumber(double value);
std::optional<double> parseNumber(std::string_view text);
std::string escapeJson(std::string_view text);

}
