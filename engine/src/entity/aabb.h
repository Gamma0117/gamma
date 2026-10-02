#pragma once

#include <glm/vec3.hpp>

#include <cstdint>

namespace aurora::entity {

// The one tolerance of collision, in blocks. Two boxes overlap on an axis only by more than this; boxes that touch
// (or overlap by less, from rounding) do not. About 27 double steps at the planned world border (+-30,000,000,
// where the step is 2^-28), and far below the smallest box (1/16 block, the player at least 0.1).
inline constexpr double kCollisionEpsilon = 1e-7;

// Axis indices into glm::dvec3.
inline constexpr int kAxisX = 0;
inline constexpr int kAxisY = 1;
inline constexpr int kAxisZ = 2;

// An axis-aligned box in block units, min <= max on every axis.
struct Aabb {
    glm::dvec3 min{0.0};
    glm::dvec3 max{0.0};

    Aabb moved(const glm::dvec3& offset) const { return {min + offset, max + offset}; }
};

// Overlap with positive extent (more than kCollisionEpsilon) on one axis.
bool overlapsOnAxis(const Aabb& a, const Aabb& b, int axis);
// On all three axes.
bool overlaps(const Aabb& a, const Aabb& b);

// The smallest box holding both.
Aabb unite(const Aabb& a, const Aabb& b);

// Whole-number cells [first, last] along one axis.
struct CellRange {
    std::int32_t first = 0;
    std::int32_t last = -1;

    bool empty() const { return last < first; }
};

// Cells that [min, max] overlaps with positive extent: [floor(min + e), ceil(max - e) - 1]. Not empty for any
// extent above 2e, so a box sitting exactly on a cell boundary does not reach into the next cell.
CellRange overlappedCells(double min, double max);
// Cells that [min, max] overlaps or touches (within e): [floor(min - e), ceil(max + e) - 1]. Collision candidates.
CellRange touchedCells(double min, double max);

} // namespace aurora::entity
