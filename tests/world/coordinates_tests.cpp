#include "world/coordinates.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>
#include <unordered_set>

using namespace aurora::world;

TEST_CASE("Chunk and local coordinates round towards negative infinity", "[world][coordinates]")
{
    struct Case {
        std::int32_t block;
        std::int32_t chunk;
        std::int32_t local;
    };
    for (const Case& c : {Case{0, 0, 0}, Case{15, 0, 15}, Case{16, 1, 0}, Case{-1, -1, 15}, Case{-16, -1, 0},
                          Case{-17, -2, 15}, Case{std::numeric_limits<std::int32_t>::max(), 134217727, 15},
                          Case{std::numeric_limits<std::int32_t>::min(), -134217728, 0}}) {
        INFO("block " << c.block);
        CHECK(chunkCoord(c.block) == c.chunk);
        CHECK(localCoord(c.block) == c.local);
        CHECK(chunkOrigin(c.chunk) + c.local == c.block);
    }
    CHECK(chunkPosOf(BlockPos{-1, 70, 16}) == ChunkPos{-1, 1});
    CHECK(chunkOrigin(-1) == -16);
}

TEST_CASE("World height is -64 up to 319 in 24 sections", "[world][coordinates]")
{
    CHECK(isInWorldHeight(-64));
    CHECK(isInWorldHeight(319));
    CHECK_FALSE(isInWorldHeight(-65));
    CHECK_FALSE(isInWorldHeight(320));

    CHECK(sectionIndex(-64) == 0);
    CHECK(localCoord(-64) == 0);
    CHECK(sectionIndex(-49) == 0);
    CHECK(sectionIndex(-48) == 1);
    CHECK(sectionIndex(63) == 7);
    CHECK(sectionIndex(319) == 23);
    CHECK(localCoord(319) == 15);
    CHECK(sectionBottomY(0) == -64);
    CHECK(sectionBottomY(23) == 304);
}

TEST_CASE("Chunk distance is the square distance", "[world][coordinates]")
{
    CHECK(chunkDistance({0, 0}, {0, 0}) == 0);
    CHECK(chunkDistance({0, 0}, {3, -2}) == 3);
    CHECK(chunkDistance({-5, 4}, {-1, -4}) == 8);
    constexpr std::int32_t kMin = std::numeric_limits<std::int32_t>::min();
    constexpr std::int32_t kMax = std::numeric_limits<std::int32_t>::max();
    CHECK(chunkDistance({kMin, 0}, {kMax, 0}) == 4294967295LL); // No overflow.
}

TEST_CASE("Chunk positions hash apart", "[world][coordinates]")
{
    std::unordered_set<ChunkPos, ChunkPosHash> positions;
    for (std::int32_t x = -20; x <= 20; ++x) {
        for (std::int32_t z = -20; z <= 20; ++z) {
            positions.insert({x, z});
        }
    }
    CHECK(positions.size() == 41 * 41);
    CHECK(positions.contains({-20, 20}));
    CHECK_FALSE(positions.contains({21, 0}));
}
