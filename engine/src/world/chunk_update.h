#pragma once

#include "world/chunk_snapshot.h"
#include "world/coordinates.h"

#include <cstdint>
#include <memory>

namespace aurora::world {

// What a client needs to learn about the server's chunk table, in order. P0-11 turns these into packets.
struct ChunkUpdate {
    enum class Kind : std::uint8_t {
        Loaded,   // The chunk is now loaded; `snapshot` holds its blocks.
        Unloaded, // The load with this generation is gone.
    };

    Kind kind = Kind::Loaded;
    ChunkPos pos;
    // Which load of `pos` this is about. Each Loaded gets a new, larger number; an Unloaded repeats the number of
    // the load it ends, so a client can ignore one that does not match what it holds.
    std::uint64_t generation = 0;
    std::shared_ptr<const ChunkSnapshot> snapshot; // Loaded only.
};

} // namespace aurora::world
