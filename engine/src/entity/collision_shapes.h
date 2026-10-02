#pragma once

#include "data/block_registry.h"
#include "entity/aabb.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace aurora::entity {

// The collision boxes of every block state, in the block's own cell: [0, 1] on every axis. Immutable once built;
// safe to read from any thread.
//
// Built from the registry: a solid block is the full cube, anything else has no boxes. Partial shapes (slabs,
// stairs) come from data later; until then tests set them with setShape().
class CollisionShapes {
public:
    static constexpr std::size_t kMaxBoxesPerState = 16;
    static constexpr double kMinBoxSize = 1.0 / 16.0; // On every axis: one model unit.

    // `stateCount` states with no boxes.
    explicit CollisionShapes(std::size_t stateCount);
    static CollisionShapes fromRegistry(const data::BlockRegistry& registry);

    // Replaces the boxes of `state`. Each box must be finite, inside [0, 1], at least kMinBoxSize on every axis,
    // and there may be at most kMaxBoxesPerState. Returns why not, leaving the shape unchanged.
    std::optional<std::string> setShape(data::BlockStateId state, std::span<const Aabb> boxes);

    // Boxes of `state`; a state beyond the table (never from a world made with the same registry) is a full cube,
    // so an unexpected id blocks rather than lets through.
    std::span<const Aabb> boxes(data::BlockStateId state) const;
    std::size_t stateCount() const { return m_boxes.size(); }

private:
    std::vector<std::vector<Aabb>> m_boxes;
};

// The unit cube [0, 1]^3.
inline constexpr Aabb kFullCube{{0.0, 0.0, 0.0}, {1.0, 1.0, 1.0}};

} // namespace aurora::entity
