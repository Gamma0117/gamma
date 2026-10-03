#include "world/world.h"

#include "core/job_system.h"
#include "core/log.h"
#include "core/profiler.h"
#include "world/chunk.h"

#include <cassert>
#include <chrono>
#include <exception>
#include <format>
#include <limits>
#include <string>
#include <utility>

namespace aurora::world {

namespace {

// Chunk coordinates whose blocks all have int32 coordinates.
constexpr std::int64_t kMinChunkCoord = std::numeric_limits<std::int32_t>::min() >> core::kSectionBits;
constexpr std::int64_t kMaxChunkCoord = std::numeric_limits<std::int32_t>::max() >> core::kSectionBits;

bool isValidChunkCoord(std::int64_t coord)
{
    return coord >= kMinChunkCoord && coord <= kMaxChunkCoord;
}

// Calls `visit` for every position at square distance `ring` from `center`.
template <typename Visit>
void forEachInRing(ChunkPos center, std::int32_t ring, Visit&& visit)
{
    const auto visitOffset = [&](std::int64_t dx, std::int64_t dz) {
        const std::int64_t x = center.x + dx;
        const std::int64_t z = center.z + dz;
        if (isValidChunkCoord(x) && isValidChunkCoord(z)) {
            visit(ChunkPos{static_cast<std::int32_t>(x), static_cast<std::int32_t>(z)});
        }
    };
    if (ring == 0) {
        visitOffset(0, 0);
        return;
    }
    for (std::int64_t dx = -ring; dx <= ring; ++dx) {
        visitOffset(dx, -ring);
        visitOffset(dx, ring);
    }
    for (std::int64_t dz = -ring + 1; dz <= ring - 1; ++dz) {
        visitOffset(-ring, dz);
        visitOffset(ring, dz);
    }
}

} // namespace

World::World(std::shared_ptr<const data::BlockRegistry> registry, core::JobSystem& jobs, ChunkGenerator generator)
    : m_owner(std::this_thread::get_id())
    , m_registry(std::move(registry))
    , m_jobs(jobs)
    , m_generator(std::move(generator))
{
    assert(m_registry && m_generator);
}

World::~World()
{
    checkOwnerThread();
    // Pending futures are dropped with the table; their jobs never refer back to the world.
}

void World::ensureLoaded(ChunkPos center, std::int32_t radius)
{
    checkOwnerThread();
    AURORA_PROFILE_ZONE_N("World ensureLoaded");
    assert(radius >= 0);

    const std::int64_t keepRadius = static_cast<std::int64_t>(radius) + 1;
    std::erase_if(m_chunks, [&](const auto& item) {
        const auto& [pos, entry] = item;
        if (chunkDistance(pos, center) <= keepRadius) {
            return false;
        }
        if (entry.state == EntryState::Loaded) {
            m_updates.push_back({.kind = ChunkUpdate::Kind::Unloaded, .pos = pos, .generation = entry.generation});
        }
        return true;
    });

    for (std::int32_t ring = 0; ring <= radius; ++ring) {
        forEachInRing(center, ring, [this](ChunkPos pos) {
            if (!m_chunks.contains(pos)) {
                request(pos);
            }
        });
    }
}

void World::update()
{
    checkOwnerThread();
    AURORA_PROFILE_ZONE_N("World update");
    for (auto& [pos, entry] : m_chunks) {
        if (entry.state == EntryState::Pending &&
            entry.result.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            receive(pos, entry);
        }
    }
}

std::optional<BlockStateId> World::getBlock(const BlockPos& pos) const
{
    checkOwnerThread();
    if (!isInWorldHeight(pos.y)) {
        return std::nullopt;
    }
    const Chunk* loaded = loadedChunk(chunkPosOf(pos));
    if (!loaded) {
        return std::nullopt;
    }
    return loaded->getBlock(localCoord(pos.x), pos.y, localCoord(pos.z));
}

bool World::setBlock(const BlockPos& pos, BlockStateId state)
{
    checkOwnerThread();
    if (!isInWorldHeight(pos.y) || state >= m_registry->stateCount()) {
        return false;
    }
    const ChunkPos chunkPos = chunkPosOf(pos);
    const auto found = m_chunks.find(chunkPos);
    if (found == m_chunks.end() || found->second.state != EntryState::Loaded) {
        return false;
    }
    Entry& entry = found->second;
    const BlockStateId previous = entry.chunk->setBlock(localCoord(pos.x), pos.y, localCoord(pos.z), state);
    if (previous != state) {
        if (entry.dirtySections == 0) {
            m_dirtyChunks.push_back(chunkPos);
        }
        entry.dirtySections |= 1u << sectionIndex(pos.y);
    }
    return true;
}

void World::publishChanges()
{
    checkOwnerThread();
    AURORA_PROFILE_ZONE_N("World publishChanges");
    for (const ChunkPos pos : m_dirtyChunks) {
        const auto found = m_chunks.find(pos);
        if (found == m_chunks.end() || found->second.state != EntryState::Loaded || found->second.dirtySections == 0) {
            continue; // Unloaded since (its marks went with it), or a new load that has not changed.
        }
        Entry& entry = found->second;
        entry.published = ChunkSnapshot::changedFrom(*entry.published, *entry.chunk, entry.dirtySections,
                                                     entry.published->revision() + 1);
        m_updates.push_back({.kind = ChunkUpdate::Kind::Changed,
                             .pos = pos,
                             .generation = entry.generation,
                             .snapshot = entry.published,
                             .changedSections = entry.dirtySections});
        entry.dirtySections = 0;
    }
    m_dirtyChunks.clear();
}

const Chunk* World::chunk(ChunkPos pos) const
{
    checkOwnerThread();
    return loadedChunk(pos);
}

std::optional<std::uint64_t> World::generation(ChunkPos pos) const
{
    checkOwnerThread();
    const auto found = m_chunks.find(pos);
    if (found == m_chunks.end() || found->second.state != EntryState::Loaded) {
        return std::nullopt;
    }
    return found->second.generation;
}

std::vector<ChunkUpdate> World::takeChunkUpdates()
{
    checkOwnerThread();
    return std::exchange(m_updates, {});
}

WorldStats World::stats() const
{
    checkOwnerThread();
    WorldStats counts;
    for (const auto& [pos, entry] : m_chunks) {
        switch (entry.state) {
        case EntryState::Pending:
            ++counts.pendingChunks;
            break;
        case EntryState::Loaded:
            ++counts.loadedChunks;
            break;
        case EntryState::Failed:
            ++counts.failedChunks;
            break;
        }
    }
    return counts;
}

void World::checkOwnerThread() const
{
    assert(std::this_thread::get_id() == m_owner && "World used from a thread other than the one that created it");
}

Chunk* World::loadedChunk(ChunkPos pos) const
{
    const auto found = m_chunks.find(pos);
    if (found == m_chunks.end() || found->second.state != EntryState::Loaded) {
        return nullptr;
    }
    return found->second.chunk.get();
}

void World::request(ChunkPos pos)
{
    Entry& entry = m_chunks[pos];
    // The job owns copies of what it needs: the position and the generator (which holds its own data).
    entry.result = m_jobs.async([generator = m_generator, pos] { return generator(pos); });
    if (!entry.result.valid()) {
        core::logError("world", "Chunk ({}, {}) failed: the job system is not accepting jobs", pos.x, pos.z);
        entry.state = EntryState::Failed;
    }
}

void World::receive(ChunkPos pos, Entry& entry)
{
    std::optional<std::string> failure;
    std::unique_ptr<Chunk> chunk;
    try {
        chunk = entry.result.get();
        if (!chunk) {
            failure = "the generator returned no chunk";
        } else if (chunk->pos() != pos) {
            failure = std::format("the generator returned chunk ({}, {})", chunk->pos().x, chunk->pos().z);
        }
    } catch (const std::exception& e) {
        failure = std::format("generation threw: {}", e.what());
    } catch (...) {
        failure = "generation threw an unknown exception";
    }
    entry.result = {};

    if (failure) {
        core::logError("world", "Chunk ({}, {}) failed: {}", pos.x, pos.z, *failure);
        entry.state = EntryState::Failed;
        return;
    }
    entry.chunk = std::move(chunk);
    entry.state = EntryState::Loaded;
    entry.generation = ++m_lastGeneration;
    entry.published = ChunkSnapshot::copyOf(*entry.chunk, entry.generation);
    entry.dirtySections = 0;
    m_updates.push_back({.kind = ChunkUpdate::Kind::Loaded,
                         .pos = pos,
                         .generation = entry.generation,
                         .snapshot = entry.published});
}

} // namespace aurora::world
