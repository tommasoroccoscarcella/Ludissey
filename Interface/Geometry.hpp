#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace ui {

inline constexpr double infinity = std::numeric_limits<double>::infinity();
inline constexpr double pi = 3.14159265358979323846;

struct Point {
    double x = 0, y = 0;
    Point operator+(Point o) const { return {x + o.x, y + o.y}; }
    Point operator-(Point o) const { return {x - o.x, y - o.y}; }
    bool operator==(const Point&) const = default;
};

struct Size {
    double width = 0, height = 0;
    bool operator==(const Size&) const = default;
};

struct PathStep {
    char kind;
    double x1, y1, x2, y2, x, y;
};

struct Frame {
    double x = 0, y = 0, width = 0, height = 0;
    double maxX() const { return x + width; }
    double maxY() const { return y + height; }
    double midX() const { return x + width / 2; }
    double midY() const { return y + height / 2; }
    bool contains(Point p) const { return p.x >= x && p.y >= y && p.x < x + width && p.y < y + height; }
    Frame inset(double dx, double dy) const { return {x + dx, y + dy, std::max(0.0, width - 2 * dx), std::max(0.0, height - 2 * dy)}; }
    Frame intersection(const Frame& o) const {
        double l = std::max(x, o.x), t = std::max(y, o.y), r = std::min(maxX(), o.maxX()), b = std::min(maxY(), o.maxY());
        if (r <= l || b <= t) return {l, t, 0, 0};
        return {l, t, r - l, b - t};
    }
    bool empty() const { return width <= 0 || height <= 0; }
    bool operator==(const Frame&) const = default;
};

struct Insets {
    double top = 0, leading = 0, bottom = 0, trailing = 0;
    static Insets all(double v) { return {v, v, v, v}; }
    static Insets horizontal(double v) { return {0, v, 0, v}; }
    static Insets vertical(double v) { return {v, 0, v, 0}; }
};

struct Color {
    double r = 0, g = 0, b = 0, a = 1;
    static Color hex(std::string_view text, double alpha = 1) {
        if (!text.empty() && text[0] == '#') text.remove_prefix(1);
        unsigned value = 0;
        for (char c : text) {
            value <<= 4;
            if (c >= '0' && c <= '9') value |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') value |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') value |= static_cast<unsigned>(c - 'A' + 10);
        }
        return {((value >> 16) & 255) / 255.0, ((value >> 8) & 255) / 255.0, (value & 255) / 255.0, alpha};
    }
    static Color white(double alpha = 1) { return {1, 1, 1, alpha}; }
    static Color black(double alpha = 1) { return {0, 0, 0, alpha}; }
    static Color clear() { return {0, 0, 0, 0}; }
    Color opacity(double alpha) const { return {r, g, b, a * alpha}; }
    bool visible() const { return a > 0; }
    bool operator==(const Color&) const = default;
};

enum class Weight { regular, bold, semibold, medium, light };

struct Font {
    std::string family;
    double size = 13;
    Weight weight = Weight::regular;
    bool monospaced = false;
    static Font named(std::string family, double size) { return {std::move(family), size, Weight::regular, false}; }
    static Font system(double size, Weight weight = Weight::regular) { return {"", size, weight, false}; }
    static Font mono(double size) { return {"Menlo", size, Weight::regular, true}; }
    bool operator==(const Font&) const = default;
};

enum class HAlign { leading, center, trailing };
enum class VAlign { top, center, bottom };

struct Alignment {
    HAlign horizontal = HAlign::center;
    VAlign vertical = VAlign::center;
    static Alignment center() { return {}; }
    static Alignment leading() { return {HAlign::leading, VAlign::center}; }
    static Alignment trailing() { return {HAlign::trailing, VAlign::center}; }
    static Alignment top() { return {HAlign::center, VAlign::top}; }
    static Alignment bottom() { return {HAlign::center, VAlign::bottom}; }
    static Alignment topLeading() { return {HAlign::leading, VAlign::top}; }
    static Alignment topTrailing() { return {HAlign::trailing, VAlign::top}; }
    static Alignment bottomLeading() { return {HAlign::leading, VAlign::bottom}; }
    static Alignment bottomTrailing() { return {HAlign::trailing, VAlign::bottom}; }
};

inline double align(HAlign a, double container, double content) { return a == HAlign::leading ? 0 : a == HAlign::center ? (container - content) / 2 : container - content; }
inline double align(VAlign a, double container, double content) { return a == VAlign::top ? 0 : a == VAlign::center ? (container - content) / 2 : container - content; }

struct Proposal {
    std::optional<double> width;
    std::optional<double> height;
    static Proposal unspecified() { return {}; }
    static Proposal of(double w, double h) { return {w, h}; }
};

}
