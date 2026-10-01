#pragma once

#include "client/client_world.h"
#include "core/constants.h"
#include "world/chunk_snapshot.h"
#include "world/chunk_update.h"

#include <cstdint>
#include <memory>
#include <utility>

namespace aurora::test {

// A snapshot whose section `filledSection` (if >= 0) is all `state`; every other section is air.
inline std::shared_ptr<const world::ChunkSnapshot> makeSnapshot(world::ChunkPos pos, std::uint64_t generation,
                                                                std::int32_t filledSection = 7,
                                                                data::BlockStateId state = 7)
{
    world::ChunkSnapshot::Sections sections;
    if (filledSection >= 0) {
        sections[static_cast<std::size_t>(filledSection)] =
            std::make_shared<const world::ChunkSection>(world::ChunkSection::filled(state));
    }
    return std::make_shared<const world::ChunkSnapshot>(pos, generation, std::move(sections));
}

inline world::ChunkUpdate loaded(world::ChunkPos pos, std::uint64_t generation, std::int32_t filledSection = 7)
{
    return {world::ChunkUpdate::Kind::Loaded, pos, generation, makeSnapshot(pos, generation, filledSection)};
}

inline world::ChunkUpdate unloaded(world::ChunkPos pos, std::uint64_t generation)
{
    return {world::ChunkUpdate::Kind::Unloaded, pos, generation, nullptr};
}

// Loads every chunk within `radius` of `center` (square), numbering generations from `firstGeneration`.
inline std::uint64_t loadSquare(client::ClientWorld& world, world::ChunkPos center, std::int32_t radius,
                                std::uint64_t firstGeneration = 1)
{
    std::uint64_t generation = firstGeneration;
    for (std::int32_t dz = -radius; dz <= radius; ++dz) {
        for (std::int32_t dx = -radius; dx <= radius; ++dx) {
            world.apply(loaded({center.x + dx, center.z + dz}, generation++));
        }
    }
    return generation;
}

} // namespace aurora::test
