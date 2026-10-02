#pragma once

#include "data/block_registry.h"
#include "entity/aabb.h"
#include "world/coordinates.h"

#include <array>
#include <span>
#include <vector>

namespace aurora::entity {

class CollisionShapes;

// The blocks as one side of the game sees them: the server's World or the client's snapshots.
class BlockCollisionView {
public:
    virtual ~BlockCollisionView() = default;

    // Whether the chunk column is loaded on this side.
    virtual bool isLoaded(world::ChunkPos pos) const = 0;
    // The block at `pos`. Only called for a loaded column and y inside the world height.
    virtual data::BlockStateId blockAt(const world::BlockPos& pos) const = 0;
};

// What collision tests against.
//
// The cells:
// - Rule A (wall): every cell of a column that is not loaded is a full cube, at every height. Nothing walks or
//   falls into a column whose blocks are unknown.
// - In a loaded column, below the world (y < core::kWorldMinY) is a full cube and above it (y >= kWorldMaxY) is
//   empty; inside, the block's boxes from CollisionShapes.
struct CollisionWorld {
    const BlockCollisionView& view;
    const CollisionShapes& shapes;
};

// Appends the world-space boxes of every cell that `region` overlaps or touches.
void collectBoxes(const CollisionWorld& world, const Aabb& region, std::vector<Aabb>& out);

// How far `box` may move along `axis` (towards the sign of `distance`, at most |distance|) before it touches a box
// of `obstacles` that it overlaps on the other two axes. Obstacles it already overlaps by more than
// kCollisionEpsilon along `axis` do not stop it, so a box can always move out of one.
double clipAxis(const Aabb& box, int axis, double distance, std::span<const Aabb> obstacles);

struct SweepResult {
    glm::dvec3 moved{0.0};               // Actual movement.
    std::array<bool, 3> blocked{};       // Per axis: stopped short of the wanted movement.
};

// Moves `box` by `wanted` one axis at a time (Y, then X, then Z), clipping each against `obstacles`. Every obstacle
// on the whole way is considered, so no speed passes through a thin wall.
SweepResult sweep(const Aabb& box, const glm::dvec3& wanted, std::span<const Aabb> obstacles);

// Rule B (freeze): true if any chunk column that `box` overlaps with positive extent is not loaded. Columns it only
// touches do not count, so a box stopped by rule A at a column border can still move back. Every overlapped column
// is checked, whatever the box's size.
bool touchesUnloadedColumn(const BlockCollisionView& view, const Aabb& box);

} // namespace aurora::entity
