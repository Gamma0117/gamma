#pragma once

#include "data/block_registry.h"
#include "world/chunk_generator.h"
#include "world/coordinates.h"

#include <cstddef>
#include <cstdint>
#include <future>
#include <memory>
#include <optional>
#include <thread>
#include <unordered_map>

namespace aurora::core {
class JobSystem;
}

namespace aurora::world {

using data::BlockStateId;

class Chunk;

struct WorldStats {
    std::size_t loadedChunks = 0;
    std::size_t pendingChunks = 0; // Being generated.
    std::size_t failedChunks = 0;  // Generation failed; not retried while the position stays in range.
};

// Chunks and blocks of one world, owned by the thread that creates it (the server thread). Every member function
// must be called on that thread; a Debug build asserts it. Other threads see the world only through copies such
// as the server's stats snapshot.
//
// Chunk table: one entry per position, Pending (a generation job is running), Loaded or Failed.
// - ensureLoaded(center, r) queues every position within r (square distance, nearest first) that has no entry,
//   and removes every entry farther than r + 1. A position is never queued twice while it has an entry.
// - Jobs capture only the position and a copy of the generator, never the world. Removing a pending entry drops
//   its future, so a late result has nowhere to go and a new request for the same position gets a new future.
//   Destroying the world with jobs still queued is safe: they finish and their results are thrown away.
// - update() takes results that are ready without waiting. A job that threw, returned nullptr or a chunk for
//   another position, or could not be queued at all (the job system is shutting down) turns the entry into
//   Failed, with one log line naming the position. Failed positions are not retried until they leave the keep
//   range (r + 1) and come back.
//
// The world keeps the registry it was created with; states are checked against it.
class World {
public:
    World(std::shared_ptr<const data::BlockRegistry> registry, core::JobSystem& jobs, ChunkGenerator generator);
    ~World();

    World(const World&) = delete;
    World& operator=(const World&) = delete;

    // `radius` >= 0.
    void ensureLoaded(ChunkPos center, std::int32_t radius);
    void update();

    // nullopt when y is outside the world height or the chunk is not loaded (so real air reads as kAirState).
    std::optional<BlockStateId> getBlock(const BlockPos& pos) const;
    // False when y is outside the world height, the chunk is not loaded or `state` is not in the registry.
    bool setBlock(const BlockPos& pos, BlockStateId state);

    // Null unless loaded.
    const Chunk* chunk(ChunkPos pos) const;
    WorldStats stats() const;
    const data::BlockRegistry& registry() const { return *m_registry; }

private:
    enum class EntryState : std::uint8_t {
        Pending,
        Loaded,
        Failed,
    };

    struct Entry {
        EntryState state = EntryState::Pending;
        std::future<std::unique_ptr<Chunk>> result; // Pending only.
        std::unique_ptr<Chunk> chunk;               // Loaded only.
    };

    void checkOwnerThread() const;
    Chunk* loadedChunk(ChunkPos pos) const;
    void request(ChunkPos pos);
    void receive(ChunkPos pos, Entry& entry);

    const std::thread::id m_owner;
    std::shared_ptr<const data::BlockRegistry> m_registry;
    core::JobSystem& m_jobs;
    ChunkGenerator m_generator;
    std::unordered_map<ChunkPos, Entry, ChunkPosHash> m_chunks;
};

} // namespace aurora::world
