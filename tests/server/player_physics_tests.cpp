#include "core/job_system.h"
#include "entity/collision_shapes.h"
#include "server/server_player.h"
#include "server/world_collision_view.h"
#include "world/flat_generator.h"
#include "world/world.h"

#include "../data/data_test_support.h"
#include "../entity/entity_test_support.h"
#include "../world/world_test_support.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>

using namespace aurora;
using Catch::Approx;

namespace {

// Test registry states with partial shapes for this test: oak_log[axis=z] is a half block, oak_log[axis=x] a stair
// climbed towards +x. Neither appears in the flat layers.
std::shared_ptr<const entity::CollisionShapes> courseShapes(const data::BlockRegistry& registry)
{
    entity::CollisionShapes shapes = entity::CollisionShapes::fromRegistry(registry);
    const entity::Aabb half[] = {{{0.0, 0.0, 0.0}, {1.0, 0.5, 1.0}}};
    const entity::Aabb stair[] = {{{0.0, 0.0, 0.0}, {1.0, 0.5, 1.0}}, {{0.5, 0.5, 0.0}, {1.0, 1.0, 1.0}}};
    REQUIRE_FALSE(shapes.setShape(test::stateOf(registry, "aurora:oak_log[axis=z]"), half));
    REQUIRE_FALSE(shapes.setShape(test::stateOf(registry, "aurora:oak_log[axis=x]"), stair));
    return std::make_shared<const entity::CollisionShapes>(std::move(shapes));
}

} // namespace

TEST_CASE("The server player climbs a half block and four stairs in a generated world", "[server][player][physics]")
{
    const auto registry = test::makeTestRegistry();
    auto preset = std::make_shared<data::FlatPreset>(*test::makeStandardFlatPreset(*registry));
    const data::BlockStateId halfBlock = test::stateOf(*registry, "aurora:oak_log[axis=z]");
    const data::BlockStateId stairBlock = test::stateOf(*registry, "aurora:oak_log[axis=x]");
    const data::BlockStateId stone = test::stateOf(*registry, "aurora:stone");
    preset->boxes.push_back({halfBlock, {3, 64, 0}, {3, 64, 0}});
    for (std::int32_t i = 0; i < 4; ++i) {
        preset->boxes.push_back({stairBlock, {6 + i, 64 + i, 0}, {6 + i, 64 + i, 0}});
    }
    preset->boxes.push_back({stone, {10, 64, 0}, {15, 67, 0}}); // Landing at the top: y 68.

    core::JobSystem jobs(2);
    world::World world(registry, jobs, world::makeFlatGenerator(preset));
    world.ensureLoaded({0, 0}, 1);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (world.stats().loadedChunks < 9 && std::chrono::steady_clock::now() < deadline) {
        world.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(world.stats().loadedChunks == 9);

    const auto shapes = courseShapes(*registry);
    const server::WorldCollisionView view(world);
    const entity::CollisionWorld collision{view, *shapes};
    server::ServerPlayer player(std::make_shared<const data::PlayerMovementTuning>(test::standardTuning()));
    player.spawn({0.5, 64.0, 0.5});

    double onHalfBlock = 0.0;
    double highest = 0.0;
    for (std::uint32_t sequence = 1; sequence <= 62; ++sequence) {
        player.receive({.kind = server::PlayerMessage::Kind::Input,
                        .input = {sequence, {.forward = 1, .yaw = 90.0f}}});
        player.tick(collision);
        const glm::dvec3& position = player.motion().position;
        if (position.x > 3.3 && position.x < 3.7) {
            onHalfBlock = std::max(onHalfBlock, position.y);
        }
        highest = std::max(highest, position.y);
        CHECK_FALSE(player.stats().frozen);
    }
    // No jump was ever sent: the half block and every stair were stepped up.
    CHECK(onHalfBlock == Approx(64.5).margin(1e-9));
    CHECK(highest == Approx(68.0).margin(1e-9));
    CHECK(player.motion().position.y == Approx(68.0).margin(1e-9));
    CHECK(player.motion().position.x > 10.0);
    CHECK(player.motion().onGround);
}

namespace {

// Loads the square of `radius` around chunk (0, 0) and waits until every chunk of it is Loaded.
void loadAround(world::World& world, std::int32_t radius)
{
    world.ensureLoaded({0, 0}, radius);
    const std::size_t wanted = static_cast<std::size_t>((2 * radius + 1) * (2 * radius + 1));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (world.stats().loadedChunks < wanted && std::chrono::steady_clock::now() < deadline) {
        world.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(world.stats().loadedChunks == wanted);
}

} // namespace

TEST_CASE("The spawn waits for the spawn chunk and its neighbours and stands on the highest block", "[server][player]")
{
    const auto registry = test::makeTestRegistry();
    const data::BlockStateId stone = test::stateOf(*registry, "aurora:stone");
    auto preset = std::make_shared<data::FlatPreset>(*test::makeStandardFlatPreset(*registry));
    preset->boxes.push_back({stone, {0, 64, 0}, {0, 66, 0}});                    // A pillar on (0, 0).
    preset->boxes.push_back({stone, {16, 64, 0}, {16, 70, 0}});                  // A taller one in chunk (1, 0).
    preset->boxes.push_back({data::kAirState, {8, -64, 8}, {8, 319, 8}}); // An empty column.
    core::JobSystem jobs(2);
    world::World world(registry, jobs, world::makeFlatGenerator(preset));
    data::PlayerMovementTuning tuning = test::standardTuning();

    loadAround(world, 0); // Only the spawn chunk.
    CHECK_FALSE(server::findSpawn(world, {0, 0}, tuning));
    loadAround(world, 1);
    CHECK(server::findSpawn(world, {0, 0}, tuning) == glm::dvec3(0.5, 67.0, 0.5));
    CHECK(server::findSpawn(world, {5, 5}, tuning) == glm::dvec3(5.5, 64.0, 5.5));
    CHECK(server::findSpawn(world, {15, 0}, tuning) == glm::dvec3(15.5, 64.0, 0.5));
    // A player three blocks wide at x 15 covers x 14..16: the pillar in chunk (1, 0) is under it too.
    tuning.width = 3.0;
    CHECK(server::findSpawn(world, {15, 0}, tuning) == glm::dvec3(15.5, 71.0, 0.5));
    // Chunk (2, 0) is not loaded: no spawn at x 31 (its neighbour), whatever else is there.
    CHECK_FALSE(server::findSpawn(world, {31, 0}, tuning));
    // An empty column: the bottom of the world.
    tuning.width = 0.6;
    CHECK(server::findSpawn(world, {8, 8}, tuning) == glm::dvec3(8.5, -64.0, 8.5));
}
