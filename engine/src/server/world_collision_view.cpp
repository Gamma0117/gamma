#include "server/world_collision_view.h"

#include "world/chunk.h"
#include "world/world.h"

namespace aurora::server {

bool WorldCollisionView::isLoaded(world::ChunkPos pos) const
{
    return m_world.chunk(pos) != nullptr;
}

data::BlockStateId WorldCollisionView::blockAt(const world::BlockPos& pos) const
{
    const world::Chunk* chunk = m_world.chunk(world::chunkPosOf(pos));
    // Only asked for loaded columns inside the world height; anything else blocks, never lets through.
    if (chunk == nullptr || !world::isInWorldHeight(pos.y)) {
        return data::kUnknownState;
    }
    return chunk->getBlock(world::localCoord(pos.x), pos.y, world::localCoord(pos.z));
}

} // namespace aurora::server
