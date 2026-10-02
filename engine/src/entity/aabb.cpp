#include "entity/aabb.h"

#include <glm/common.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace aurora::entity {

namespace {

// Floor or ceiling of a coordinate as a cell index, kept one inside the int32 range (coordinates far beyond the
// world would otherwise overflow the conversion or the "- 1" of a range end).
std::int32_t toCell(double value)
{
    constexpr double kLow = std::numeric_limits<std::int32_t>::min() + 1.0;
    constexpr double kHigh = std::numeric_limits<std::int32_t>::max() - 1.0;
    if (std::isnan(value)) {
        return 0; // Never from valid motion; keeps the conversion defined.
    }
    return static_cast<std::int32_t>(std::clamp(value, kLow, kHigh));
}

} // namespace

bool overlapsOnAxis(const Aabb& a, const Aabb& b, int axis)
{
    return a.max[axis] - b.min[axis] > kCollisionEpsilon && b.max[axis] - a.min[axis] > kCollisionEpsilon;
}

bool overlaps(const Aabb& a, const Aabb& b)
{
    return overlapsOnAxis(a, b, kAxisX) && overlapsOnAxis(a, b, kAxisY) && overlapsOnAxis(a, b, kAxisZ);
}

Aabb unite(const Aabb& a, const Aabb& b)
{
    return {glm::min(a.min, b.min), glm::max(a.max, b.max)};
}

CellRange overlappedCells(double min, double max)
{
    return {toCell(std::floor(min + kCollisionEpsilon)), toCell(std::ceil(max - kCollisionEpsilon)) - 1};
}

CellRange touchedCells(double min, double max)
{
    return {toCell(std::floor(min - kCollisionEpsilon)), toCell(std::ceil(max + kCollisionEpsilon)) - 1};
}

} // namespace aurora::entity
