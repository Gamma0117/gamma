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
// shared_ptr. A later change to the same chunk makes a new snapshot that shares the untouched sections.
class ChunkSnapshot {
public:
    using Sections = std::array<std::shared_ptr<const ChunkSection>, core::kSectionsPerChunk>;

    // `generation` tells loads of the same position apart (see World); `revision` orders the snapshots of one load
    // (0 for the first, one more for each published change).
    ChunkSnapshot(ChunkPos pos, std::uint64_t generation, Sections sections, std::uint64_t revision = 0);

    // Copies every stored section of `chunk`.
    static std::shared_ptr<const ChunkSnapshot> copyOf(const Chunk& chunk, std::uint64_t generation);
    // The next snapshot of the same load: the sections in `changedSections` (bit i = section i) are copied from
    // `chunk` (null when all air), every other section is shared with `previous`.
    static std::shared_ptr<const ChunkSnapshot> changedFrom(const ChunkSnapshot& previous, const Chunk& chunk,
                                                            std::uint32_t changedSections, std::uint64_t revision);

    ChunkPos pos() const { return m_pos; }
    std::uint64_t generation() const { return m_generation; }
    std::uint64_t revision() const { return m_revision; }
    // Null for an all-air section. `index` 0..kSectionsPerChunk - 1.
    const ChunkSection* section(std::int32_t index) const { return m_sections[static_cast<std::size_t>(index)].get(); }
    // localX and localZ 0..15, y inside the world height (asserted).
    BlockStateId getBlock(std::int32_t localX, std::int32_t y, std::int32_t localZ) const;

private:
    ChunkPos m_pos;
    std::uint64_t m_generation = 0;
    std::uint64_t m_revision = 0;
    Sections m_sections;
};

} // namespace aurora::world
