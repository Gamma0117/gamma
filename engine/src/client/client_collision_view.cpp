#include "client/client_collision_view.h"

#include "client/client_world.h"
#include "world/chunk_snapshot.h"

namespace aurora::client {

bool ClientCollisionView::isLoaded(world::ChunkPos pos) const
{
    return m_world.snapshot(pos) != nullptr;
}

data::BlockStateId ClientCollisionView::blockAt(const world::BlockPos& pos) const
{
    const auto snapshot = m_world.snapshot(world::chunkPosOf(pos));
    // Only asked for loaded columns inside the world height; anything else blocks, never lets through.
    if (!snapshot || !world::isInWorldHeight(pos.y)) {
        return data::kUnknownState;
    }
    return snapshot->getBlock(world::localCoord(pos.x), pos.y, world::localCoord(pos.z));
}

} // namespace aurora::client
