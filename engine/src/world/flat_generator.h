#pragma once

#include "world/chunk_generator.h"
#include "world/coordinates.h"

#include <memory>

namespace aurora::data {
struct FlatPreset;
}

namespace aurora::world {

// The flat world: `preset` layers stacked from core::kWorldMinY up, air above. The same for every position, so the
// result depends on nothing but its inputs. A section covered by a single layer is stored as one state (0 bits).
std::unique_ptr<Chunk> generateFlatChunk(const data::FlatPreset& preset, ChunkPos pos);

// A generator that keeps `preset` alive for as long as any copy of it (including queued jobs) exists.
ChunkGenerator makeFlatGenerator(std::shared_ptr<const data::FlatPreset> preset);

} // namespace aurora::world
