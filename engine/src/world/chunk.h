#pragma once

#include "core/constants.h"
#include "world/chunk_section.h"
#include "world/coordinates.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace aurora::world {

enum class ChunkStatus : std::uint8_t {
    Empty,     // Just created, nothing generated yet.
    Generated, // Terrain is in place. Later stages (features, light) come with their steps.
};

// A 16 x 384 x 16 column of core::kSectionsPerChunk sections. An all-air section is not stored at all (null), so
// the open sky above the surface costs no memory. Owned by one thread at a time: a worker while it generates the
// chunk, then the server thread once World takes it.
//
// Height map: per column, the y of the highest non-air block, or kNoHeight for a column of only air. The flat
// world's surface (grass at y 63) reads 63; a caller that wants the first free block above adds 1.
class Chunk {
public:
    static constexpr auto kNoHeight = static_cast<std::int16_t>(core::kWorldMinY - 1);

    explicit Chunk(ChunkPos pos);

    ChunkPos pos() const { return m_pos; }
    ChunkStatus status() const { return m_status; }
    void setStatus(ChunkStatus status) { m_status = status; }

    // localX and localZ 0..15, y inside the world height (asserted; World checks before calling).
    BlockStateId getBlock(std::int32_t localX, std::int32_t y, std::int32_t localZ) const;
    // Returns the previous state. Creates the section on the first non-air block and drops it when it is all air
    // again; keeps the height map up to date.
    BlockStateId setBlock(std::int32_t localX, std::int32_t y, std::int32_t localZ, BlockStateId state);

    // Null for an all-air section. `index` 0..kSectionsPerChunk - 1.
    const ChunkSection* section(std::int32_t index) const { return m_sections[static_cast<std::size_t>(index)].get(); }
    // Replaces a whole section (used by generators); an empty one is stored as null. Call rebuildHeightMap()
    // after the last one.
    void setSection(std::int32_t index, std::unique_ptr<ChunkSection> section);
    // Sections currently stored (not null).
    std::size_t sectionCount() const;

    std::int32_t height(std::int32_t localX, std::int32_t localZ) const;
    // Recomputes every column from the sections, top down.
    void rebuildHeightMap();

private:
    // Highest non-air y in the column at or below `fromY`, or kNoHeight.
    std::int32_t findHeight(std::int32_t localX, std::int32_t localZ, std::int32_t fromY) const;

    ChunkPos m_pos;
    ChunkStatus m_status = ChunkStatus::Empty;
    std::array<std::unique_ptr<ChunkSection>, core::kSectionsPerChunk> m_sections;
    std::array<std::int16_t, core::kSectionArea> m_heightMap; // Index z * 16 + x.
};

} // namespace aurora::world
