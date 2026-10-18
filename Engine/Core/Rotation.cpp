#include "Rotation.hpp"

namespace ld::rotation {

Mat3 axis(int which, double degrees) {
    double a = degrees * pi / 180, c = std::cos(a), s = std::sin(a);
    Mat3 r;
    switch (which) {
    case 0: r.m = {{{1, 0, 0}, {0, c, -s}, {0, s, c}}}; break;
    case 1: r.m = {{{c, 0, s}, {0, 1, 0}, {-s, 0, c}}}; break;
    default: r.m = {{{c, -s, 0}, {s, c, 0}, {0, 0, 1}}}; break;
    }
    return r;
}

Mat3 matrix(double x, double y, double z) { return axis(2, z) * (axis(1, y) * axis(0, x)); }

Vec3 euler(const Mat3& m) {
    auto clean = [](double value) {
        double rounded = std::round(value * 1000000) / 1000000;
        return rounded == 0 ? 0.0 : rounded;
    };
    double y = std::asin(std::clamp(-m[2][0], -1.0, 1.0));
    if (std::abs(m[2][0]) < 0.999999)
        return {clean(std::atan2(m[2][1], m[2][2]) * 180 / pi), clean(y * 180 / pi), clean(std::atan2(m[1][0], m[0][0]) * 180 / pi)};
    return {clean(std::atan2(-m[1][2], m[1][1]) * 180 / pi), clean(y * 180 / pi), 0};
}

}
