#include "core/job_system.h"
#include "data/block_loader.h"
#include "data/flat_preset.h"
#include "data/player_movement.h"
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
#include <filesystem>
#include <functional>
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

TEST_CASE("The shipped test course has a wall a passage a pit and stairs that behave", "[server][player][physics]")
{
    // The game's own data: blocks, the flat preset with its course (south of the spawn) and the movement settings.
    const std::vector<data::DataPack> packs{{"aurora", std::filesystem::path(AURORA_SOURCE_DIR) / "game", true}};
    const data::BlockLoadResult blocks = data::loadBlocks(packs);
    REQUIRE(blocks.registry);
    const data::FlatPresetLoadResult preset = data::loadFlatPreset(packs, *blocks.registry);
    REQUIRE(preset.preset);
    const data::PlayerMovementLoadResult movement = data::loadPlayerMovement(packs);
    REQUIRE(movement.tuning);
    core::JobSystem jobs(2);
    world::World world(blocks.registry, jobs, world::makeFlatGenerator(preset.preset));
    loadAround(world, 1);
    const entity::CollisionShapes shapes = entity::CollisionShapes::fromRegistry(*blocks.registry);
    const server::WorldCollisionView view(world);
    const entity::CollisionWorld collision{view, shapes};

    // Walks south (yaw 180) from `feet` for `ticks` ticks; `each` sees the motion after every tick.
    std::uint32_t sequence = 0;
    const auto walkSouth = [&](server::ServerPlayer& player, int ticks, bool jump,
                               const std::function<void(const entity::PlayerMotion&)>& each = {}) {
        for (int i = 0; i < ticks; ++i) {
            player.receive({.kind = server::PlayerMessage::Kind::Input,
                            .input = {++sequence, {.forward = 1, .jump = jump, .yaw = 180.0f}}});
            player.tick(collision);
            if (each) {
                each(player.motion());
            }
        }
    };
    const auto spawnAt = [&](double x, double z) {
        auto player = std::make_unique<server::ServerPlayer>(movement.tuning);
        player->spawn({x, 64.0, z});
        sequence = 0;
        return player;
    };

    SECTION("The wall (x 5..7, z 5, two blocks high) stops walking and jumping")
    {
        const auto player = spawnAt(6.5, 2.5);
        walkSouth(*player, 40, false);
        CHECK(player->motion().position.z == Approx(4.7).margin(1e-6));
        double furthest = 0.0;
        walkSouth(*player, 60, true, [&](const entity::PlayerMotion& motion) {
            furthest = std::max(furthest, motion.position.z);
        });
        CHECK(furthest <= 4.7 + 1e-6);
    }
    SECTION("The passage (x -5, z 5..9, roofed at y 66) fits the player and holds a jump down")
    {
        const auto player = spawnAt(-4.5, 0.5);
        double highestInside = 0.0;
        walkSouth(*player, 80, true, [&](const entity::PlayerMotion& motion) {
            if (motion.position.z > 5.3 && motion.position.z < 9.7) {
                highestInside = std::max(highestInside, motion.position.y);
            }
        });
        CHECK(player->motion().position.z > 11.0); // Through and out.
        CHECK(player->motion().position.x == -4.5);
        CHECK(highestInside <= 66.0 - movement.tuning->height + 1e-6); // The head stops at the roof.
        CHECK(highestInside > 64.0);                                   // But it did jump.
    }
    SECTION("The pit (x 9..11, z 5..7, two deep, a step at its south end) is climbed out by jumping")
    {
        const auto player = spawnAt(10.5, 2.5);
        double lowest = 64.0;
        walkSouth(*player, 40, false, [&](const entity::PlayerMotion& motion) {
            lowest = std::min(lowest, motion.position.y);
        });
        CHECK(lowest == Approx(62.0).margin(1e-6)); // Fell to the bottom.
        CHECK(player->motion().position.z < 7.0);   // Held by the step without jumping.
        walkSouth(*player, 60, true);
        CHECK(player->motion().position.z > 9.0); // Out on the far side.
        CHECK(player->motion().position.y >= 64.0 - 1e-6);
    }
    SECTION("The cobblestone stairs (x 0..2, one block per step) are climbed by jumping, not by walking")
    {
        const auto player = spawnAt(1.5, 2.5);
        walkSouth(*player, 40, false);
        CHECK(player->motion().position.z == Approx(4.7).margin(1e-6)); // A full block is not a step.
        double highestStanding = 0.0;
        walkSouth(*player, 40, true, [&](const entity::PlayerMotion& motion) {
            if (motion.onGround) {
                highestStanding = std::max(highestStanding, motion.position.y);
            }
        });
        CHECK(highestStanding == Approx(67.0).margin(1e-6)); // Stood on top of the third step.
    }
}
