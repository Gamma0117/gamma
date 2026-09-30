#include "world/chunk.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>

using aurora::world::BlockStateId;
using aurora::world::Chunk;
using aurora::world::ChunkPos;
using aurora::world::ChunkStatus;
using aurora::world::sectionIndex;

namespace {

constexpr BlockStateId kAir = 0;
constexpr BlockStateId kStone = 7;
constexpr BlockStateId kDirt = 2;

} // namespace

TEST_CASE("A new chunk is all air with no sections", "[world][chunk]")
{
    const Chunk chunk(ChunkPos{-3, 5});
    CHECK(chunk.pos() == ChunkPos{-3, 5});
    CHECK(chunk.status() == ChunkStatus::Empty);
    CHECK(chunk.sectionCount() == 0);
    CHECK(chunk.getBlock(0, -64, 0) == kAir);
    CHECK(chunk.getBlock(15, 319, 15) == kAir);
    CHECK(chunk.height(7, 7) == Chunk::kNoHeight);
    CHECK(Chunk::kNoHeight == -65);
}

TEST_CASE("Chunk sections are created on the first block and dropped when all air", "[world][chunk]")
{
    Chunk chunk(ChunkPos{0, 0});
    CHECK(chunk.setBlock(1, 100, 2, kAir) == kAir); // Air on air: no section needed.
    CHECK(chunk.sectionCount() == 0);

    CHECK(chunk.setBlock(1, 100, 2, kStone) == kAir);
    CHECK(chunk.sectionCount() == 1);
    REQUIRE(chunk.section(sectionIndex(100)) != nullptr);
    CHECK(chunk.section(sectionIndex(100))->nonAirCount() == 1);
    CHECK(chunk.getBlock(1, 100, 2) == kStone);

    CHECK(chunk.setBlock(1, 101, 2, kDirt) == kAir); // Same section (96..111).
    CHECK(chunk.sectionCount() == 1);
    CHECK(chunk.setBlock(1, -64, 2, kDirt) == kAir);
    CHECK(chunk.setBlock(1, 319, 2, kDirt) == kAir);
    CHECK(chunk.sectionCount() == 3);

    CHECK(chunk.setBlock(1, 100, 2, kAir) == kStone);
    CHECK(chunk.sectionCount() == 3); // y 101 is still there.
    CHECK(chunk.setBlock(1, 101, 2, kAir) == kDirt);
    CHECK(chunk.sectionCount() == 2);
    CHECK(chunk.section(sectionIndex(100)) == nullptr);
    CHECK(chunk.getBlock(1, 101, 2) == kAir);
}

TEST_CASE("The height map follows the highest non-air block", "[world][chunk]")
{
    Chunk chunk(ChunkPos{0, 0});

    chunk.setBlock(4, 10, 6, kStone);
    CHECK(chunk.height(4, 6) == 10);
    CHECK(chunk.height(6, 4) == Chunk::kNoHeight); // Columns are separate.

    chunk.setBlock(4, 5, 6, kStone); // Below the top: no change.
    CHECK(chunk.height(4, 6) == 10);
    chunk.setBlock(4, 200, 6, kDirt); // Above, several sections up.
    CHECK(chunk.height(4, 6) == 200);
    chunk.setBlock(4, 200, 6, kStone); // Replacing the top keeps it.
    CHECK(chunk.height(4, 6) == 200);

    chunk.setBlock(4, 200, 6, kAir); // Removing the top searches down.
    CHECK(chunk.height(4, 6) == 10);
    chunk.setBlock(4, 5, 6, kAir); // Removing a lower block: no change.
    CHECK(chunk.height(4, 6) == 10);
    chunk.setBlock(4, 10, 6, kAir);
    CHECK(chunk.height(4, 6) == Chunk::kNoHeight);

    chunk.setBlock(0, -64, 0, kStone);
    CHECK(chunk.height(0, 0) == -64);
    chunk.setBlock(0, 319, 0, kStone);
    CHECK(chunk.height(0, 0) == 319);
    chunk.setBlock(0, 319, 0, kAir);
    CHECK(chunk.height(0, 0) == -64);
    chunk.setBlock(0, -64, 0, kAir);
    CHECK(chunk.height(0, 0) == Chunk::kNoHeight);
}

TEST_CASE("Rebuilding the height map matches the kept one", "[world][chunk]")
{
    Chunk chunk(ChunkPos{0, 0});
    for (std::int32_t x = 0; x < 16; ++x) {
        for (std::int32_t z = 0; z < 16; ++z) {
            const std::int32_t top = -64 + (x * 37 + z * 11) % 384;
            chunk.setBlock(x, top, z, kStone);
            chunk.setBlock(x, top - 20 < -64 ? -64 : top - 20, z, kDirt);
        }
    }
    chunk.setBlock(3, 0, 3, kAir);
    std::int32_t kept[16][16];
    for (std::int32_t x = 0; x < 16; ++x) {
        for (std::int32_t z = 0; z < 16; ++z) {
            kept[x][z] = chunk.height(x, z);
        }
    }
    chunk.rebuildHeightMap();
    for (std::int32_t x = 0; x < 16; ++x) {
        for (std::int32_t z = 0; z < 16; ++z) {
            INFO("column " << x << ", " << z);
            CHECK(chunk.height(x, z) == kept[x][z]);
        }
    }
}

TEST_CASE("setSection stores an empty section as none", "[world][chunk]")
{
    Chunk chunk(ChunkPos{0, 0});
    chunk.setSection(3, std::make_unique<aurora::world::ChunkSection>());
    CHECK(chunk.section(3) == nullptr);
    chunk.setSection(3, std::make_unique<aurora::world::ChunkSection>(aurora::world::ChunkSection::filled(kStone)));
    CHECK(chunk.sectionCount() == 1);
    chunk.rebuildHeightMap();
    CHECK(chunk.height(0, 0) == aurora::world::sectionBottomY(3) + 15);
}
