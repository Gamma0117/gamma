#include "client/client_world.h"

#include "core/profiler.h"

#include <cassert>
#include <utility>

namespace aurora::client {

namespace {

// Offsets of the eight neighbours, in MeshInput order without the center.
constexpr std::array<std::array<std::int32_t, 2>, 8> kNeighbourOffsets{{
    {-1, -1},
    {0, -1},
    {1, -1},
    {-1, 0},
    {1, 0},
    {-1, 1},
    {0, 1},
    {1, 1},
}};

world::ChunkPos offset(world::ChunkPos pos, std::int32_t dx, std::int32_t dz)
{
    return {pos.x + dx, pos.z + dz};
}

} // namespace

ClientWorld::ClientWorld(std::int32_t renderDistance)
    : m_renderDistance(renderDistance < 0 ? 0 : renderDistance)
{
}

void ClientWorld::apply(const world::ChunkUpdate& update)
{
    AURORA_PROFILE_ZONE_N("ClientWorld apply");
    const auto found = m_chunks.find(update.pos);
    switch (update.kind) {
    case world::ChunkUpdate::Kind::Loaded: {
        assert(update.snapshot && update.snapshot->pos() == update.pos);
        if (found != m_chunks.end() && found->second.snapshot->generation() >= update.generation) {
            return; // Not newer than what is held.
        }
        m_chunks[update.pos].snapshot = update.snapshot;
        refresh(update.pos, true);
        refreshNeighbours(update.pos);
        break;
    }
    case world::ChunkUpdate::Kind::Unloaded:
        if (found == m_chunks.end() || found->second.snapshot->generation() != update.generation) {
            return; // Not the load this ends.
        }
        m_chunks.erase(found);
        if (m_changedSet.insert(update.pos).second) {
            m_changed.push_back(update.pos); // Gone: whatever was made for it is stale.
        }
        refreshNeighbours(update.pos);
        break;
    }
}

void ClientWorld::setCenter(world::ChunkPos center)
{
    if (center == m_center) {
        return;
    }
    m_center = center;
    for (auto& [pos, entry] : m_chunks) {
        refresh(pos, false);
    }
}

std::shared_ptr<const world::ChunkSnapshot> ClientWorld::snapshot(world::ChunkPos pos) const
{
    const auto found = m_chunks.find(pos);
    return found == m_chunks.end() ? nullptr : found->second.snapshot;
}

bool ClientWorld::isEligible(world::ChunkPos pos) const
{
    const auto found = m_chunks.find(pos);
    return found != m_chunks.end() && found->second.eligible;
}

std::optional<std::uint64_t> ClientWorld::stamp(const SectionKey& key) const
{
    const auto found = m_chunks.find(key.pos);
    if (found == m_chunks.end()) {
        return std::nullopt;
    }
    return found->second.stamps[static_cast<std::size_t>(key.section)];
}

std::optional<MeshKey> ClientWorld::currentKey(const SectionKey& key) const
{
    const auto found = m_chunks.find(key.pos);
    if (found == m_chunks.end() || !found->second.eligible) {
        return std::nullopt;
    }
    return MeshKey{key, found->second.snapshot->generation(),
                   found->second.stamps[static_cast<std::size_t>(key.section)]};
}

bool ClientWorld::isCurrent(const MeshKey& key) const
{
    const std::optional<MeshKey> current = currentKey(key.section);
    return current && *current == key;
}

std::optional<std::array<std::shared_ptr<const world::ChunkSnapshot>, 9>>
ClientWorld::neighbourhood(world::ChunkPos pos) const
{
    if (!isEligible(pos)) {
        return std::nullopt;
    }
    std::array<std::shared_ptr<const world::ChunkSnapshot>, 9> chunks;
    for (std::int32_t dz = -1; dz <= 1; ++dz) {
        for (std::int32_t dx = -1; dx <= 1; ++dx) {
            chunks[static_cast<std::size_t>((dz + 1) * 3 + (dx + 1))] = snapshot(offset(pos, dx, dz));
        }
    }
    return chunks;
}

std::vector<world::ChunkPos> ClientWorld::takeChangedChunks()
{
    m_changedSet.clear();
    return std::exchange(m_changed, {});
}

std::size_t ClientWorld::eligibleCount() const
{
    std::size_t count = 0;
    for (const auto& [pos, entry] : m_chunks) {
        count += entry.eligible ? 1 : 0;
    }
    return count;
}

bool ClientWorld::isAreaComplete() const
{
    for (std::int32_t dz = -m_renderDistance; dz <= m_renderDistance; ++dz) {
        for (std::int32_t dx = -m_renderDistance; dx <= m_renderDistance; ++dx) {
            if (!isEligible(offset(m_center, dx, dz))) {
                return false;
            }
        }
    }
    return true;
}

std::vector<world::ChunkPos> ClientWorld::eligibleChunks() const
{
    std::vector<world::ChunkPos> result;
    for (const auto& [pos, entry] : m_chunks) {
        if (entry.eligible) {
            result.push_back(pos);
        }
    }
    return result;
}

bool ClientWorld::computeEligible(world::ChunkPos pos) const
{
    if (world::chunkDistance(pos, m_center) > m_renderDistance) {
        return false;
    }
    for (const auto& [dx, dz] : kNeighbourOffsets) {
        if (!m_chunks.contains(offset(pos, dx, dz))) {
            return false;
        }
    }
    return true;
}

void ClientWorld::refresh(world::ChunkPos pos, bool inputsChanged)
{
    const auto found = m_chunks.find(pos);
    if (found == m_chunks.end()) {
        return;
    }
    Entry& entry = found->second;
    const bool eligible = computeEligible(pos);
    if (eligible == entry.eligible && !inputsChanged) {
        return;
    }
    entry.eligible = eligible;
    entry.stamps.fill(++m_lastStamp);
    if (m_changedSet.insert(pos).second) {
        m_changed.push_back(pos);
    }
}

void ClientWorld::refreshNeighbours(world::ChunkPos pos)
{
    for (const auto& [dx, dz] : kNeighbourOffsets) {
        refresh(offset(pos, dx, dz), true);
    }
}

} // namespace aurora::client
