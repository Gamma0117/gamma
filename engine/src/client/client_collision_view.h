#pragma once

#include "entity/collision.h"

namespace aurora::client {

class ClientWorld;

// The client's copy of the world as collision sees it: a column is loaded when its snapshot has arrived, whether
// or not it is drawn yet. Main thread only, like the ClientWorld.
class ClientCollisionView final : public entity::BlockCollisionView {
public:
    explicit ClientCollisionView(const ClientWorld& world)
        : m_world(world)
    {
    }

    bool isLoaded(world::ChunkPos pos) const override;
    data::BlockStateId blockAt(const world::BlockPos& pos) const override;

private:
    const ClientWorld& m_world;
};

} // namespace aurora::client
