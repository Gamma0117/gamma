#include "entity/collision_shapes.h"

#include <cmath>
#include <format>

namespace aurora::entity {

CollisionShapes::CollisionShapes(std::size_t stateCount)
    : m_boxes(stateCount)
{
}

CollisionShapes CollisionShapes::fromRegistry(const data::BlockRegistry& registry)
{
    CollisionShapes shapes(registry.stateCount());
    for (std::uint32_t state = 0; state < registry.stateCount(); ++state) {
        const auto id = static_cast<data::BlockStateId>(state);
        if (registry.blockOf(id).solid) {
            shapes.m_boxes[state] = {kFullCube};
        }
    }
    return shapes;
}

std::optional<std::string> CollisionShapes::setShape(data::BlockStateId state, std::span<const Aabb> boxes)
{
    if (state >= m_boxes.size()) {
        return std::format("state {} is not in the table ({} states)", state, m_boxes.size());
    }
    if (boxes.size() > kMaxBoxesPerState) {
        return std::format("at most {} boxes per state, got {}", kMaxBoxesPerState, boxes.size());
    }
    for (std::size_t i = 0; i < boxes.size(); ++i) {
        const Aabb& box = boxes[i];
        for (int axis = 0; axis < 3; ++axis) {
            const double low = box.min[axis];
            const double high = box.max[axis];
            if (!std::isfinite(low) || !std::isfinite(high)) {
                return std::format("box {}: coordinates must be finite", i);
            }
            if (low < 0.0 || high > 1.0) {
                return std::format("box {}: must lie inside the block cell [0, 1]", i);
            }
            if (high - low < kMinBoxSize) {
                return std::format("box {}: must be at least 1/16 block on every axis", i);
            }
        }
    }
    m_boxes[state].assign(boxes.begin(), boxes.end());
    return std::nullopt;
}

std::span<const Aabb> CollisionShapes::boxes(data::BlockStateId state) const
{
    static constexpr Aabb kUnknown[1] = {kFullCube};
    if (state >= m_boxes.size()) {
        return kUnknown;
    }
    return m_boxes[state];
}

} // namespace aurora::entity
