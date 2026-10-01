#include "client/client_world.h"

#include "client_test_support.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

using namespace aurora::client;
using aurora::test::loaded;
using aurora::test::loadSquare;
using aurora::test::unloaded;
using aurora::world::ChunkPos;

namespace {

MeshKey keyOf(const ClientWorld& world, ChunkPos pos, std::int32_t section = 7)
{
    const std::optional<MeshKey> key = world.currentKey({pos, section});
    REQUIRE(key);
    return *key;
}

bool contains(const std::vector<ChunkPos>& positions, ChunkPos pos)
{
    return std::ranges::find(positions, pos) != positions.end();
}

} // namespace

TEST_CASE("A chunk is eligible only within the render distance and with all eight neighbours", "[client][world]")
{
    ClientWorld world(1);
    loadSquare(world, {0, 0}, 1); // 3 x 3: only the middle one has all its neighbours.
    CHECK(world.chunkCount() == 9);
    CHECK(world.eligibleCount() == 1);
    CHECK(world.isEligible({0, 0}));
    CHECK_FALSE(world.isEligible({1, 0}));
    CHECK_FALSE(world.isAreaComplete());
    CHECK_FALSE(world.currentKey({{1, 0}, 7}));
    CHECK_FALSE(world.neighbourhood({1, 0}));

    loadSquare(world, {0, 0}, 2, 100); // 5 x 5: the inner 3 x 3 now qualifies; the outer ring is beyond distance 1.
    CHECK(world.eligibleCount() == 9);
    CHECK(world.isEligible({1, 1}));
    CHECK_FALSE(world.isEligible({2, 0}));
    CHECK(world.isAreaComplete());

    const auto chunks = world.neighbourhood({1, 0});
    REQUIRE(chunks);
    CHECK((*chunks)[4]->pos() == ChunkPos{1, 0});
    CHECK((*chunks)[0]->pos() == ChunkPos{0, -1}); // dx -1, dz -1
    CHECK((*chunks)[8]->pos() == ChunkPos{2, 1});  // dx +1, dz +1
}

TEST_CASE("Updates apply only to the load they are about", "[client][world]")
{
    ClientWorld world(0);
    world.apply(loaded({0, 0}, 5));
    world.apply(unloaded({0, 0}, 4)); // Ends a load this world does not hold.
    CHECK(world.chunkCount() == 1);
    world.apply(loaded({0, 0}, 3)); // Older than what is held.
    CHECK(world.snapshot({0, 0})->generation() == 5);
    world.apply(loaded({0, 0}, 9)); // Newer replaces.
    CHECK(world.snapshot({0, 0})->generation() == 9);
    world.apply(unloaded({0, 0}, 5)); // The replaced load's unload is stale now.
    CHECK(world.chunkCount() == 1);
    world.apply(unloaded({0, 0}, 9));
    CHECK(world.chunkCount() == 0);
    world.apply(unloaded({7, 7}, 1)); // Unknown position: ignored.
    CHECK(world.chunkCount() == 0);
}

TEST_CASE("Stamps change with inputs and eligibility and old keys never come back", "[client][world]")
{
    ClientWorld world(1);
    loadSquare(world, {0, 0}, 2);
    world.takeChangedChunks();
    const MeshKey original = keyOf(world, {0, 0});
    CHECK(world.isCurrent(original));

    SECTION("A neighbour leaving makes the chunk ineligible")
    {
        world.apply(unloaded({1, 1}, world.snapshot({1, 1})->generation()));
        CHECK_FALSE(world.isEligible({0, 0}));
        CHECK_FALSE(world.isCurrent(original));
        CHECK(contains(world.takeChangedChunks(), {0, 0}));

        world.apply(loaded({1, 1}, 500)); // Back again: eligible with a new stamp, not the old one.
        const MeshKey again = keyOf(world, {0, 0});
        CHECK(again.stamp > original.stamp);
        CHECK_FALSE(world.isCurrent(original));
        CHECK(world.isCurrent(again));
    }
    SECTION("A neighbour replaced by a newer load changes the stamp")
    {
        world.apply(loaded({-1, 0}, 500));
        CHECK(world.isEligible({0, 0}));
        CHECK_FALSE(world.isCurrent(original));
        CHECK(keyOf(world, {0, 0}).stamp > original.stamp);
    }
    SECTION("The chunk itself reloaded gets a new generation")
    {
        world.apply(unloaded({0, 0}, original.generation));
        world.apply(loaded({0, 0}, 501));
        const MeshKey reloaded = keyOf(world, {0, 0});
        CHECK(reloaded.generation == 501);
        CHECK_FALSE(world.isCurrent(original));
    }
    SECTION("Leaving and re-entering the render distance")
    {
        world.setCenter({3, 0}); // (0, 0) is 3 away now.
        CHECK_FALSE(world.isEligible({0, 0}));
        CHECK_FALSE(world.isCurrent(original));
        world.setCenter({0, 0});
        CHECK(world.isEligible({0, 0}));
        CHECK_FALSE(world.isCurrent(original));
        CHECK(keyOf(world, {0, 0}).stamp > original.stamp);
    }
    SECTION("Unrelated changes keep the key")
    {
        world.apply(loaded({5, 5}, 600));
        world.setCenter({0, 0});
        CHECK(world.isCurrent(original));
        CHECK_FALSE(contains(world.takeChangedChunks(), {0, 0}));
    }
}

TEST_CASE("Each changed chunk is reported once per take", "[client][world]")
{
    ClientWorld world(1);
    loadSquare(world, {0, 0}, 2);
    const std::vector<ChunkPos> changed = world.takeChangedChunks();
    CHECK(changed.size() == 25);
    std::vector<ChunkPos> sorted = changed;
    std::ranges::sort(sorted, [](ChunkPos a, ChunkPos b) { return a.x != b.x ? a.x < b.x : a.z < b.z; });
    CHECK(std::ranges::adjacent_find(sorted) == sorted.end());
    CHECK(world.takeChangedChunks().empty());

    world.apply(unloaded({2, 2}, world.snapshot({2, 2})->generation()));
    const std::vector<ChunkPos> after = world.takeChangedChunks();
    CHECK(contains(after, {2, 2}));
    CHECK(contains(after, {1, 1})); // Lost a neighbour.
    CHECK_FALSE(contains(after, {-1, -1}));
}
