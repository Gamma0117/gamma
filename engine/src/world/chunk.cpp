#include "world/chunk.h"

#include <algorithm>
#include <cassert>
#include <utility>

namespace aurora::world {

namespace {

using data::kAirState;

std::size_t columnIndex(std::int32_t localX, std::int32_t localZ)
{
    return static_cast<std::size_t>(localZ * core::kSectionSize + localX);
}

bool isLocal(std::int32_t coordinate)
{
    return coordinate >= 0 && coordinate < core::kSectionSize;
}

} // namespace

Chunk::Chunk(ChunkPos pos)
    : m_pos(pos)
{
    m_heightMap.fill(kNoHeight);
}

BlockStateId Chunk::getBlock(std::int32_t localX, std::int32_t y, std::int32_t localZ) const
{
    assert(isLocal(localX) && isLocal(localZ) && isInWorldHeight(y));
    const ChunkSection* stored = section(sectionIndex(y));
    return stored ? stored->get(localX, localCoord(y), localZ) : kAirState;
}

BlockStateId Chunk::setBlock(std::int32_t localX, std::int32_t y, std::int32_t localZ, BlockStateId state)
{
    assert(isLocal(localX) && isLocal(localZ) && isInWorldHeight(y));
    std::unique_ptr<ChunkSection>& stored = m_sections[static_cast<std::size_t>(sectionIndex(y))];
    if (!stored) {
        if (state == kAirState) {
            return kAirState; // Already air.
        }
        stored = std::make_unique<ChunkSection>();
    }

    const BlockStateId previous = stored->set(localX, localCoord(y), localZ, state);
    if (stored->isEmpty()) {
        stored.reset();
    }

    std::int16_t& height = m_heightMap[columnIndex(localX, localZ)];
    if (state != kAirState && y > height) {
        height = static_cast<std::int16_t>(y);
    } else if (state == kAirState && y == height) {
        height = static_cast<std::int16_t>(findHeight(localX, localZ, y - 1));
    }
    return previous;
}

void Chunk::setSection(std::int32_t index, std::unique_ptr<ChunkSection> section)
{
    assert(index >= 0 && index < core::kSectionsPerChunk);
    if (section && section->isEmpty()) {
        section.reset();
    }
    m_sections[static_cast<std::size_t>(index)] = std::move(section);
}

std::size_t Chunk::sectionCount() const
{
    std::size_t count = 0;
    for (const std::unique_ptr<ChunkSection>& stored : m_sections) {
        count += stored ? 1 : 0;
    }
    return count;
}

std::int32_t Chunk::height(std::int32_t localX, std::int32_t localZ) const
{
    assert(isLocal(localX) && isLocal(localZ));
    return m_heightMap[columnIndex(localX, localZ)];
}

void Chunk::rebuildHeightMap()
{
    for (std::int32_t z = 0; z < core::kSectionSize; ++z) {
        for (std::int32_t x = 0; x < core::kSectionSize; ++x) {
            m_heightMap[columnIndex(x, z)] = static_cast<std::int16_t>(findHeight(x, z, core::kWorldMaxY - 1));
        }
    }
}

std::int32_t Chunk::findHeight(std::int32_t localX, std::int32_t localZ, std::int32_t fromY) const
{
    if (fromY < core::kWorldMinY) {
        return kNoHeight;
    }
    for (std::int32_t index = sectionIndex(fromY); index >= 0; --index) {
        const ChunkSection* stored = section(index);
        if (!stored) {
            continue; // All air: skip the whole section.
        }
        const std::int32_t bottom = sectionBottomY(index);
        const std::int32_t top = std::min(fromY, bottom + core::kSectionSize - 1);
        for (std::int32_t y = top; y >= bottom; --y) {
            if (stored->get(localX, y - bottom, localZ) != kAirState) {
                return y;
            }
        }
    }
    return kNoHeight;
}

} // namespace aurora::world
