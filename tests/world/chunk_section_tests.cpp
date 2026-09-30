#include "world/chunk_section.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

using aurora::world::BlockStateId;
using aurora::world::ChunkSection;

namespace {

constexpr BlockStateId kAir = 0;
constexpr BlockStateId kStone = 7;
constexpr BlockStateId kDirt = 2;
constexpr std::size_t kCells = ChunkSection::kCellCount;

using Reference = std::array<BlockStateId, kCells>;

// Every cell of `section` equals `reference`, and the non-air count matches.
bool matches(const ChunkSection& section, const Reference& reference)
{
    std::uint32_t nonAir = 0;
    for (std::size_t i = 0; i < kCells; ++i) {
        if (section.getAt(i) != reference[i]) {
            return false;
        }
        nonAir += reference[i] != kAir ? 1 : 0;
    }
    return section.nonAirCount() == nonAir;
}

std::uint32_t expectedBits(std::size_t paletteSize)
{
    if (paletteSize <= 1) {
        return 0;
    }
    if (paletteSize <= 2) {
        return 1;
    }
    if (paletteSize <= 4) {
        return 2;
    }
    if (paletteSize <= 16) {
        return 4;
    }
    return paletteSize <= 256 ? 8 : 16;
}

} // namespace

TEST_CASE("A new section is all air with no cell data", "[world][section]")
{
    const ChunkSection section;
    CHECK(section.isEmpty());
    CHECK(section.nonAirCount() == 0);
    CHECK(section.bitsPerEntry() == 0);
    CHECK(section.palette() == std::vector<BlockStateId>{kAir});
    CHECK(section.memoryBytes() <= 8); // The one palette entry; no words.
    Reference air{};
    CHECK(matches(section, air));
}

TEST_CASE("Cells are indexed x fastest then z then y", "[world][section]")
{
    CHECK(ChunkSection::cellIndex(0, 0, 0) == 0);
    CHECK(ChunkSection::cellIndex(1, 0, 0) == 1);
    CHECK(ChunkSection::cellIndex(0, 0, 1) == 16);
    CHECK(ChunkSection::cellIndex(0, 1, 0) == 256);
    CHECK(ChunkSection::cellIndex(15, 15, 15) == 4095);

    ChunkSection section;
    section.set(3, 5, 9, kStone);
    CHECK(section.getAt(ChunkSection::cellIndex(3, 5, 9)) == kStone);
    CHECK(section.get(9, 5, 3) == kAir);
}

TEST_CASE("Cell width grows at palette sizes 2 3 5 17 and 257 and keeps every value", "[world][section]")
{
    ChunkSection section;
    Reference reference{};
    std::mt19937 random(1234);
    std::uniform_int_distribution<std::size_t> anyCell(0, kCells - 1);

    // New state n (1..300) goes into 8 random cells; earlier states stay in the palette even when overwritten, so
    // after state n the palette holds n + 1 entries (air included).
    for (BlockStateId state = 1; state <= 300; ++state) {
        for (int i = 0; i < 8; ++i) {
            const std::size_t cell = anyCell(random);
            CHECK(section.setAt(cell, state) == reference[cell]);
            reference[cell] = state;
        }
        const std::size_t paletteSize = static_cast<std::size_t>(state) + 1;
        INFO("palette size " << paletteSize);
        REQUIRE(section.bitsPerEntry() == expectedBits(paletteSize));
        CHECK(section.isDirect() == (paletteSize > 256));
        CHECK(section.palette().size() == (section.isDirect() ? 0 : paletteSize));
        // Check every cell at each change of width and the sizes around it.
        if (expectedBits(paletteSize) != expectedBits(paletteSize - 1) ||
            expectedBits(paletteSize) != expectedBits(paletteSize + 1)) {
            REQUIRE(matches(section, reference));
        }
    }
    CHECK(matches(section, reference));
    CHECK(section.memoryBytes() == 8192);
}

TEST_CASE("The last cell and the largest state id survive in palette and direct storage", "[world][section]")
{
    constexpr BlockStateId kLargest = 65535;
    constexpr std::size_t kLastCell = kCells - 1;

    ChunkSection section;
    CHECK(section.set(15, 15, 15, kLargest) == kAir);
    CHECK(section.bitsPerEntry() == 1);
    CHECK(section.getAt(kLastCell) == kLargest);
    CHECK(section.getAt(kLastCell - 1) == kAir);

    // 300 more states push the section into direct storage.
    for (BlockStateId state = 1; state <= 300; ++state) {
        section.setAt(state, state);
    }
    REQUIRE(section.isDirect());
    CHECK(section.getAt(kLastCell) == kLargest);
    CHECK(section.getAt(0) == kAir);
    CHECK(section.getAt(300) == 300);

    CHECK(section.setAt(kLastCell, kLargest - 1) == kLargest);
    CHECK(section.getAt(kLastCell) == kLargest - 1);
    CHECK(section.setAt(kLastCell, kLargest) == kLargest - 1);
    CHECK(section.getAt(kLastCell) == kLargest);
    CHECK(section.getAt(kLastCell - 1) == kAir);
    CHECK(section.nonAirCount() == 301);
}

TEST_CASE("set returns the previous state and keeps the non-air count", "[world][section]")
{
    ChunkSection section;

    // air -> non-air: +1, the state joins the palette.
    CHECK(section.set(1, 2, 3, kStone) == kAir);
    CHECK(section.nonAirCount() == 1);
    CHECK(section.palette() == std::vector<BlockStateId>{kAir, kStone});

    // Same state: nothing changes.
    CHECK(section.set(1, 2, 3, kStone) == kStone);
    CHECK(section.nonAirCount() == 1);
    CHECK(section.bitsPerEntry() == 1);

    // non-air -> non-air: count unchanged, new palette entry, wider cells.
    CHECK(section.set(1, 2, 3, kDirt) == kStone);
    CHECK(section.nonAirCount() == 1);
    CHECK(section.bitsPerEntry() == 2);
    CHECK(section.get(1, 2, 3) == kDirt);

    CHECK(section.set(4, 4, 4, kStone) == kAir);
    CHECK(section.nonAirCount() == 2);

    // non-air -> air: -1.
    CHECK(section.set(4, 4, 4, kAir) == kStone);
    CHECK(section.nonAirCount() == 1);
    CHECK(section.bitsPerEntry() == 2); // set() never shrinks the palette.
}

TEST_CASE("Removing the last non-air block resets the section to all air", "[world][section]")
{
    ChunkSection section;
    for (BlockStateId state = 1; state <= 300; ++state) {
        section.setAt(state * 3, state);
    }
    REQUIRE(section.isDirect());
    for (BlockStateId state = 1; state <= 300; ++state) {
        CHECK(section.setAt(state * 3, kAir) == state);
    }
    CHECK(section.isEmpty());
    CHECK(section.bitsPerEntry() == 0);
    CHECK(section.palette() == std::vector<BlockStateId>{kAir});
    CHECK(section.memoryBytes() <= 8); // The 8 KB of cells are freed.
    Reference air{};
    CHECK(matches(section, air));
}

TEST_CASE("A filled section with one air cell holds two states in 1 bit", "[world][section]")
{
    ChunkSection section = ChunkSection::filled(kStone);
    CHECK(section.bitsPerEntry() == 0);
    CHECK(section.nonAirCount() == 4096);
    CHECK(section.palette() == std::vector<BlockStateId>{kStone});
    CHECK(section.memoryBytes() <= 8);

    CHECK(section.set(3, 4, 5, kAir) == kStone);
    CHECK(section.bitsPerEntry() == 1);
    CHECK(section.palette() == std::vector<BlockStateId>{kStone, kAir});
    CHECK(section.nonAirCount() == 4095);
    CHECK(section.memoryBytes() <= 512 + 8);
    Reference reference;
    reference.fill(kStone);
    reference[ChunkSection::cellIndex(3, 4, 5)] = kAir;
    CHECK(matches(section, reference));

    CHECK(section.set(3, 4, 5, kStone) == kAir);
    CHECK(section.nonAirCount() == 4096);
    section.compact();
    CHECK(section.bitsPerEntry() == 0);
    CHECK(section.palette() == std::vector<BlockStateId>{kStone});

    CHECK(ChunkSection::filled(kAir).isEmpty());
}

TEST_CASE("compact rebuilds the palette from the states in use", "[world][section]")
{
    SECTION("Unused palette entries go and the cells narrow")
    {
        ChunkSection section;
        for (BlockStateId state = 1; state <= 5; ++state) {
            section.set(0, 0, 0, state); // Five states pass through one cell; only the last stays.
        }
        CHECK(section.bitsPerEntry() == 4);
        Reference reference{};
        reference[0] = 5;
        section.compact();
        CHECK(section.palette() == std::vector<BlockStateId>{kAir, 5});
        CHECK(section.bitsPerEntry() == 1);
        CHECK(matches(section, reference));
    }
    SECTION("Direct storage returns to a palette")
    {
        ChunkSection section;
        Reference reference{};
        for (BlockStateId state = 1; state <= 300; ++state) {
            section.setAt(state, state);
            reference[state] = state;
        }
        REQUIRE(section.isDirect());
        for (BlockStateId state = 1; state <= 290; ++state) {
            section.setAt(state, kStone);
            reference[state] = kStone;
        }
        section.compact();
        // air, stone and 291..300: 12 states.
        CHECK_FALSE(section.isDirect());
        CHECK(section.palette().size() == 12);
        CHECK(section.bitsPerEntry() == 4);
        CHECK(matches(section, reference));
        CHECK(section.memoryBytes() <= 2048 + 64);
    }
    SECTION("A section that stays direct keeps no palette")
    {
        // 257 states is the smallest count that stays direct; 4096 is every cell different.
        for (const std::size_t stateCount : {std::size_t{257}, kCells}) {
            INFO(stateCount << " states");
            ChunkSection section;
            Reference reference{};
            for (std::size_t i = 0; i < kCells; ++i) {
                const auto state = static_cast<BlockStateId>(i % stateCount + 1);
                section.setAt(i, state);
                reference[i] = state;
            }
            REQUIRE(section.isDirect());
            REQUIRE(section.memoryBytes() == 8192);

            section.compact();
            CHECK(section.isDirect());
            CHECK(section.palette().empty());
            CHECK(section.memoryBytes() == 8192); // Only the cells: no palette storage left behind.
            CHECK(matches(section, reference));
        }
    }
    SECTION("A section of one non-air state becomes 0 bits")
    {
        ChunkSection section = ChunkSection::filled(kStone);
        for (BlockStateId state = 100; state < 400; ++state) {
            section.setAt(state, state); // Through direct storage...
        }
        REQUIRE(section.isDirect());
        for (BlockStateId state = 100; state < 400; ++state) {
            section.setAt(state, kStone); // ...and back to one state.
        }
        section.compact();
        CHECK(section.bitsPerEntry() == 0);
        CHECK(section.palette() == std::vector<BlockStateId>{kStone});
        CHECK(section.nonAirCount() == 4096);
        Reference reference;
        reference.fill(kStone);
        CHECK(matches(section, reference));
    }
    SECTION("An all-air section stays empty")
    {
        ChunkSection section;
        section.compact();
        CHECK(section.isEmpty());
        CHECK(section.bitsPerEntry() == 0);
    }
}

TEST_CASE("Random writes match a reference array", "[world][section]")
{
    std::mt19937 random(20260930);
    std::uniform_int_distribution<std::size_t> anyCell(0, kCells - 1);
    std::uniform_int_distribution<int> percent(0, 99);
    std::uniform_int_distribution<int> fewStates(0, 5);
    std::uniform_int_distribution<int> manyStates(0, 65535);

    ChunkSection section;
    Reference reference{};
    for (int step = 1; step <= 40000; ++step) {
        const int roll = percent(random);
        // Mostly a handful of states (with air), sometimes any state, so every width is visited.
        const int drawn = roll < 40 ? 0 : roll < 97 ? fewStates(random) : manyStates(random);
        const auto state = static_cast<BlockStateId>(drawn);
        const std::size_t cell = anyCell(random);
        REQUIRE(section.setAt(cell, state) == reference[cell]);
        reference[cell] = state;
        if (step % 5000 == 0) {
            INFO("step " << step);
            REQUIRE(matches(section, reference));
            section.compact();
            REQUIRE(matches(section, reference));
        }
    }
}
