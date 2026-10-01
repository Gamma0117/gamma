#include "world/chunk_snapshot.h"

#include "world/chunk.h"

#include <cassert>
#include <utility>

namespace aurora::world {

ChunkSnapshot::ChunkSnapshot(ChunkPos pos, std::uint64_t generation, Sections sections)
    : m_pos(pos)
    , m_generation(generation)
    , m_sections(std::move(sections))
{
}

std::shared_ptr<const ChunkSnapshot> ChunkSnapshot::copyOf(const Chunk& chunk, std::uint64_t generation)
{
    Sections sections;
    for (std::int32_t index = 0; index < core::kSectionsPerChunk; ++index) {
        if (const ChunkSection* section = chunk.section(index)) {
            sections[static_cast<std::size_t>(index)] = std::make_shared<const ChunkSection>(*section);
        }
    }
    return std::make_shared<const ChunkSnapshot>(chunk.pos(), generation, std::move(sections));
}

BlockStateId ChunkSnapshot::getBlock(std::int32_t localX, std::int32_t y, std::int32_t localZ) const
{
    assert(localX >= 0 && localX < core::kSectionSize && localZ >= 0 && localZ < core::kSectionSize &&
           isInWorldHeight(y));
    const ChunkSection* stored = section(sectionIndex(y));
    return stored ? stored->get(localX, localCoord(y), localZ) : data::kAirState;
}

} // namespace aurora::world
