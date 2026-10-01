#pragma once

#include "core/constants.h"
#include "world/chunk_section.h"
#include "world/coordinates.h"

#include <array>
#include <cstdint>
#include <memory>

namespace aurora::world {

class Chunk;

// An immutable copy of a loaded chunk, the only form in which chunk data leaves the server thread (to the client
// and its meshing jobs). Nothing changes it after construction, so any thread may read it and keep it alive with a
// shared_ptr. A later change to the same chunk (P0-7) makes a new snapshot that shares the untouched sections.
class ChunkSnapshot {
public:
    using Sections = std::array<std::shared_ptr<const ChunkSection>, core::kSectionsPerChunk>;

    // `generation` tells loads of the same position apart (see World).
    ChunkSnapshot(ChunkPos pos, std::uint64_t generation, Sections sections);

    // Copies every stored section of `chunk`.
    static std::shared_ptr<const ChunkSnapshot> copyOf(const Chunk& chunk, std::uint64_t generation);

    ChunkPos pos() const { return m_pos; }
    std::uint64_t generation() const { return m_generation; }
    // Null for an all-air section. `index` 0..kSectionsPerChunk - 1.
    const ChunkSection* section(std::int32_t index) const { return m_sections[static_cast<std::size_t>(index)].get(); }
    // localX and localZ 0..15, y inside the world height (asserted).
    BlockStateId getBlock(std::int32_t localX, std::int32_t y, std::int32_t localZ) const;

private:
    ChunkPos m_pos;
    std::uint64_t m_generation = 0;
    Sections m_sections;
};

} // namespace aurora::world
