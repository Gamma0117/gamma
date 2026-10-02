#pragma once

#include "world/chunk_generator.h"
#include "world/coordinates.h"

#include <memory>

namespace aurora::data {
struct FlatPreset;
}

namespace aurora::world {

// The flat world: `preset` layers stacked from core::kWorldMinY up, air above, then the preset's boxes (only their
// part inside this chunk, in file order). The result depends on nothing but its inputs. A section covered by a
// single layer is stored as one state (0 bits). The height map is built last, so it sees carved boxes.
std::unique_ptr<Chunk> generateFlatChunk(const data::FlatPreset& preset, ChunkPos pos);

// A generator that keeps `preset` alive for as long as any copy of it (including queued jobs) exists.
ChunkGenerator makeFlatGenerator(std::shared_ptr<const data::FlatPreset> preset);

} // namespace aurora::world
