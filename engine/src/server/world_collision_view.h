#pragma once

#include "entity/collision.h"

namespace aurora::world {
class World;
}

namespace aurora::server {

// The server's World as collision sees it: a column is loaded when its chunk is Loaded (Pending and Failed are
// not). Server thread only, like the world.
class WorldCollisionView final : public entity::BlockCollisionView {
public:
    explicit WorldCollisionView(const world::World& world)
        : m_world(world)
    {
    }

    bool isLoaded(world::ChunkPos pos) const override;
    data::BlockStateId blockAt(const world::BlockPos& pos) const override;

private:
    const world::World& m_world;
};

} // namespace aurora::server
