#pragma once

#include "core/constants.h"
#include "data/block_registry.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace aurora::world {

using data::BlockStateId;

// 16x16x16 block states, packed by how many different states the section holds:
//
//   palette entries | bits per cell | cell data
//   1               | 0             | none (the one palette entry fills the section)
//   2               | 1             | 512 B
//   3-4             | 2             | 1 KB
//   5-16            | 4             | 2 KB
//   17-256          | 8             | 4 KB
//   257+            | 16 (direct)   | 8 KB, state ids stored as they are, no palette
//
// Cells sit in 64-bit words and never span two of them (every width divides 64). Cell index is (y * 16 + z) * 16 + x
// with local coordinates 0..15.
//
// Writing (set() always returns the previous state):
//   same state         -> nothing changes
//   air -> non-air     -> non-air count + 1; a new state joins the palette, widening the cells when it is full
//   non-air -> non-air -> count unchanged;   same palette rule
//   non-air -> air     -> count - 1; air joins the palette if needed. At 0 the section resets to all air, 0 bits
//                         (and the chunk drops it).
// So filled(stone) with one cell set to air holds {stone, air} in 1 bit per cell, 4095 non-air.
// set() never removes palette entries; compact() does.
class ChunkSection {
public:
    static constexpr std::size_t kCellCount = static_cast<std::size_t>(core::kSectionVolume);
    // Largest palette; one more state switches to direct storage.
    static constexpr std::size_t kMaxPaletteSize = 256;
    static constexpr std::uint32_t kDirectBits = 16;

    // All air.
    ChunkSection();

    // Every cell set to `state`, 0 bits per cell.
    static ChunkSection filled(BlockStateId state);

    static constexpr std::size_t cellIndex(std::int32_t x, std::int32_t y, std::int32_t z)
    {
        return static_cast<std::size_t>((y * core::kSectionSize + z) * core::kSectionSize + x);
    }

    // Local coordinates 0..15 / index below kCellCount (checked by assert only).
    BlockStateId get(std::int32_t x, std::int32_t y, std::int32_t z) const { return getAt(cellIndex(x, y, z)); }
    BlockStateId set(std::int32_t x, std::int32_t y, std::int32_t z, BlockStateId state)
    {
        return setAt(cellIndex(x, y, z), state);
    }
    BlockStateId getAt(std::size_t index) const;
    BlockStateId setAt(std::size_t index, BlockStateId state);

    // Rebuilds the palette from the states actually in use (sorted by id) with the fewest bits that hold them:
    // unused entries go, direct storage returns to a palette at 256 states or fewer, and a section of one state
    // is 0 bits again. Cell values and the non-air count do not change.
    void compact();

    // Cells that are not air (0..4096).
    std::uint32_t nonAirCount() const { return m_nonAirCount; }
    bool isEmpty() const { return m_nonAirCount == 0; }
    std::uint32_t bitsPerEntry() const { return m_bits; }
    bool isDirect() const { return m_bits == kDirectBits; }
    // Empty in direct mode.
    const std::vector<BlockStateId>& palette() const { return m_palette; }
    // Heap bytes held for cell data and palette.
    std::size_t memoryBytes() const;

private:
    // Palette slot of `state`, adding it (and widening the cells) if missing. In direct mode the state itself.
    std::uint32_t slotFor(BlockStateId state);
    std::uint32_t readSlot(std::size_t index) const;
    void writeSlot(std::size_t index, std::uint32_t slot);
    // Widens every cell to `bits` (more than now) and installs `palette`. Palette slots keep their numbers; at
    // kDirectBits the cells become state ids and `palette` must be empty.
    void repack(std::uint32_t bits, std::vector<BlockStateId> palette);
    void resetToAir();

    std::vector<BlockStateId> m_palette;
    std::vector<std::uint64_t> m_data;
    std::uint32_t m_bits = 0;
    std::uint32_t m_nonAirCount = 0;
};

} // namespace aurora::world
