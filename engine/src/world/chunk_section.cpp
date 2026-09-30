#include "world/chunk_section.h"

#include <algorithm>
#include <array>
#include <bitset>
#include <cassert>
#include <utility>

namespace aurora::world {

namespace {

using data::kAirState;

constexpr std::uint32_t kWordBits = 64;

using CellStates = std::array<BlockStateId, ChunkSection::kCellCount>;

// Narrowest width that holds `size` palette slots; kDirectBits past the largest palette.
constexpr std::uint32_t bitsForPaletteSize(std::size_t size)
{
    if (size <= 1) {
        return 0;
    }
    if (size <= 2) {
        return 1;
    }
    if (size <= 4) {
        return 2;
    }
    if (size <= 16) {
        return 4;
    }
    if (size <= ChunkSection::kMaxPaletteSize) {
        return 8;
    }
    return ChunkSection::kDirectBits;
}

constexpr std::size_t wordCount(std::uint32_t bits)
{
    return ChunkSection::kCellCount * bits / kWordBits;
}

static_assert(kWordBits % ChunkSection::kDirectBits == 0 && kWordBits % 8 == 0, "cells must not span words");
static_assert(wordCount(1) * 8 == 512 && wordCount(ChunkSection::kDirectBits) * 8 == 8192);

} // namespace

ChunkSection::ChunkSection()
    : m_palette{kAirState}
{
}

ChunkSection ChunkSection::filled(BlockStateId state)
{
    ChunkSection section;
    section.m_palette[0] = state;
    section.m_nonAirCount = state == kAirState ? 0 : static_cast<std::uint32_t>(kCellCount);
    return section;
}

BlockStateId ChunkSection::getAt(std::size_t index) const
{
    assert(index < kCellCount);
    const std::uint32_t slot = readSlot(index);
    return isDirect() ? static_cast<BlockStateId>(slot) : m_palette[slot];
}

BlockStateId ChunkSection::setAt(std::size_t index, BlockStateId state)
{
    const BlockStateId previous = getAt(index);
    if (previous == state) {
        return previous;
    }
    if (state == kAirState && m_nonAirCount == 1) {
        resetToAir(); // The last non-air cell: nothing is left to store.
        return previous;
    }

    writeSlot(index, slotFor(state));
    if (previous == kAirState) {
        ++m_nonAirCount;
    } else if (state == kAirState) {
        --m_nonAirCount;
    }
    return previous;
}

void ChunkSection::compact()
{
    if (m_nonAirCount == 0) {
        resetToAir();
        return;
    }

    CellStates states;
    for (std::size_t i = 0; i < kCellCount; ++i) {
        states[i] = getAt(i);
    }
    std::bitset<std::size_t{1} << 16> seen;
    std::vector<BlockStateId> used;
    for (const BlockStateId state : states) {
        if (!seen[state]) {
            seen[state] = true;
            used.push_back(state);
        }
    }
    std::ranges::sort(used);

    const std::uint32_t bits = bitsForPaletteSize(used.size());
    m_bits = bits;
    // Direct storage keeps no palette. A new empty vector rather than clear(), which would keep the allocation;
    // `used` is freed on return.
    m_palette = bits == kDirectBits ? std::vector<BlockStateId>() : std::move(used);
    m_data = std::vector<std::uint64_t>(wordCount(bits)); // A fresh vector: shrinking frees the old words.
    if (bits == 0) {
        return;
    }
    for (std::size_t i = 0; i < kCellCount; ++i) {
        // The palette is sorted, so a slot is a binary search away.
        const std::uint32_t slot =
            isDirect() ? states[i]
                       : static_cast<std::uint32_t>(std::ranges::lower_bound(m_palette, states[i]) - m_palette.begin());
        writeSlot(i, slot);
    }
}

std::size_t ChunkSection::memoryBytes() const
{
    return m_data.capacity() * sizeof(std::uint64_t) + m_palette.capacity() * sizeof(BlockStateId);
}

std::uint32_t ChunkSection::slotFor(BlockStateId state)
{
    if (isDirect()) {
        return state;
    }
    const auto found = std::ranges::find(m_palette, state);
    if (found != m_palette.end()) {
        return static_cast<std::uint32_t>(found - m_palette.begin());
    }

    const std::size_t newSize = m_palette.size() + 1;
    const std::uint32_t bits = bitsForPaletteSize(newSize);
    if (bits == kDirectBits) {
        repack(bits, {});
        return state;
    }
    if (bits != m_bits) {
        repack(bits, m_palette);
    }
    m_palette.push_back(state);
    return static_cast<std::uint32_t>(newSize - 1);
}

std::uint32_t ChunkSection::readSlot(std::size_t index) const
{
    if (m_bits == 0) {
        return 0;
    }
    const std::size_t perWord = kWordBits / m_bits;
    const std::uint64_t mask = (std::uint64_t{1} << m_bits) - 1;
    const auto shift = static_cast<std::uint32_t>(index % perWord) * m_bits;
    return static_cast<std::uint32_t>((m_data[index / perWord] >> shift) & mask);
}

void ChunkSection::writeSlot(std::size_t index, std::uint32_t slot)
{
    assert(m_bits > 0 && slot < (std::uint64_t{1} << m_bits));
    const std::size_t perWord = kWordBits / m_bits;
    const std::uint64_t mask = (std::uint64_t{1} << m_bits) - 1;
    const auto shift = static_cast<std::uint32_t>(index % perWord) * m_bits;
    std::uint64_t& word = m_data[index / perWord];
    word = (word & ~(mask << shift)) | (static_cast<std::uint64_t>(slot) << shift);
}

void ChunkSection::repack(std::uint32_t bits, std::vector<BlockStateId> palette)
{
    assert(bits > m_bits);
    const bool toDirect = bits == kDirectBits;
    // Widening keeps every palette slot; only the direct switch turns slots into state ids.
    std::vector<std::uint64_t> old = std::exchange(m_data, std::vector<std::uint64_t>(wordCount(bits)));
    const std::uint32_t oldBits = std::exchange(m_bits, bits);
    const std::vector<BlockStateId> oldPalette = std::exchange(m_palette, std::move(palette));

    for (std::size_t i = 0; i < kCellCount; ++i) {
        std::uint32_t slot = 0;
        if (oldBits > 0) {
            const std::size_t perWord = kWordBits / oldBits;
            const std::uint64_t mask = (std::uint64_t{1} << oldBits) - 1;
            const auto shift = static_cast<std::uint32_t>(i % perWord) * oldBits;
            slot = static_cast<std::uint32_t>((old[i / perWord] >> shift) & mask);
        }
        writeSlot(i, toDirect ? oldPalette[slot] : slot);
    }
}

void ChunkSection::resetToAir()
{
    m_palette = std::vector<BlockStateId>{kAirState};
    m_data = std::vector<std::uint64_t>();
    m_bits = 0;
    m_nonAirCount = 0;
}

} // namespace aurora::world
