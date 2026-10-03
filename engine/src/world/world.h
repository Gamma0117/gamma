#pragma once

#include "data/block_registry.h"
#include "world/chunk_generator.h"
#include "world/chunk_update.h"
#include "world/coordinates.h"

#include <cstddef>
#include <cstdint>
#include <future>
#include <memory>
#include <optional>
#include <thread>
#include <unordered_map>
#include <vector>

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
// - Each time a position becomes Loaded it gets a new generation number (1, 2, ... for the whole world) and a
//   Loaded update with a snapshot; removing a Loaded entry adds an Unloaded update with the same number.
//   Pending and Failed entries were never announced, so removing them adds nothing.
//
// Block changes: setBlock() marks the section dirty when the block really changes (writing the state already
// there changes nothing). publishChanges() turns the dirty sections of every loaded chunk into one Changed update
// with the next revision and a snapshot that copies only those sections and shares the rest with the last one
// published. So a section changed several times between two calls is copied once, with its final blocks, and a
// change that is undone before the call (A -> B -> A) is still published once. Unloading drops the dirty marks;
// updates already made stay in order.
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
    // Adds a Changed update for every loaded chunk with dirty sections, in the order they first changed.
    void publishChanges();

    // Null unless loaded.
    const Chunk* chunk(ChunkPos pos) const;
    // The load generation of a loaded chunk, nullopt otherwise.
    std::optional<std::uint64_t> generation(ChunkPos pos) const;
    // The updates since the last call, oldest first.
    std::vector<ChunkUpdate> takeChunkUpdates();
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
        // Loaded only:
        std::unique_ptr<Chunk> chunk;
        std::uint64_t generation = 0;
        std::shared_ptr<const ChunkSnapshot> published; // The last snapshot sent out.
        std::uint32_t dirtySections = 0;                // Bit i: section i changed since `published`.
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
    std::uint64_t m_lastGeneration = 0;
    std::vector<ChunkUpdate> m_updates;
    std::vector<ChunkPos> m_dirtyChunks; // In the order they first changed since the last publishChanges().
};

} // namespace aurora::world
