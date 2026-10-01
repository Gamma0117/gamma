#include "client/client_world.h"
#include "render/upload_queue.h"

#include "../client/client_test_support.h"

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <vector>

using namespace aurora::render;
using aurora::client::ClientWorld;
using aurora::client::MeshKey;
using aurora::client::ReadyMesh;
using aurora::test::loaded;
using aurora::test::loadSquare;
using aurora::test::unloaded;
using aurora::world::ChunkPos;

namespace {

// A mesh of `words` vertex words and as many indices, for the current key of (pos, section 7).
ReadyMesh readyMesh(const ClientWorld& world, ChunkPos pos, std::size_t words = 50)
{
    const std::optional<MeshKey> key = world.currentKey({pos, 7});
    REQUIRE(key);
    ReadyMesh ready{*key, {}};
    ready.mesh.vertexWords.assign(words, 0);
    ready.mesh.indices.assign(words, 0);
    return ready;
}

} // namespace

TEST_CASE("Queued meshes upload in order within the byte budget", "[render][upload]")
{
    ClientWorld world(1);
    loadSquare(world, {0, 0}, 2);
    UploadQueue queue;
    // 400 bytes each.
    queue.push({readyMesh(world, {0, 0}), readyMesh(world, {1, 0}), readyMesh(world, {0, 1})});
    CHECK(meshBytes(readyMesh(world, {0, 0}).mesh) == 400);

    const std::vector<ReadyMesh> first = queue.take(world, 500); // 400 < 500, so a second one goes too.
    REQUIRE(first.size() == 2);
    CHECK(first[0].key.section.pos == ChunkPos{0, 0});
    CHECK(first[1].key.section.pos == ChunkPos{1, 0});
    CHECK(queue.size() == 1);

    CHECK(queue.take(world, 1).size() == 1); // One always goes, however small the budget.
    CHECK(queue.take(world, 1).empty());
}

TEST_CASE("A queued mesh that loses its chunk or eligibility is never uploaded", "[render][upload]")
{
    ClientWorld world(1);
    loadSquare(world, {0, 0}, 2);
    UploadQueue queue;
    queue.push({readyMesh(world, {1, 1}), readyMesh(world, {-1, -1})});

    SECTION("A neighbour leaves")
    {
        world.apply(unloaded({2, 2}, world.snapshot({2, 2})->generation())); // (1, 1) loses a neighbour.
    }
    SECTION("The chunk is reloaded")
    {
        world.apply(unloaded({1, 1}, world.snapshot({1, 1})->generation()));
        world.apply(loaded({1, 1}, 900));
    }
    SECTION("The center moves away")
    {
        world.setCenter({-1, -1}); // (1, 1) is 2 away now.
    }
    const std::vector<ReadyMesh> taken = queue.take(world, 1 << 20);
    REQUIRE(taken.size() == 1);
    CHECK(taken[0].key.section.pos == ChunkPos{-1, -1});
    CHECK(queue.size() == 0);
}

TEST_CASE("A mesh on the GPU stays drawable while its chunk load stays eligible", "[render][upload]")
{
    ClientWorld world(1);
    loadSquare(world, {0, 0}, 2);
    const MeshKey key = *world.currentKey({{0, 0}, 7});
    CHECK(isDrawable(world, key));

    world.apply(loaded({5, 5}, 500)); // Unrelated.
    CHECK(isDrawable(world, key));

    world.apply(loaded({-1, 0}, 501)); // A neighbour replaced: new stamp, but the old mesh may stay until replaced.
    CHECK_FALSE(world.isCurrent(key));
    CHECK(isDrawable(world, key));

    world.setCenter({3, 0}); // Out of range.
    CHECK_FALSE(isDrawable(world, key));
    world.setCenter({0, 0});
    CHECK(isDrawable(world, key));

    world.apply(unloaded({0, 0}, key.generation));
    world.apply(loaded({0, 0}, 777));
    CHECK_FALSE(isDrawable(world, key)); // Another load of the same position.
}
