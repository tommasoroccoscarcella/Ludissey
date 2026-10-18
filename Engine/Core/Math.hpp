#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <string_view>

namespace ld {

constexpr double pi = std::numbers::pi;

struct Vec2 {
    double x = 0, y = 0;
    constexpr Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    constexpr Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    constexpr Vec2 operator*(double s) const { return {x * s, y * s}; }
    constexpr bool operator==(const Vec2&) const = default;
    double length() const { return std::sqrt(x * x + y * y); }
};

struct Vec3 {
    double x = 0, y = 0, z = 0;
    constexpr Vec3 operator+(Vec3 o) const { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Vec3 operator-(Vec3 o) const { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Vec3 operator-() const { return {-x, -y, -z}; }
    constexpr Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
    constexpr Vec3 operator/(double s) const { return {x / s, y / s, z / s}; }
    constexpr Vec3& operator+=(Vec3 o) { x += o.x; y += o.y; z += o.z; return *this; }
    constexpr Vec3& operator-=(Vec3 o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    constexpr bool operator==(const Vec3&) const = default;
    constexpr double operator[](int axis) const { return axis == 0 ? x : axis == 1 ? y : z; }
    constexpr double& operator[](int axis) { return axis == 0 ? x : axis == 1 ? y : z; }
    double length() const { return std::sqrt(x * x + y * y + z * z); }
    constexpr double lengthSquared() const { return x * x + y * y + z * z; }
    Vec3 normalized() const { double l = length(); return l > 1e-12 ? *this / l : Vec3{}; }
};

constexpr double dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
constexpr Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
constexpr Vec3 lerp(Vec3 a, Vec3 b, double t) { return a + (b - a) * t; }
inline double distance(Vec3 a, Vec3 b) { return (a - b).length(); }

struct Vec4 {
    double x = 0, y = 0, z = 0, w = 0;
    constexpr bool operator==(const Vec4&) const = default;
};

struct Mat3 {
    std::array<std::array<double, 3>, 3> m{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
    constexpr const std::array<double, 3>& operator[](int row) const { return m[row]; }
    constexpr std::array<double, 3>& operator[](int row) { return m[row]; }
    constexpr Mat3 operator*(const Mat3& b) const {
        Mat3 r;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) r.m[i][j] = m[i][0] * b.m[0][j] + m[i][1] * b.m[1][j] + m[i][2] * b.m[2][j];
        return r;
    }
    constexpr Vec3 operator*(Vec3 v) const {
        return {m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z, m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z, m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z};
    }
    constexpr Vec3 column(int c) const { return {m[0][c], m[1][c], m[2][c]}; }
    constexpr Mat3 transposed() const {
        Mat3 r;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) r.m[i][j] = m[j][i];
        return r;
    }
};

struct Quaternion {
    double x = 0, y = 0, z = 0, w = 1;
    static Quaternion axisAngle(Vec3 axis, double radians) {
        Vec3 a = axis.normalized();
        double s = std::sin(radians / 2);
        return {a.x * s, a.y * s, a.z * s, std::cos(radians / 2)};
    }
    static Quaternion between(Vec3 from, Vec3 to) {
        Vec3 f = from.normalized(), t = to.normalized();
        double d = dot(f, t);
        if (d < -0.999999) {
            Vec3 axis = cross({1, 0, 0}, f);
            if (axis.length() < 1e-6) axis = cross({0, 0, 1}, f);
            return axisAngle(axis, pi);
        }
        Vec3 c = cross(f, t);
        Quaternion q{c.x, c.y, c.z, 1 + d};
        double l = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
        return {q.x / l, q.y / l, q.z / l, q.w / l};
    }
    Quaternion operator*(const Quaternion& b) const {
        return {w * b.x + x * b.w + y * b.z - z * b.y, w * b.y - x * b.z + y * b.w + z * b.x, w * b.z + x * b.y - y * b.x + z * b.w, w * b.w - x * b.x - y * b.y - z * b.z};
    }
    Mat3 matrix() const {
        Mat3 r;
        r.m = {{{1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)},
                {2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)},
                {2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)}}};
        return r;
    }
    Vec3 rotate(Vec3 v) const { return matrix() * v; }
};

struct Mat4 {
    std::array<double, 16> m{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    constexpr double& at(int row, int column) { return m[column * 4 + row]; }
    constexpr double at(int row, int column) const { return m[column * 4 + row]; }
    constexpr Mat4 operator*(const Mat4& b) const {
        Mat4 r;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) {
                double sum = 0;
                for (int k = 0; k < 4; ++k) sum += at(i, k) * b.at(k, j);
                r.at(i, j) = sum;
            }
        return r;
    }
    constexpr Vec4 operator*(Vec4 v) const {
        return {at(0, 0) * v.x + at(0, 1) * v.y + at(0, 2) * v.z + at(0, 3) * v.w, at(1, 0) * v.x + at(1, 1) * v.y + at(1, 2) * v.z + at(1, 3) * v.w,
                at(2, 0) * v.x + at(2, 1) * v.y + at(2, 2) * v.z + at(2, 3) * v.w, at(3, 0) * v.x + at(3, 1) * v.y + at(3, 2) * v.z + at(3, 3) * v.w};
    }
    static Mat4 translation(Vec3 t) { Mat4 r; r.at(0, 3) = t.x; r.at(1, 3) = t.y; r.at(2, 3) = t.z; return r; }
    static Mat4 scale(Vec3 s) { Mat4 r; r.at(0, 0) = s.x; r.at(1, 1) = s.y; r.at(2, 2) = s.z; return r; }
    static Mat4 rotation(const Mat3& a) {
        Mat4 r;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) r.at(i, j) = a.m[i][j];
        return r;
    }
    static Mat4 perspective(double fovY, double aspect, double near, double far) {
        Mat4 r;
        double f = 1 / std::tan(fovY / 2);
        r.m.fill(0);
        r.at(0, 0) = f / aspect;
        r.at(1, 1) = f;
        r.at(2, 2) = far / (near - far);
        r.at(2, 3) = near * far / (near - far);
        r.at(3, 2) = -1;
        return r;
    }
    static Mat4 lookAt(Vec3 eye, Vec3 target, Vec3 up) {
        Vec3 f = (target - eye).normalized();
        Vec3 s = cross(f, up).normalized();
        if (s.length() < 1e-9) s = cross(f, Vec3{0, 0, 1}).normalized();
        Vec3 u = cross(s, f);
        Mat4 r;
        r.at(0, 0) = s.x; r.at(0, 1) = s.y; r.at(0, 2) = s.z; r.at(0, 3) = -dot(s, eye);
        r.at(1, 0) = u.x; r.at(1, 1) = u.y; r.at(1, 2) = u.z; r.at(1, 3) = -dot(u, eye);
        r.at(2, 0) = -f.x; r.at(2, 1) = -f.y; r.at(2, 2) = -f.z; r.at(2, 3) = dot(f, eye);
        return r;
    }
    Mat4 inverted() const;
    Vec3 transformPoint(Vec3 p) const {
        Vec4 r = *this * Vec4{p.x, p.y, p.z, 1};
        return std::abs(r.w) > 1e-12 ? Vec3{r.x / r.w, r.y / r.w, r.z / r.w} : Vec3{r.x, r.y, r.z};
    }
};

struct Transform {
    Vec3 position;
    Mat3 rotation;
    Vec3 scale{1, 1, 1};
    Mat4 matrix() const { return Mat4::translation(position) * Mat4::rotation(rotation) * Mat4::scale(scale); }
};

struct Ray {
    Vec3 origin;
    Vec3 direction;
    Vec3 at(double t) const { return origin + direction * t; }
};

struct Plane {
    Vec3 normal{0, 1, 0};
    double offset = 0;
    double signedDistance(Vec3 p) const { return dot(normal, p) - offset; }
};

struct Bounds {
    Vec3 low, high;
    constexpr Vec3 center() const { return (low + high) * 0.5; }
    constexpr Vec3 extent() const { return high - low; }
    constexpr bool contains(Vec3 p) const { return p.x >= low.x && p.y >= low.y && p.z >= low.z && p.x <= high.x && p.y <= high.y && p.z <= high.z; }
    constexpr Bounds merged(const Bounds& o) const {
        return {{std::min(low.x, o.low.x), std::min(low.y, o.low.y), std::min(low.z, o.low.z)}, {std::max(high.x, o.high.x), std::max(high.y, o.high.y), std::max(high.z, o.high.z)}};
    }
};

struct Color {
    double r = 0, g = 0, b = 0, a = 1;
    constexpr bool operator==(const Color&) const = default;
    static Color hex(std::string_view text);
    static Color hex(std::string_view text, Color fallback);
    static Color rgb(double r, double g, double b) { return {r, g, b, 1}; }
    Color blended(Color to, double amount) const {
        double t = std::clamp(amount, 0.0, 1.0);
        return {r + (to.r - r) * t, g + (to.g - g) * t, b + (to.b - b) * t, 1};
    }
};

inline double radians(double degrees) { return degrees * pi / 180; }
inline double degrees(double radians) { return radians * 180 / pi; }

}
