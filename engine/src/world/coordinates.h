#pragma once

#include "core/constants.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace aurora::world {

// Block coordinates. y is the real height (core::kWorldMinY is the lowest layer).
struct BlockPos {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t z = 0;

    friend bool operator==(const BlockPos&, const BlockPos&) = default;
};

// Chunk column coordinates: the chunk holding block x is x >> 4.
struct ChunkPos {
    std::int32_t x = 0;
    std::int32_t z = 0;

    friend bool operator==(const ChunkPos&, const ChunkPos&) = default;
};

// Chunk (or section) coordinate of a block coordinate, rounding towards negative infinity: -1 is in chunk -1,
// -16 in chunk -1, -17 in chunk -2. C++20 defines >> on negative numbers as an arithmetic shift.
constexpr std::int32_t chunkCoord(std::int32_t block)
{
    return block >> core::kSectionBits;
}

// Position inside the chunk, 0..15, for negative coordinates too: -1 is 15, -16 is 0.
constexpr std::int32_t localCoord(std::int32_t block)
{
    return block & (core::kSectionSize - 1);
}

constexpr ChunkPos chunkPosOf(const BlockPos& pos)
{
    return {chunkCoord(pos.x), chunkCoord(pos.z)};
}

// Lowest block x (or z) of a chunk.
constexpr std::int32_t chunkOrigin(std::int32_t chunk)
{
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(chunk) << core::kSectionBits);
}

// Inside [core::kWorldMinY, core::kWorldMaxY).
constexpr bool isInWorldHeight(std::int32_t y)
{
    return y >= core::kWorldMinY && y < core::kWorldMaxY;
}

// Index of the section holding height y, 0 (bottom) to core::kSectionsPerChunk - 1. `y` must be inside the world
// height: check isInWorldHeight first.
constexpr std::int32_t sectionIndex(std::int32_t y)
{
    return (y - core::kWorldMinY) >> core::kSectionBits;
}

// Lowest y of a section.
constexpr std::int32_t sectionBottomY(std::int32_t index)
{
    return core::kWorldMinY + index * core::kSectionSize;
}

// Square (Chebyshev) distance in chunks: max(|dx|, |dz|). Radius r covers (2r + 1)^2 chunks. 64-bit, so any two
// int32 positions work.
constexpr std::int64_t chunkDistance(const ChunkPos& a, const ChunkPos& b)
{
    const std::int64_t dx = static_cast<std::int64_t>(a.x) - b.x;
    const std::int64_t dz = static_cast<std::int64_t>(a.z) - b.z;
    return std::max(dx < 0 ? -dx : dx, dz < 0 ? -dz : dz);
}

struct ChunkPosHash {
    std::size_t operator()(const ChunkPos& pos) const noexcept
    {
        const std::uint64_t packed = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(pos.x)) << 32) |
                                     static_cast<std::uint32_t>(pos.z);
        // Fibonacci multiply, then fold the high half down: neighbouring chunks land in different buckets.
        const std::uint64_t mixed = packed * 0x9E3779B97F4A7C15ull;
        return static_cast<std::size_t>(mixed ^ (mixed >> 32));
    }
};

} // namespace aurora::world
