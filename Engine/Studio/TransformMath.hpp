#pragma once

#include "Engine/Experience/Experience.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace ld {

enum class TransformSpace { world, local };

struct ScaleAnchor {
    bool center = true;
    int sign = 1;
    static ScaleAnchor middle() { return {true, 1}; }
    static ScaleAnchor face(int sign) { return {false, sign}; }
    bool operator==(const ScaleAnchor&) const = default;
};

struct TransformFrame {
    Vec3 center;
    std::array<Vec3, 3> axes;
    std::array<double, 3> half{};
    Vec3 direction(int axis, int sign) const { return axes[static_cast<std::size_t>(axis)] * static_cast<double>(sign); }
    Vec3 face(int axis, int sign) const { return center + direction(axis, sign) * half[static_cast<std::size_t>(axis)]; }
};

using BlockSet = std::map<std::string, WorldBlock>;

namespace transform_math {
std::array<Vec3, 3> localAxes(const WorldBlock& block);
std::optional<Bounds> bounds(const std::vector<WorldBlock>& blocks);
std::optional<TransformFrame> frame(const std::vector<WorldBlock>& blocks, TransformSpace space, bool single);
std::optional<double> closestParameter(Vec3 rayOrigin, Vec3 rayDirection, Vec3 lineOrigin, Vec3 lineDirection);
WorldBlock resized(const WorldBlock& block, int axis, int sign, double delta, double snap);
BlockSet scaled(const BlockSet& origins, int axis, ScaleAnchor anchor, double factor);
BlockSet moved(const BlockSet& origins, Vec3 direction, double delta, double snap, bool worldAligned);
BlockSet rotated(const BlockSet& origins, Vec3 pivot, int axis, double degrees);
}

namespace studio_limits {
inline constexpr int parts = rules::world::parts;
inline constexpr double horizontal = rules::world::horizontal;
inline constexpr double vertical = rules::world::vertical;
inline constexpr double minimumSize = rules::world::minimumSize;
inline constexpr double maximumSize = rules::world::maximumSize;
inline constexpr int outputLines = 400;
inline constexpr int history = 120;
inline constexpr double snap = 0.25;
inline constexpr double fineSnap = 0.05;
}

}
