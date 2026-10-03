#include "core/constants.h"
#include "world/chunk.h"

#include "test_world.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <vector>

using namespace aurora::world;
using aurora::data::kAirState;

namespace {

using Fixture = aurora::test::TestWorld;

std::uint32_t bit(std::int32_t y)
{
    return 1u << sectionIndex(y);
}

} // namespace

TEST_CASE("A block change is published as a Changed update with the next revision", "[world][changes]")
{
    Fixture fixture;
    const std::vector<ChunkUpdate> loads = fixture.load(ChunkPos{0, 0}, 0);
    REQUIRE(loads.size() == 1);
    const std::shared_ptr<const ChunkSnapshot> loaded = loads[0].snapshot;
    REQUIRE(loaded);
    CHECK(loaded->revision() == 0);
    const BlockStateId grass = fixture.state("aurora:grass_block");
    const BlockStateId log = fixture.state("aurora:oak_log");

    // Writing what is already there changes nothing.
    REQUIRE(fixture.world.setBlock(BlockPos{3, 63, 4}, grass));
    REQUIRE(fixture.world.setBlock(BlockPos{3, 100, 4}, kAirState));
    CHECK(fixture.publish().empty());

    // Two sections change; the section at y 63 twice: one update, each section copied once with its final blocks.
    REQUIRE(fixture.world.setBlock(BlockPos{3, 63, 4}, log));
    REQUIRE(fixture.world.setBlock(BlockPos{3, 63, 4}, kAirState));
    REQUIRE(fixture.world.setBlock(BlockPos{5, 100, 6}, log));
    const std::vector<ChunkUpdate> first = fixture.publish();
    REQUIRE(first.size() == 1);
    const ChunkUpdate& changed = first[0];
    CHECK(changed.kind == ChunkUpdate::Kind::Changed);
    CHECK(changed.pos == ChunkPos{0, 0});
    CHECK(changed.generation == loads[0].generation);
    CHECK(changed.changedSections == (bit(63) | bit(100)));
    REQUIRE(changed.snapshot);
    CHECK(changed.snapshot->generation() == loads[0].generation);
    CHECK(changed.snapshot->revision() == 1);
    CHECK(changed.snapshot->getBlock(3, 63, 4) == kAirState);
    CHECK(changed.snapshot->getBlock(5, 100, 6) == log);
    CHECK(changed.snapshot->getBlock(4, 63, 4) == grass);
    // Untouched sections are shared, not copied; changed ones are new copies.
    for (std::int32_t index = 0; index < aurora::core::kSectionsPerChunk; ++index) {
        INFO("section " << index);
        const bool dirty = (changed.changedSections >> index & 1u) != 0;
        if (!dirty) {
            CHECK(changed.snapshot->section(index) == loaded->section(index));
        } else {
            CHECK(changed.snapshot->section(index) != loaded->section(index));
        }
    }
    // The old snapshot keeps its blocks.
    CHECK(loaded->getBlock(3, 63, 4) == grass);
    CHECK(loaded->getBlock(5, 100, 6) == kAirState);
    CHECK(loaded->section(sectionIndex(100)) == nullptr);

    // A -> B -> A before publishing is still one revision for that section; its content is A again.
    REQUIRE(fixture.world.setBlock(BlockPos{4, 63, 4}, log));
    REQUIRE(fixture.world.setBlock(BlockPos{4, 63, 4}, grass));
    const std::vector<ChunkUpdate> second = fixture.publish();
    REQUIRE(second.size() == 1);
    CHECK(second[0].changedSections == bit(63));
    CHECK(second[0].snapshot->revision() == 2);
    CHECK(second[0].snapshot->getBlock(4, 63, 4) == grass);
    CHECK(second[0].snapshot->section(sectionIndex(100)) == changed.snapshot->section(sectionIndex(100)));

    // Removing the last block of a section leaves it null (all air) in the snapshot.
    REQUIRE(fixture.world.setBlock(BlockPos{5, 100, 6}, kAirState));
    const std::vector<ChunkUpdate> third = fixture.publish();
    REQUIRE(third.size() == 1);
    CHECK(third[0].changedSections == bit(100));
    CHECK(third[0].snapshot->revision() == 3);
    CHECK(third[0].snapshot->section(sectionIndex(100)) == nullptr);
    CHECK(changed.snapshot->getBlock(5, 100, 6) == log); // Older snapshots still unchanged.

    // Nothing left to publish.
    CHECK(fixture.publish().empty());
}

TEST_CASE("Changes in several chunks are published in the order they first changed", "[world][changes]")
{
    Fixture fixture;
    REQUIRE(fixture.load(ChunkPos{0, 0}, 1).size() == 9);
    const BlockStateId log = fixture.state("aurora:oak_log");

    // Chunk (-1, -1) first (negative coordinates), then (1, 0), then (-1, -1) again.
    REQUIRE(fixture.world.setBlock(BlockPos{-1, 70, -16}, log));
    REQUIRE(fixture.world.setBlock(BlockPos{16, 70, 0}, log));
    REQUIRE(fixture.world.setBlock(BlockPos{-16, -64, -1}, kAirState));
    const std::vector<ChunkUpdate> updates = fixture.publish();
    REQUIRE(updates.size() == 2);
    CHECK(updates[0].pos == ChunkPos{-1, -1});
    CHECK(updates[0].changedSections == (bit(70) | bit(-64)));
    CHECK(updates[0].snapshot->getBlock(15, 70, 0) == log);
    CHECK(updates[0].snapshot->getBlock(0, -64, 15) == kAirState);
    CHECK(updates[1].pos == ChunkPos{1, 0});
    CHECK(updates[1].changedSections == bit(70));
    CHECK(updates[1].snapshot->revision() == 1);
}

TEST_CASE("Unloading drops unpublished changes and a reload starts at revision 0", "[world][changes]")
{
    Fixture fixture;
    const std::vector<ChunkUpdate> loads = fixture.load(ChunkPos{0, 0}, 0);
    REQUIRE(loads.size() == 1);
    const BlockStateId log = fixture.state("aurora:oak_log");
    const BlockStateId grass = fixture.state("aurora:grass_block");
    CHECK(fixture.world.generation(ChunkPos{0, 0}) == loads[0].generation);
    CHECK_FALSE(fixture.world.generation(ChunkPos{5, 5}));

    // A published change stays in the update list ahead of the Unloaded that follows it.
    REQUIRE(fixture.world.setBlock(BlockPos{1, 64, 1}, log));
    fixture.world.publishChanges();
    // An unpublished change is dropped with the chunk.
    REQUIRE(fixture.world.setBlock(BlockPos{2, 64, 2}, log));
    fixture.world.ensureLoaded(ChunkPos{10, 0}, 0); // (0, 0) leaves the keep range.
    fixture.world.publishChanges();
    std::vector<ChunkUpdate> updates = fixture.world.takeChunkUpdates();
    REQUIRE(updates.size() == 2);
    CHECK(updates[0].kind == ChunkUpdate::Kind::Changed);
    CHECK(updates[0].snapshot->revision() == 1);
    CHECK(updates[1].kind == ChunkUpdate::Kind::Unloaded);
    CHECK(updates[1].generation == loads[0].generation);
    CHECK_FALSE(fixture.world.generation(ChunkPos{0, 0}));
    CHECK_FALSE(fixture.world.setBlock(BlockPos{2, 64, 2}, log)); // Not loaded any more.

    // Back again: a new load with a larger generation, revision 0, the generator's blocks (edits are not kept, P0-12).
    fixture.load(ChunkPos{10, 0}, 0);
    std::vector<ChunkUpdate> reloads = fixture.load(ChunkPos{0, 0}, 0);
    std::erase_if(reloads, [](const ChunkUpdate& update) { return update.pos != ChunkPos{0, 0}; });
    REQUIRE(reloads.size() == 1);
    CHECK(reloads[0].kind == ChunkUpdate::Kind::Loaded);
    CHECK(reloads[0].generation > loads[0].generation);
    CHECK(reloads[0].snapshot->revision() == 0);
    CHECK(reloads[0].snapshot->getBlock(1, 64, 1) == kAirState);
    CHECK(reloads[0].snapshot->getBlock(1, 63, 1) == grass);
    CHECK(fixture.world.generation(ChunkPos{0, 0}) == reloads[0].generation);
    CHECK(fixture.publish().empty());
}
