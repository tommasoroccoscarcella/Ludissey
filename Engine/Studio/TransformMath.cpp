#include "TransformMath.hpp"

#include "Engine/Core/Rotation.hpp"

#include <limits>

namespace ld::transform_math {

std::array<Vec3, 3> localAxes(const WorldBlock& block) {
    Mat3 m = block.rotationMatrix();
    return {m.column(0), m.column(1), m.column(2)};
}

std::optional<Bounds> bounds(const std::vector<WorldBlock>& blocks) {
    if (blocks.empty()) return std::nullopt;
    double big = std::numeric_limits<double>::max();
    Bounds box{{big, big, big}, {-big, -big, -big}};
    for (const auto& b : blocks) {
        Vec3 half = b.halfExtents();
        box.low = {std::min(box.low.x, b.x - half.x), std::min(box.low.y, b.y - half.y), std::min(box.low.z, b.z - half.z)};
        box.high = {std::max(box.high.x, b.x + half.x), std::max(box.high.y, b.y + half.y), std::max(box.high.z, b.z + half.z)};
    }
    return box;
}

std::optional<TransformFrame> frame(const std::vector<WorldBlock>& blocks, TransformSpace space, bool single) {
    if (single && space == TransformSpace::local && !blocks.empty()) {
        const WorldBlock& b = blocks.front();
        return TransformFrame{{b.x, b.y, b.z}, localAxes(b), {b.sizeX / 2, b.sizeY / 2, b.sizeZ / 2}};
    }
    auto box = bounds(blocks);
    if (!box) return std::nullopt;
    return TransformFrame{box->center(), {Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, 0, 1}},
                          {(box->high.x - box->low.x) / 2, (box->high.y - box->low.y) / 2, (box->high.z - box->low.z) / 2}};
}

std::optional<double> closestParameter(Vec3 rayOrigin, Vec3 rayDirection, Vec3 lineOrigin, Vec3 lineDirection) {
    Vec3 w = lineOrigin - rayOrigin;
    double a = dot(lineDirection, lineDirection), b = dot(lineDirection, rayDirection), c = dot(rayDirection, rayDirection);
    double d = dot(lineDirection, w), e = dot(rayDirection, w);
    double denominator = a * c - b * b;
    if (!(std::abs(denominator) > 1e-9 * std::max(1.0, a * c))) return std::nullopt;
    return (b * e - c * d) / denominator;
}

WorldBlock resized(const WorldBlock& block, int axis, int sign, double delta, double snap) {
    WorldBlock result = block;
    double sizes[3] = {block.sizeX, block.sizeY, block.sizeZ};
    double wanted = sizes[axis] + delta;
    double grain = sizes[axis] < 1 ? std::min(snap, studio_limits::fineSnap) : snap;
    double size = std::min(studio_limits::maximumSize, std::max(studio_limits::minimumSize, grain > 0 ? std::round(wanted / grain) * grain : wanted));
    double growth = size - sizes[axis];
    Vec3 a = localAxes(block)[static_cast<std::size_t>(axis)];
    double s = sign;
    result.x += a.x * s * growth / 2;
    result.y += a.y * s * growth / 2;
    result.z += a.z * s * growth / 2;
    if (axis == 0) result.sizeX = size;
    else if (axis == 1) result.sizeY = size;
    else result.sizeZ = size;
    return result;
}

BlockSet scaled(const BlockSet& origins, int axis, ScaleAnchor anchor, double factor) {
    std::vector<WorldBlock> list;
    for (const auto& entry : origins) list.push_back(entry.second);
    auto box = bounds(list);
    if (!box) return origins;
    double low = box->low[axis], high = box->high[axis];
    double pivot = anchor.center ? (low + high) / 2 : (anchor.sign > 0 ? low : high);
    BlockSet result;
    for (const auto& [id, origin] : origins) {
        WorldBlock b = origin;
        double position = origin.position()[axis];
        double moved = pivot + (position - pivot) * factor;
        if (axis == 0) b.x = moved;
        else if (axis == 1) b.y = moved;
        else b.z = moved;
        Mat3 m = origin.rotationMatrix();
        int local = 0;
        for (int c = 1; c < 3; ++c)
            if (std::abs(m[axis][local]) < std::abs(m[axis][c])) local = c;
        if (local == 0) b.sizeX = origin.sizeX * factor;
        else if (local == 1) b.sizeY = origin.sizeY * factor;
        else b.sizeZ = origin.sizeZ * factor;
        result[id] = b;
    }
    return result;
}

BlockSet moved(const BlockSet& origins, Vec3 direction, double delta, double snap, bool worldAligned) {
    if (origins.empty()) return origins;
    const WorldBlock& first = origins.begin()->second;
    double amount;
    if (worldAligned && snap > 0) {
        double base = dot(first.position(), direction);
        amount = std::round((base + delta) / snap) * snap - base;
    } else amount = snap > 0 ? std::round(delta / snap) * snap : delta;
    BlockSet result;
    for (const auto& [id, origin] : origins) {
        WorldBlock b = origin;
        b.x += direction.x * amount;
        b.y += direction.y * amount;
        b.z += direction.z * amount;
        result[id] = b;
    }
    return result;
}

BlockSet rotated(const BlockSet& origins, Vec3 pivot, int axis, double degrees) {
    Mat3 turn = rotation::axis(axis, degrees);
    BlockSet result;
    for (const auto& [id, origin] : origins) {
        WorldBlock b = origin;
        Vec3 moved = turn * (origin.position() - pivot);
        b.x = pivot.x + moved.x;
        b.y = pivot.y + moved.y;
        b.z = pivot.z + moved.z;
        Vec3 angles = rotation::euler(turn * origin.rotationMatrix());
        b.rotationX = angles.x;
        b.rotationY = angles.y;
        b.rotationZ = angles.z;
        result[id] = b;
    }
    return result;
}

}
