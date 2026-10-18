#pragma once

#include "Math.hpp"

namespace ld::rotation {

Mat3 axis(int axis, double degrees);
Mat3 matrix(double x, double y, double z);
Vec3 euler(const Mat3& m);

}
