#include "world/flat_generator.h"

#include "core/constants.h"
#include "core/profiler.h"
#include "data/flat_preset.h"
#include "world/chunk.h"
#include "world/chunk_section.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <utility>

namespace aurora::world {

std::unique_ptr<Chunk> generateFlatChunk(const data::FlatPreset& preset, ChunkPos pos)
{
    AURORA_PROFILE_ZONE_N("Generate flat chunk");

    // One state per height, bottom first. The loader keeps the layers within the world height; anything past it
    // would be cut off here.
    std::array<BlockStateId, core::kWorldHeight> column;
    column.fill(data::kAirState);
    std::size_t filledTo = 0;
    for (const data::FlatLayer& layer : preset.layers) {
        const std::size_t end = std::min(column.size(), filledTo + layer.height);
        for (; filledTo < end; ++filledTo) {
            column[filledTo] = layer.state;
        }
    }

    auto chunk = std::make_unique<Chunk>(pos);
    for (std::int32_t index = 0; index < core::kSectionsPerChunk; ++index) {
        const auto first = column.begin() + static_cast<std::ptrdiff_t>(index) * core::kSectionSize;
        const auto last = first + core::kSectionSize;
        if (std::all_of(first, last, [state = *first](BlockStateId other) { return other == state; })) {
            // One layer covers the section: a single state, no cell data. All air stays null.
            if (*first != data::kAirState) {
                chunk->setSection(index, std::make_unique<ChunkSection>(ChunkSection::filled(*first)));
            }
            continue;
        }

        auto section = std::make_unique<ChunkSection>();
        for (std::int32_t y = 0; y < core::kSectionSize; ++y) {
            const BlockStateId state = first[y];
            if (state == data::kAirState) {
                continue;
            }
            for (std::int32_t z = 0; z < core::kSectionSize; ++z) {
                for (std::int32_t x = 0; x < core::kSectionSize; ++x) {
                    section->set(x, y, z, state);
                }
            }
        }
        chunk->setSection(index, std::move(section));
    }

    chunk->rebuildHeightMap();
    chunk->setStatus(ChunkStatus::Generated);
    return chunk;
}

ChunkGenerator makeFlatGenerator(std::shared_ptr<const data::FlatPreset> preset)
{
    return [preset = std::move(preset)](ChunkPos pos) { return generateFlatChunk(*preset, pos); };
}

} // namespace aurora::world
