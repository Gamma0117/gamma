// Block changes from a real server World reaching the client's copy: which Changed updates apply, and which
// sections get new stamps.

#include "client/client_world.h"
#include "core/constants.h"

#include "../world/test_world.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>
#include <set>
#include <tuple>
#include <vector>

using namespace aurora;
using client::ClientWorld;
using client::SectionKey;
using world::BlockPos;
using world::ChunkPos;
using world::ChunkUpdate;

namespace {

void applyAll(ClientWorld& client, const std::vector<ChunkUpdate>& updates)
{
    for (const ChunkUpdate& update : updates) {
        client.apply(update);
    }
}

std::set<std::tuple<std::int32_t, std::int32_t, std::int32_t>> asSet(const std::vector<SectionKey>& keys)
{
    std::set<std::tuple<std::int32_t, std::int32_t, std::int32_t>> result;
    for (const SectionKey& key : keys) {
        result.emplace(key.pos.x, key.pos.z, key.section);
    }
    return result;
}

} // namespace

TEST_CASE("A Changed applies to the same load when it is newer", "[client][world][changes]")
{
    test::TestWorld server;
    ClientWorld client(1);
    const data::BlockStateId log = server.state("aurora:oak_log");

    // A load and a change of it in the same batch apply in order.
    std::vector<ChunkUpdate> updates = server.load({0, 0}, 2);
    REQUIRE(server.world.setBlock(BlockPos{1, 64, 1}, log));
    const std::vector<ChunkUpdate> firstChange = server.publish();
    REQUIRE(firstChange.size() == 1);
    updates.insert(updates.end(), firstChange.begin(), firstChange.end());
    applyAll(client, updates);
    REQUIRE(client.snapshot({0, 0}));
    CHECK(client.snapshot({0, 0})->revision() == 1);
    CHECK(client.snapshot({0, 0})->getBlock(1, 64, 1) == log);
    CHECK(client.isEligible({0, 0}));

    // A second change, then the first one again (a duplicate) and the second again: nothing goes back.
    REQUIRE(server.world.setBlock(BlockPos{2, 64, 2}, log));
    const std::vector<ChunkUpdate> secondChange = server.publish();
    REQUIRE(secondChange.size() == 1);
    applyAll(client, secondChange);
    CHECK(client.snapshot({0, 0})->revision() == 2);
    const std::optional<std::uint64_t> stampAfterSecond = client.stamp(SectionKey{{0, 0}, 8});
    applyAll(client, firstChange);
    applyAll(client, secondChange);
    CHECK(client.snapshot({0, 0})->revision() == 2);
    CHECK(client.stamp(SectionKey{{0, 0}, 8}) == stampAfterSecond); // Duplicates renew nothing (no re-meshing).
    CHECK(client.snapshot({0, 0})->getBlock(2, 64, 2) == log);

    // A Changed of another load (generation) of the same position is ignored, however new its revision.
    const auto changedOf = [](ChunkPos pos, std::uint64_t generation, std::uint64_t revision) {
        return ChunkUpdate{.kind = ChunkUpdate::Kind::Changed,
                           .pos = pos,
                           .generation = generation,
                           .snapshot = std::make_shared<const world::ChunkSnapshot>(
                               pos, generation, world::ChunkSnapshot::Sections{}, revision),
                           .changedSections = 1u << 7};
    };
    client.apply(changedOf({0, 0}, secondChange[0].generation + 1000, 50));
    CHECK(client.snapshot({0, 0})->revision() == 2);
    CHECK(client.snapshot({0, 0})->generation() == secondChange[0].generation);

    // A Changed for a chunk the client does not hold is ignored.
    client.apply(changedOf({40, 40}, secondChange[0].generation, 50));
    CHECK_FALSE(client.snapshot({40, 40}));
    // Only the two real changes marked sections: both in section 8 of (0, 0), so the same 27, listed once.
    CHECK(client.takeChangedSections().size() == 27);

    // Unload and reload: the old load's change cannot bring old blocks back.
    applyAll(client, server.load({20, 0}, 0));
    CHECK_FALSE(client.snapshot({0, 0}));
    applyAll(client, server.load({0, 0}, 2));
    REQUIRE(client.snapshot({0, 0}));
    const std::uint64_t reloaded = client.snapshot({0, 0})->generation();
    CHECK(reloaded > firstChange[0].generation);
    applyAll(client, secondChange);
    CHECK(client.snapshot({0, 0})->generation() == reloaded);
    CHECK(client.snapshot({0, 0})->revision() == 0);
    CHECK(client.snapshot({0, 0})->getBlock(2, 64, 2) == data::kAirState);
}

TEST_CASE("A changed section renews the stamps of the sections around it", "[client][world][changes]")
{
    test::TestWorld server;
    ClientWorld client(1);
    applyAll(client, server.load({0, 0}, 2));
    client.takeChangedChunks();
    CHECK(client.takeChangedSections().empty());
    const auto stampOf = [&](ChunkPos pos, std::int32_t section) { return *client.stamp({pos, section}); };
    const std::uint64_t before = stampOf({0, 0}, 7);
    const std::uint64_t untouched = stampOf({0, 0}, 3);

    // A corner cell of chunk (0, 0), section 7 (y 48..63): 3 x 3 chunks x 3 sections.
    REQUIRE(server.world.setBlock(BlockPos{0, 48, 0}, data::kAirState));
    applyAll(client, server.publish());
    const std::vector<SectionKey> marked = client.takeChangedSections();
    CHECK(marked.size() == 27);
    CHECK(asSet(marked).size() == 27); // Each once.
    for (const SectionKey& key : marked) {
        CHECK(std::abs(key.pos.x) <= 1);
        CHECK(std::abs(key.pos.z) <= 1);
        CHECK(key.section >= 6);
        CHECK(key.section <= 8);
        CHECK(stampOf(key.pos, key.section) > before);
    }
    CHECK(stampOf({0, 0}, 3) == untouched);
    CHECK(client.takeChangedChunks().empty()); // Eligibility and loads did not change.
    CHECK(client.isEligible({0, 0}));
    CHECK(client.takeChangedSections().empty());

    // The bottom section has no section below it; a chunk at the edge of what is held has fewer neighbours.
    REQUIRE(server.world.setBlock(BlockPos{-32, -64, 5}, data::kAirState)); // Chunk (-2, 0), section 0.
    applyAll(client, server.publish());
    const auto bottom = asSet(client.takeChangedSections());
    // (-2, 0) and its held neighbours (-2..-1) x (-1..1), sections 0 and 1: 6 chunks x 2 sections.
    CHECK(bottom.size() == 12);
    CHECK_FALSE(bottom.contains({-3, 0, 0}));

    // Two changed sections of one chunk that share neighbours are listed once each.
    REQUIRE(server.world.setBlock(BlockPos{5, 48, 5}, data::kAirState)); // Section 7.
    REQUIRE(server.world.setBlock(BlockPos{5, 47, 5}, data::kAirState)); // Section 6.
    applyAll(client, server.publish());
    const std::vector<SectionKey> pair = client.takeChangedSections();
    CHECK(pair.size() == 9 * 4); // Sections 5..8 of 9 chunks.
    CHECK(asSet(pair).size() == pair.size());
}
