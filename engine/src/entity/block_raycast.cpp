#include "entity/block_raycast.h"

#include "data/player_interaction.h"
#include "entity/collision.h"

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace aurora::entity {

namespace {

constexpr double kMinCoordinate = static_cast<double>(std::numeric_limits<std::int32_t>::min());
constexpr double kMaxCoordinate = static_cast<double>(std::numeric_limits<std::int32_t>::max());

bool isFinite(const glm::dvec3& v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool inBlockRange(std::int64_t value)
{
    return value >= std::numeric_limits<std::int32_t>::min() && value <= std::numeric_limits<std::int32_t>::max();
}

enum class CellKind : std::uint8_t {
    Open,       // Nothing to pick: air, an invisible state, or outside the world height.
    Selectable, // A block to pick.
    Unloaded,
};

struct Cell {
    CellKind kind = CellKind::Open;
    data::BlockStateId state = data::kAirState;
};

Cell lookAt(const BlockCollisionView& view, const std::vector<bool>& selectable,
            const std::array<std::int64_t, 3>& cell)
{
    const world::BlockPos pos{static_cast<std::int32_t>(cell[0]), static_cast<std::int32_t>(cell[1]),
                              static_cast<std::int32_t>(cell[2])};
    if (!view.isLoaded(world::chunkPosOf(pos))) {
        return {CellKind::Unloaded};
    }
    if (!world::isInWorldHeight(pos.y)) {
        return {};
    }
    const data::BlockStateId state = view.blockAt(pos);
    if (state < selectable.size() && selectable[state]) {
        return {CellKind::Selectable, state};
    }
    return {};
}

// The face a ray enters through when it steps into a cell along `axis` in direction `step`, and its normal.
data::BlockFace entryFace(int axis, int step)
{
    constexpr std::array<std::array<data::BlockFace, 2>, 3> kFaces{{
        {data::BlockFace::East, data::BlockFace::West},  // x: moving -x enters the east face, +x the west.
        {data::BlockFace::Up, data::BlockFace::Down},    // y
        {data::BlockFace::South, data::BlockFace::North}, // z: moving -z enters the south face (+z side).
    }};
    return kFaces[static_cast<std::size_t>(axis)][step > 0 ? 1 : 0];
}

} // namespace

glm::dvec3 lookDirection(double yaw, double pitch)
{
    const double yawRadians = glm::radians(yaw);
    const double pitchRadians = glm::radians(pitch);
    return {std::sin(yawRadians) * std::cos(pitchRadians), std::sin(pitchRadians),
            -std::cos(yawRadians) * std::cos(pitchRadians)};
}

glm::dvec3 eyePosition(const PlayerMotion& motion, const data::PlayerMovementTuning& tuning)
{
    return motion.position + glm::dvec3(0.0, motion.sneaking ? tuning.sneakEyeHeight : tuning.eyeHeight, 0.0);
}

std::vector<bool> selectableStates(const data::BlockRegistry& registry)
{
    std::vector<bool> selectable(registry.stateCount(), false);
    for (std::uint32_t state = 0; state < registry.stateCount(); ++state) {
        const auto id = static_cast<data::BlockStateId>(state);
        selectable[state] = id != data::kAirState && registry.blockOf(id).render != data::RenderLayer::Invisible;
    }
    return selectable;
}

RaycastResult raycastBlocks(const BlockCollisionView& view, const std::vector<bool>& selectable,
                            const glm::dvec3& origin, const glm::dvec3& direction, double reach)
{
    RaycastResult result;
    result.status = RaycastStatus::Invalid;
    if (!isFinite(origin) || !isFinite(direction) || !std::isfinite(reach) || reach < data::kMinReach ||
        reach > data::kMaxReach) {
        return result;
    }
    // Normalised without overflow or underflow: scaled by the largest component first.
    const double largest = std::max({std::abs(direction.x), std::abs(direction.y), std::abs(direction.z)});
    if (largest == 0.0) {
        return result;
    }
    glm::dvec3 dir = direction / largest;
    dir /= glm::length(dir);

    std::array<std::int64_t, 3> cell{};
    std::array<int, 3> step{};
    std::array<double, 3> tMax{};
    std::array<double, 3> tDelta{};
    for (int axis = 0; axis < 3; ++axis) {
        const double o = origin[axis];
        const double d = dir[axis];
        const double floored = std::floor(o);
        if (floored < kMinCoordinate || floored > kMaxCoordinate) {
            return result;
        }
        std::int64_t c = static_cast<std::int64_t>(floored);
        if (d < 0.0 && o == floored) {
            --c; // On the plane, moving down: the cell below it comes first.
        }
        if (!inBlockRange(c)) {
            return result;
        }
        cell[static_cast<std::size_t>(axis)] = c;
        if (d > 0.0) {
            step[static_cast<std::size_t>(axis)] = 1;
            tMax[static_cast<std::size_t>(axis)] = (static_cast<double>(c) + 1.0 - o) / d;
            tDelta[static_cast<std::size_t>(axis)] = 1.0 / d;
        } else if (d < 0.0) {
            step[static_cast<std::size_t>(axis)] = -1;
            tMax[static_cast<std::size_t>(axis)] = (o - static_cast<double>(c)) / -d;
            tDelta[static_cast<std::size_t>(axis)] = 1.0 / -d;
        } else {
            tMax[static_cast<std::size_t>(axis)] = std::numeric_limits<double>::infinity();
            tDelta[static_cast<std::size_t>(axis)] = std::numeric_limits<double>::infinity();
        }
    }

    const Cell start = lookAt(view, selectable, cell);
    if (start.kind == CellKind::Unloaded) {
        result.status = RaycastStatus::Unloaded;
        return result;
    }
    if (start.kind == CellKind::Selectable) {
        return result; // Inside a block: no face of it can be aimed at.
    }

    for (;;) {
        const double t = std::min({tMax[0], tMax[1], tMax[2]});
        if (!(t <= reach)) {
            result.status = RaycastStatus::Miss;
            return result;
        }
        int entryAxis = -1;
        for (int axis = 0; axis < 3; ++axis) {
            const auto a = static_cast<std::size_t>(axis);
            if (tMax[a] <= t + kRayEpsilon) {
                if (entryAxis < 0) {
                    entryAxis = axis; // X, then Y, then Z wins a tie.
                }
                cell[a] += step[a];
                tMax[a] += tDelta[a];
            }
        }
        if (!inBlockRange(cell[0]) || !inBlockRange(cell[1]) || !inBlockRange(cell[2])) {
            result.status = RaycastStatus::Miss; // Past the edge of block coordinates: nothing there.
            return result;
        }
        const Cell next = lookAt(view, selectable, cell);
        if (next.kind == CellKind::Unloaded) {
            result.status = RaycastStatus::Unloaded;
            return result;
        }
        if (next.kind == CellKind::Selectable) {
            const auto a = static_cast<std::size_t>(entryAxis);
            result.status = RaycastStatus::Hit;
            result.cell = {static_cast<std::int32_t>(cell[0]), static_cast<std::int32_t>(cell[1]),
                           static_cast<std::int32_t>(cell[2])};
            result.state = next.state;
            result.face = entryFace(entryAxis, step[a]);
            result.normal = glm::ivec3(0);
            result.normal[entryAxis] = -step[a];
            result.distance = t;
            result.point = origin + dir * t;
            return result;
        }
    }
}

} // namespace aurora::entity
