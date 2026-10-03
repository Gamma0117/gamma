#pragma once

#include "world/chunk_snapshot.h"
#include "world/coordinates.h"

#include <chrono>
#include <cstdint>
#include <memory>

namespace aurora::world {

// What a client needs to learn about the server's chunk table, in order. P0-11 turns these into packets.
struct ChunkUpdate {
    enum class Kind : std::uint8_t {
        Loaded,   // The chunk is now loaded; `snapshot` holds its blocks.
        Unloaded, // The load with this generation is gone.
        Changed,  // Blocks of this load changed; `snapshot` is the whole chunk at its new revision.
    };

    Kind kind = Kind::Loaded;
    ChunkPos pos;
    // Which load of `pos` this is about. Each Loaded gets a new, larger number; an Unloaded repeats the number of
    // the load it ends, so a client can ignore one that does not match what it holds. A Changed repeats it too.
    std::uint64_t generation = 0;
    std::shared_ptr<const ChunkSnapshot> snapshot{}; // Loaded and Changed.
    // Changed only: the sections that differ from the previous revision (bit i = section i). Every Changed of a load
    // is sent, in order, so applying them all invalidates every section that changed.
    std::uint32_t changedSections = 0;
    // Set by the server when it publishes the update at the end of a tick: that tick's number and its monotonic time
    // in this process (P0 only; a network client needs its own clock, P0-11). Used to time how long a change takes
    // to reach the client's meshes.
    std::uint64_t serverTick = 0;
    std::chrono::steady_clock::time_point publishedAt{};
};

} // namespace aurora::world
