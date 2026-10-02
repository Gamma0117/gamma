#include "entity/collision.h"

#include "core/constants.h"
#include "entity/collision_shapes.h"

#include <algorithm>

namespace aurora::entity {

void collectBoxes(const CollisionWorld& world, const Aabb& region, std::vector<Aabb>& out)
{
    const CellRange xs = touchedCells(region.min.x, region.max.x);
    const CellRange ys = touchedCells(region.min.y, region.max.y);
    const CellRange zs = touchedCells(region.min.z, region.max.z);
    for (std::int32_t z = zs.first; z <= zs.last; ++z) {
        for (std::int32_t x = xs.first; x <= xs.last; ++x) {
            const bool loaded = world.view.isLoaded(world::chunkPosOf({x, 0, z}));
            for (std::int32_t y = ys.first; y <= ys.last; ++y) {
                const glm::dvec3 origin(x, y, z);
                if (!loaded || y < core::kWorldMinY) {
                    out.push_back(kFullCube.moved(origin));
                    continue;
                }
                if (y >= core::kWorldMaxY) {
                    continue;
                }
                for (const Aabb& box : world.shapes.boxes(world.view.blockAt({x, y, z}))) {
                    out.push_back(box.moved(origin));
                }
            }
        }
    }
}

double clipAxis(const Aabb& box, int axis, double distance, std::span<const Aabb> obstacles)
{
    if (distance == 0.0) {
        return 0.0;
    }
    const int first = (axis + 1) % 3;
    const int second = (axis + 2) % 3;
    for (const Aabb& obstacle : obstacles) {
        if (!overlapsOnAxis(box, obstacle, first) || !overlapsOnAxis(box, obstacle, second)) {
            continue;
        }
        if (distance > 0.0) {
            const double gap = obstacle.min[axis] - box.max[axis];
            if (gap >= -kCollisionEpsilon) { // Ahead (or touching); deeper overlaps are ignored.
                distance = std::min(distance, std::max(gap, 0.0));
            }
        } else {
            const double gap = obstacle.max[axis] - box.min[axis];
            if (gap <= kCollisionEpsilon) {
                distance = std::max(distance, std::min(gap, 0.0));
            }
        }
    }
    return distance;
}

SweepResult sweep(const Aabb& box, const glm::dvec3& wanted, std::span<const Aabb> obstacles)
{
    SweepResult result;
    Aabb current = box;
    for (const int axis : {kAxisY, kAxisX, kAxisZ}) {
        const double moved = clipAxis(current, axis, wanted[axis], obstacles);
        result.moved[axis] = moved;
        result.blocked[static_cast<std::size_t>(axis)] = moved != wanted[axis];
        glm::dvec3 offset(0.0);
        offset[axis] = moved;
        current = current.moved(offset);
    }
    return result;
}

bool touchesUnloadedColumn(const BlockCollisionView& view, const Aabb& box)
{
    const CellRange xs = overlappedCells(box.min.x, box.max.x);
    const CellRange zs = overlappedCells(box.min.z, box.max.z);
    if (xs.empty() || zs.empty()) {
        return false;
    }
    const std::int32_t firstX = world::chunkCoord(xs.first);
    const std::int32_t lastX = world::chunkCoord(xs.last);
    const std::int32_t firstZ = world::chunkCoord(zs.first);
    const std::int32_t lastZ = world::chunkCoord(zs.last);
    for (std::int32_t z = firstZ; z <= lastZ; ++z) {
        for (std::int32_t x = firstX; x <= lastX; ++x) {
            if (!view.isLoaded({x, z})) {
                return true;
            }
        }
    }
    return false;
}

} // namespace aurora::entity
