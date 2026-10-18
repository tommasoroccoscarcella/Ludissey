#include "Text.hpp"

#include <array>
#include <cctype>
#include <cstdio>
#include <random>
#include <xlocale.h>

namespace ld {

namespace {

locale_t cLocale() {
    static locale_t locale = newlocale(LC_ALL_MASK, "C", nullptr);
    return locale;
}

std::mt19937_64& generator() {
    static thread_local std::mt19937_64 engine([] {
        std::random_device device;
        std::seed_seq seed{device(), device(), device(), device(), device(), device(), device(), device()};
        return std::mt19937_64(seed);
    }());
    return engine;
}

constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

}

std::string makeUuid() {
    std::array<std::uint8_t, 16> bytes{};
    auto& engine = generator();
    for (std::size_t i = 0; i < bytes.size(); i += 8) {
        std::uint64_t value = engine();
        for (std::size_t j = 0; j < 8; ++j) bytes[i + j] = static_cast<std::uint8_t>(value >> (j * 8));
    }
    bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0F) | 0x40);
    bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3F) | 0x80);
    std::string out;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) out += '-';
        out += hexByte(bytes[i]);
    }
    return out;
}

bool isUuid(std::string_view text) {
    if (text.size() != 36) return false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (c != '-') return false;
        } else if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    }
    return true;
}

std::string lowercase(std::string_view text) {
    std::string out(text);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string uppercase(std::string_view text) {
    std::string out(text);
    for (char& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

std::string trim(std::string_view text) {
    std::size_t start = 0, end = text.size();
    while (start < end && std::isspace(static_cast<unsigned char>(text[start]))) ++start;
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
    return std::string(text.substr(start, end - start));
}

bool startsWith(std::string_view text, std::string_view prefix) { return text.substr(0, prefix.size()) == prefix; }
bool endsWith(std::string_view text, std::string_view suffix) { return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix; }

std::vector<std::string> split(std::string_view text, char separator) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (true) {
        auto next = text.find(separator, start);
        if (next == std::string_view::npos) { parts.emplace_back(text.substr(start)); break; }
        parts.emplace_back(text.substr(start, next - start));
        start = next + 1;
    }
    return parts;
}

std::string join(const std::vector<std::string>& parts, std::string_view separator) {
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i) out += separator;
        out += parts[i];
    }
    return out;
}

std::string replaceAll(std::string text, std::string_view from, std::string_view to) {
    if (from.empty()) return text;
    std::size_t position = 0;
    while ((position = text.find(from, position)) != std::string::npos) {
        text.replace(position, from.size(), to);
        position += to.size();
    }
    return text;
}

std::vector<std::uint32_t> utf8Decode(std::string_view text) {
    std::vector<std::uint32_t> codes;
    for (std::size_t i = 0; i < text.size();) {
        auto c = static_cast<unsigned char>(text[i]);
        std::uint32_t code;
        std::size_t length;
        if (c < 0x80) { code = c; length = 1; }
        else if ((c >> 5) == 6) { code = c & 0x1F; length = 2; }
        else if ((c >> 4) == 14) { code = c & 0x0F; length = 3; }
        else if ((c >> 3) == 30) { code = c & 0x07; length = 4; }
        else { codes.push_back(0xFFFD); ++i; continue; }
        if (i + length > text.size()) { codes.push_back(0xFFFD); break; }
        bool valid = true;
        for (std::size_t j = 1; j < length; ++j) {
            auto continuation = static_cast<unsigned char>(text[i + j]);
            if ((continuation >> 6) != 2) { valid = false; break; }
            code = (code << 6) | (continuation & 0x3F);
        }
        if (!valid) { codes.push_back(0xFFFD); ++i; continue; }
        codes.push_back(code);
        i += length;
    }
    return codes;
}

std::string utf8Encode(const std::vector<std::uint32_t>& codes) {
    std::string out;
    for (auto code : codes) {
        if (code < 0x80) out += static_cast<char>(code);
        else if (code < 0x800) { out += static_cast<char>(0xC0 | (code >> 6)); out += static_cast<char>(0x80 | (code & 0x3F)); }
        else if (code < 0x10000) { out += static_cast<char>(0xE0 | (code >> 12)); out += static_cast<char>(0x80 | ((code >> 6) & 0x3F)); out += static_cast<char>(0x80 | (code & 0x3F)); }
        else { out += static_cast<char>(0xF0 | (code >> 18)); out += static_cast<char>(0x80 | ((code >> 12) & 0x3F)); out += static_cast<char>(0x80 | ((code >> 6) & 0x3F)); out += static_cast<char>(0x80 | (code & 0x3F)); }
    }
    return out;
}

std::size_t utf8Length(std::string_view text) {
    std::size_t count = 0;
    for (char c : text)
        if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++count;
    return count;
}

std::string utf8Prefix(std::string_view text, std::size_t characters) {
    std::size_t count = 0, i = 0;
    for (; i < text.size(); ++i) {
        if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80) {
            if (count == characters) break;
            ++count;
        }
    }
    return std::string(text.substr(0, i));
}

std::string base64Encode(const std::uint8_t* data, std::size_t size) {
    std::string out;
    out.reserve((size + 2) / 3 * 4);
    for (std::size_t i = 0; i < size; i += 3) {
        std::uint32_t chunk = static_cast<std::uint32_t>(data[i]) << 16;
        if (i + 1 < size) chunk |= static_cast<std::uint32_t>(data[i + 1]) << 8;
        if (i + 2 < size) chunk |= data[i + 2];
        out += alphabet[(chunk >> 18) & 63];
        out += alphabet[(chunk >> 12) & 63];
        out += i + 1 < size ? alphabet[(chunk >> 6) & 63] : '=';
        out += i + 2 < size ? alphabet[chunk & 63] : '=';
    }
    return out;
}

std::string base64Encode(std::string_view data) { return base64Encode(reinterpret_cast<const std::uint8_t*>(data.data()), data.size()); }

std::optional<std::vector<std::uint8_t>> base64Decode(std::string_view text) {
    if (text.size() % 4 != 0) return std::nullopt;
    std::vector<std::uint8_t> out;
    out.reserve(text.size() / 4 * 3);
    auto value = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    for (std::size_t i = 0; i < text.size(); i += 4) {
        int a = value(text[i]), b = value(text[i + 1]);
        bool lastGroup = i + 4 == text.size();
        int c = text[i + 2] == '=' && lastGroup ? -2 : value(text[i + 2]);
        int d = text[i + 3] == '=' && lastGroup ? -2 : value(text[i + 3]);
        if (a < 0 || b < 0 || c == -1 || d == -1 || (c == -2 && d != -2)) return std::nullopt;
        std::uint32_t chunk = static_cast<std::uint32_t>(a) << 18 | static_cast<std::uint32_t>(b) << 12 | static_cast<std::uint32_t>(c < 0 ? 0 : c) << 6 | static_cast<std::uint32_t>(d < 0 ? 0 : d);
        out.push_back(static_cast<std::uint8_t>(chunk >> 16));
        if (c >= 0) out.push_back(static_cast<std::uint8_t>(chunk >> 8));
        if (d >= 0) out.push_back(static_cast<std::uint8_t>(chunk));
    }
    return out;
}

std::string sha256Hex(const std::uint8_t* data, std::size_t size) {
    static constexpr std::uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    std::uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    auto rotr = [](std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };
    std::vector<std::uint8_t> message(data, data + size);
    std::uint64_t bits = static_cast<std::uint64_t>(size) * 8;
    message.push_back(0x80);
    while (message.size() % 64 != 56) message.push_back(0);
    for (int i = 7; i >= 0; --i) message.push_back(static_cast<std::uint8_t>(bits >> (i * 8)));
    for (std::size_t block = 0; block < message.size(); block += 64) {
        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = static_cast<std::uint32_t>(message[block + i * 4]) << 24 | static_cast<std::uint32_t>(message[block + i * 4 + 1]) << 16 |
                   static_cast<std::uint32_t>(message[block + i * 4 + 2]) << 8 | message[block + i * 4 + 3];
        for (int i = 16; i < 64; ++i) {
            std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            std::uint32_t ch = (e & f) ^ (~e & g);
            std::uint32_t t1 = hh + s1 + ch + k[i] + w[i];
            std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            std::uint32_t t2 = s0 + maj;
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    std::string out;
    for (auto word : h)
        for (int i = 3; i >= 0; --i) out += hexByte((word >> (i * 8)) & 255);
    return out;
}

std::string sha256Hex(std::string_view data) { return sha256Hex(reinterpret_cast<const std::uint8_t*>(data.data()), data.size()); }

std::string hexByte(unsigned value) {
    static constexpr char digits[] = "0123456789abcdef";
    return {digits[(value >> 4) & 15], digits[value & 15]};
}

std::string formatFixed(double value, int decimals) {
    char buffer[64];
    snprintf_l(buffer, sizeof buffer, cLocale(), "%.*f", decimals, value);
    return buffer;
}

std::string formatGeneral(double value) {
    char buffer[64];
    snprintf_l(buffer, sizeof buffer, cLocale(), "%g", value);
    return buffer;
}

std::string doubleText(double value) {
    if (std::isnan(value)) return "nan";
    if (std::isinf(value)) return value < 0 ? "-inf" : "inf";
    static locale_t c = newlocale(LC_ALL_MASK, "C", nullptr);
    char buffer[64];
    for (int precision = 1; precision <= 17; ++precision) {
        snprintf_l(buffer, sizeof buffer, c, "%.*g", precision, value);
        if (strtod_l(buffer, nullptr, c) == value) break;
    }
    std::string text = buffer;
    double magnitude = std::fabs(value);
    bool exponential = magnitude != 0 && (magnitude >= 1e16 || magnitude < 1e-4);
    if (!exponential) {
        auto e = text.find('e');
        if (e != std::string::npos) {
            snprintf_l(buffer, sizeof buffer, c, "%.17f", value);
            text = buffer;
            for (int precision = 0; precision <= 17; ++precision) {
                snprintf_l(buffer, sizeof buffer, c, "%.*f", precision, value);
                if (strtod_l(buffer, nullptr, c) == value) { text = buffer; break; }
            }
        }
        if (text.find('.') == std::string::npos) text += ".0";
        return text;
    }
    auto e = text.find('e');
    if (e == std::string::npos) return text;
    std::string mantissa = text.substr(0, e), exponent = text.substr(e + 1);
    bool negative = exponent[0] == '-';
    std::size_t digits = exponent.find_first_not_of("+-0");
    std::string cleaned = digits == std::string::npos ? "0" : exponent.substr(digits);
    return mantissa + "e" + (negative ? "-" : "+") + (cleaned.size() < 2 ? "0" + cleaned : cleaned);
}

}
