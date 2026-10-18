#include "Json.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <xlocale.h>

namespace ld {

namespace {

const std::string emptyString;
const Json::Array emptyArray;
const Json::Object emptyObject;
const Json nullJson;

locale_t cLocale() {
    static locale_t locale = newlocale(LC_ALL_MASK, "C", nullptr);
    return locale;
}

void appendUtf8(std::string& out, std::uint32_t code) {
    if (code < 0x80) out += static_cast<char>(code);
    else if (code < 0x800) {
        out += static_cast<char>(0xC0 | (code >> 6));
        out += static_cast<char>(0x80 | (code & 0x3F));
    } else if (code < 0x10000) {
        out += static_cast<char>(0xE0 | (code >> 12));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (code >> 18));
        out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    }
}

class Parser {
public:
    Parser(std::string_view text, std::size_t maximumDepth) : text(text), maximumDepth(maximumDepth) {}

    Result<Json> run() {
        skip();
        auto value = parseValue(0);
        if (!value) return value;
        skip();
        if (position != text.size()) return failure("caratteri in eccesso");
        return value;
    }

private:
    std::string_view text;
    std::size_t position = 0;
    std::size_t maximumDepth;

    Error failure(const std::string& reason) const { return fail(ErrorCode::corrupted, "JSON non valido alla posizione " + std::to_string(position) + ": " + reason); }

    void skip() {
        while (position < text.size() && (text[position] == ' ' || text[position] == '\n' || text[position] == '\r' || text[position] == '\t')) ++position;
    }

    bool literal(std::string_view word) {
        if (text.substr(position, word.size()) != word) return false;
        position += word.size();
        return true;
    }

    Result<Json> parseValue(std::size_t depth) {
        if (depth > maximumDepth) return failure("annidamento eccessivo");
        if (position >= text.size()) return failure("fine inattesa");
        char c = text[position];
        if (c == '{') return parseObject(depth);
        if (c == '[') return parseArray(depth);
        if (c == '"') {
            auto s = parseString();
            if (!s) return s.error();
            return Json(std::move(*s));
        }
        if (literal("true")) return Json(true);
        if (literal("false")) return Json(false);
        if (literal("null")) return Json(nullptr);
        if (c == '-' || (c >= '0' && c <= '9')) return parseNumberValue();
        return failure("valore inatteso");
    }

    Result<Json> parseNumberValue() {
        std::size_t start = position;
        if (text[position] == '-') ++position;
        if (position >= text.size()) return failure("numero incompleto");
        if (text[position] == '0') ++position;
        else if (text[position] >= '1' && text[position] <= '9') {
            while (position < text.size() && std::isdigit(static_cast<unsigned char>(text[position]))) ++position;
        } else return failure("numero non valido");
        if (position < text.size() && text[position] == '.') {
            ++position;
            if (position >= text.size() || !std::isdigit(static_cast<unsigned char>(text[position]))) return failure("decimali mancanti");
            while (position < text.size() && std::isdigit(static_cast<unsigned char>(text[position]))) ++position;
        }
        if (position < text.size() && (text[position] == 'e' || text[position] == 'E')) {
            ++position;
            if (position < text.size() && (text[position] == '+' || text[position] == '-')) ++position;
            if (position >= text.size() || !std::isdigit(static_cast<unsigned char>(text[position]))) return failure("esponente mancante");
            while (position < text.size() && std::isdigit(static_cast<unsigned char>(text[position]))) ++position;
        }
        auto value = parseNumber(text.substr(start, position - start));
        if (!value || !std::isfinite(*value)) return failure("numero fuori intervallo");
        return Json(*value);
    }

    Result<std::string> parseString() {
        ++position;
        std::string out;
        while (true) {
            if (position >= text.size()) return failure("stringa non terminata");
            char c = text[position++];
            if (c == '"') return out;
            if (static_cast<unsigned char>(c) < 0x20) return failure("carattere di controllo nella stringa");
            if (c != '\\') { out += c; continue; }
            if (position >= text.size()) return failure("escape incompleto");
            char e = text[position++];
            switch (e) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                auto first = hex4();
                if (!first) return failure("escape unicode non valido");
                std::uint32_t code = *first;
                if (code >= 0xD800 && code <= 0xDBFF) {
                    if (!literal("\\u")) return failure("surrogato incompleto");
                    auto second = hex4();
                    if (!second || *second < 0xDC00 || *second > 0xDFFF) return failure("surrogato non valido");
                    code = 0x10000 + ((code - 0xD800) << 10) + (*second - 0xDC00);
                } else if (code >= 0xDC00 && code <= 0xDFFF) return failure("surrogato isolato");
                appendUtf8(out, code);
                break;
            }
            default: return failure("escape sconosciuto");
            }
        }
    }

    std::optional<std::uint32_t> hex4() {
        if (position + 4 > text.size()) return std::nullopt;
        std::uint32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            char c = text[position++];
            value <<= 4;
            if (c >= '0' && c <= '9') value |= static_cast<std::uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') value |= static_cast<std::uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') value |= static_cast<std::uint32_t>(c - 'A' + 10);
            else return std::nullopt;
        }
        return value;
    }

    Result<Json> parseArray(std::size_t depth) {
        ++position;
        Json::Array items;
        skip();
        if (position < text.size() && text[position] == ']') { ++position; return Json(std::move(items)); }
        while (true) {
            skip();
            auto value = parseValue(depth + 1);
            if (!value) return value;
            items.push_back(std::move(*value));
            skip();
            if (position >= text.size()) return failure("array non terminato");
            if (text[position] == ',') { ++position; continue; }
            if (text[position] == ']') { ++position; return Json(std::move(items)); }
            return failure("separatore mancante nell'array");
        }
    }

    Result<Json> parseObject(std::size_t depth) {
        ++position;
        Json result = Json::object();
        skip();
        if (position < text.size() && text[position] == '}') { ++position; return result; }
        while (true) {
            skip();
            if (position >= text.size() || text[position] != '"') return failure("chiave attesa");
            auto key = parseString();
            if (!key) return key.error();
            skip();
            if (position >= text.size() || text[position] != ':') return failure("due punti attesi");
            ++position;
            skip();
            auto value = parseValue(depth + 1);
            if (!value) return value;
            result.set(std::move(*key), std::move(*value));
            skip();
            if (position >= text.size()) return failure("oggetto non terminato");
            if (text[position] == ',') { ++position; continue; }
            if (text[position] == '}') { ++position; return result; }
            return failure("separatore mancante nell'oggetto");
        }
    }
};

}

std::optional<double> parseNumber(std::string_view text) {
    if (text.empty() || text.size() > 64) return std::nullopt;
    std::string copy(text);
    char* end = nullptr;
    double value = strtod_l(copy.c_str(), &end, cLocale());
    if (end != copy.c_str() + copy.size()) return std::nullopt;
    return value;
}

std::string formatNumber(double value) {
    if (!std::isfinite(value)) return "null";
    if (value == 0) return "0";
    if (std::floor(value) == value && std::abs(value) < 1e15) {
        char buffer[32];
        snprintf_l(buffer, sizeof buffer, cLocale(), "%.0f", value);
        return buffer;
    }
    char buffer[40];
    for (int precision = 1; precision <= 17; ++precision) {
        snprintf_l(buffer, sizeof buffer, cLocale(), "%.*g", precision, value);
        if (strtod_l(buffer, nullptr, cLocale()) == value) break;
    }
    std::string out(buffer);
    auto e = out.find('e');
    if (e != std::string::npos) {
        std::string mantissa = out.substr(0, e);
        std::string exponent = out.substr(e + 1);
        bool negative = !exponent.empty() && exponent[0] == '-';
        std::size_t start = (exponent[0] == '-' || exponent[0] == '+') ? 1 : 0;
        while (start + 1 < exponent.size() && exponent[start] == '0') ++start;
        out = mantissa + "e" + (negative ? "-" : "+") + exponent.substr(start);
    }
    return out;
}

std::string escapeJson(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 2);
    out += '"';
    for (char c : text) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buffer[8];
                snprintf(buffer, sizeof buffer, "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
                out += buffer;
            } else out += c;
        }
    }
    out += '"';
    return out;
}

Json::Type Json::type() const {
    switch (data.index()) {
    case 0: return Type::null;
    case 1: return Type::boolean;
    case 2: return Type::number;
    case 3: return Type::string;
    case 4: return Type::array;
    default: return Type::object;
    }
}

bool Json::boolean(bool fallback) const { return isBool() ? std::get<bool>(data) : fallback; }
double Json::number(double fallback) const { return isNumber() ? std::get<double>(data) : fallback; }
long long Json::integer(long long fallback) const {
    if (!isNumber()) return fallback;
    double value = std::get<double>(data);
    if (!std::isfinite(value) || std::abs(value) > 9.0e18) return fallback;
    return static_cast<long long>(value);
}
const std::string& Json::string() const { return isString() ? std::get<std::string>(data) : emptyString; }
std::string Json::stringOr(std::string fallback) const { return isString() ? std::get<std::string>(data) : std::move(fallback); }
const Json::Array& Json::items() const { return isArray() ? *std::get<std::shared_ptr<Array>>(data) : emptyArray; }
const Json::Object& Json::members() const { return isObject() ? *std::get<std::shared_ptr<Object>>(data) : emptyObject; }

void Json::detach() {
    if (isArray()) {
        auto& pointer = std::get<std::shared_ptr<Array>>(data);
        if (pointer.use_count() > 1) pointer = std::make_shared<Array>(*pointer);
    } else if (isObject()) {
        auto& pointer = std::get<std::shared_ptr<Object>>(data);
        if (pointer.use_count() > 1) pointer = std::make_shared<Object>(*pointer);
    }
}

Json::Array& Json::items() {
    if (!isArray()) data = std::make_shared<Array>();
    detach();
    return *std::get<std::shared_ptr<Array>>(data);
}

Json::Object& Json::members() {
    if (!isObject()) data = std::make_shared<Object>();
    detach();
    return *std::get<std::shared_ptr<Object>>(data);
}

const Json* Json::find(std::string_view key) const {
    if (!isObject()) return nullptr;
    for (const auto& member : members())
        if (member.first == key) return &member.second;
    return nullptr;
}

Json* Json::find(std::string_view key) {
    if (!isObject()) return nullptr;
    for (auto& member : members())
        if (member.first == key) return &member.second;
    return nullptr;
}

const Json& Json::member(std::string_view key) const {
    const Json* found = find(key);
    return found ? *found : nullJson;
}

Json& Json::member(std::string_view key) {
    if (Json* found = find(key)) return *found;
    auto& list = members();
    list.emplace_back(std::string(key), Json());
    return list.back().second;
}

const Json& Json::element(std::size_t index) const {
    const auto& list = items();
    return index < list.size() ? list[index] : nullJson;
}

std::size_t Json::size() const {
    if (isArray()) return items().size();
    if (isObject()) return members().size();
    return 0;
}

void Json::set(std::string key, Json value) {
    if (Json* found = find(key)) { *found = std::move(value); return; }
    members().emplace_back(std::move(key), std::move(value));
}

bool Json::erase(std::string_view key) {
    if (!isObject()) return false;
    auto& list = members();
    auto it = std::find_if(list.begin(), list.end(), [&](const Member& m) { return m.first == key; });
    if (it == list.end()) return false;
    list.erase(it);
    return true;
}

void Json::push(Json value) { items().push_back(std::move(value)); }

std::optional<std::string> Json::optionalString(std::string_view key) const {
    const Json* value = find(key);
    if (!value || !value->isString()) return std::nullopt;
    return value->string();
}

std::optional<double> Json::optionalNumber(std::string_view key) const {
    const Json* value = find(key);
    if (!value || !value->isNumber()) return std::nullopt;
    return value->number();
}

std::optional<bool> Json::optionalBool(std::string_view key) const {
    const Json* value = find(key);
    if (!value || !value->isBool()) return std::nullopt;
    return value->boolean();
}

void Json::write(std::string& out, bool sortKeys, int indent, int depth) const {
    auto newline = [&](int level) {
        if (indent <= 0) return;
        out += '\n';
        out.append(static_cast<std::size_t>(level * indent), ' ');
    };
    switch (type()) {
    case Type::null: out += "null"; break;
    case Type::boolean: out += boolean() ? "true" : "false"; break;
    case Type::number: out += formatNumber(number()); break;
    case Type::string: out += escapeJson(string()); break;
    case Type::array: {
        const auto& list = items();
        out += '[';
        for (std::size_t i = 0; i < list.size(); ++i) {
            if (i) out += ',';
            newline(depth + 1);
            list[i].write(out, sortKeys, indent, depth + 1);
        }
        if (!list.empty()) newline(depth);
        out += ']';
        break;
    }
    case Type::object: {
        std::vector<const Member*> order;
        for (const auto& member : members()) order.push_back(&member);
        if (sortKeys) std::sort(order.begin(), order.end(), [](const Member* a, const Member* b) { return a->first < b->first; });
        out += '{';
        for (std::size_t i = 0; i < order.size(); ++i) {
            if (i) out += ',';
            newline(depth + 1);
            out += escapeJson(order[i]->first);
            out += indent > 0 ? ": " : ":";
            order[i]->second.write(out, sortKeys, indent, depth + 1);
        }
        if (!order.empty()) newline(depth);
        out += '}';
        break;
    }
    }
}

std::string Json::dump(bool sortKeys) const {
    std::string out;
    write(out, sortKeys, 0, 0);
    return out;
}

Result<Json> Json::parse(std::string_view text, std::size_t maximumDepth) { return Parser(text, maximumDepth).run(); }

bool Json::operator==(const Json& other) const {
    if (type() != other.type()) return false;
    switch (type()) {
    case Type::null: return true;
    case Type::boolean: return boolean() == other.boolean();
    case Type::number: return number() == other.number();
    case Type::string: return string() == other.string();
    case Type::array: return items() == other.items();
    case Type::object: {
        if (members().size() != other.members().size()) return false;
        for (const auto& member : members()) {
            const Json* match = other.find(member.first);
            if (!match || *match != member.second) return false;
        }
        return true;
    }
    }
    return false;
}

}
