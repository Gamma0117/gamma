#pragma once

#include "world/chunk_snapshot.h"
#include "world/coordinates.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace aurora::client {

// One section of one chunk column.
struct SectionKey {
    world::ChunkPos pos;
    std::int32_t section = 0; // 0 (bottom) .. core::kSectionsPerChunk - 1

    friend bool operator==(const SectionKey&, const SectionKey&) = default;
};

struct SectionKeyHash {
    std::size_t operator()(const SectionKey& key) const noexcept
    {
        return world::ChunkPosHash{}(key.pos) * 31 + static_cast<std::size_t>(key.section);
    }
};

// Names the exact inputs a mesh was built from. A result is used only while ClientWorld::isCurrent(key) holds:
// the chunk is still the same load (generation), still eligible for rendering, and nothing it depends on has
// changed since (stamp).
struct MeshKey {
    SectionKey section;
    std::uint64_t generation = 0;
    std::uint64_t stamp = 0;

    friend bool operator==(const MeshKey&, const MeshKey&) = default;
};

// Everything a meshing job reads: the chunk and its eight neighbours (immutable snapshots, index
// (dz + 1) * 3 + (dx + 1), so 4 is the chunk itself) and which section to mesh. Jobs own these copies; they never
// see ClientWorld.
struct MeshInput {
    std::array<std::shared_ptr<const world::ChunkSnapshot>, 9> chunks;
    std::int32_t section = 0;

    const world::ChunkSnapshot& center() const { return *chunks[4]; }
};

// A section's mesh in the renderer's vertex format: two 32-bit words per vertex, three uint32 indices per
// triangle. The client only stores and forwards it.
struct MeshData {
    std::vector<std::uint32_t> vertexWords;
    std::vector<std::uint32_t> indices;

    bool empty() const { return indices.empty(); }
    std::size_t vertexCount() const { return vertexWords.size() / 2; }
};

// Builds one section's mesh. Runs on worker threads, several at once: it may read only its input and immutable
// data it owns (the renderer's mesher captures its block and texture tables by shared_ptr).
using MeshFunction = std::function<MeshData(const MeshInput&)>;

} // namespace aurora::client
