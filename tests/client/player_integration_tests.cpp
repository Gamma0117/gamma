#include "client/client_collision_view.h"
#include "client/client_world.h"
#include "client/local_player.h"
#include "core/job_system.h"
#include "core/tick_scheduler.h"
#include "entity/collision_shapes.h"
#include "server/integrated_server.h"
#include "world/chunk.h"
#include "world/chunk_snapshot.h"
#include "world/flat_generator.h"

#include "../data/data_test_support.h"
#include "../entity/entity_test_support.h"
#include "../world/world_test_support.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <thread>

using namespace aurora;
using namespace std::chrono_literals;
using Catch::Approx;

TEST_CASE("The client's copy of the world climbs a half block and stairs like the server's",
          "[client][player][physics]")
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
    preset->boxes.push_back({stone, {10, 64, 0}, {15, 67, 0}});

    client::ClientWorld clientWorld(1);
    std::uint64_t generation = 1;
    for (std::int32_t z = -1; z <= 1; ++z) {
        for (std::int32_t x = -1; x <= 1; ++x) {
            const std::unique_ptr<world::Chunk> chunk = world::generateFlatChunk(*preset, {x, z});
            clientWorld.apply({world::ChunkUpdate::Kind::Loaded, {x, z}, generation,
                               world::ChunkSnapshot::copyOf(*chunk, generation)});
            ++generation;
        }
    }

    entity::CollisionShapes shapes = entity::CollisionShapes::fromRegistry(*registry);
    const entity::Aabb half[] = {{{0.0, 0.0, 0.0}, {1.0, 0.5, 1.0}}};
    const entity::Aabb stair[] = {{{0.0, 0.0, 0.0}, {1.0, 0.5, 1.0}}, {{0.5, 0.5, 0.0}, {1.0, 1.0, 1.0}}};
    REQUIRE_FALSE(shapes.setShape(halfBlock, half));
    REQUIRE_FALSE(shapes.setShape(stairBlock, stair));
    const client::ClientCollisionView view(clientWorld);
    const entity::CollisionWorld collision{view, shapes};

    entity::PlayerMotion motion{.position = {0.5, 64.0, 0.5}};
    double onHalfBlock = 0.0;
    for (int i = 0; i < 62; ++i) {
        entity::stepPlayer(motion, {.forward = 1, .yaw = 90.0f}, test::standardTuning(), collision);
        if (motion.position.x > 3.3 && motion.position.x < 3.7) {
            onHalfBlock = std::max(onHalfBlock, motion.position.y);
        }
    }
    CHECK(onHalfBlock == Approx(64.5).margin(1e-9));
    CHECK(motion.position.y == Approx(68.0).margin(1e-9));
    CHECK(motion.position.x > 10.0);

    // A column whose snapshot has not arrived is a wall: chunk (2, 0) is missing, so the walk ends at x = 32.
    CHECK_FALSE(view.isLoaded({2, 0}));
    CHECK(view.isLoaded({1, 0}));
}

TEST_CASE("Prediction against the real server thread converges to the authoritative state", "[client][player]")
{
    const auto registry = test::makeTestRegistry();
    const auto tuning = std::make_shared<const data::PlayerMovementTuning>(test::standardTuning());
    core::JobSystem jobs(2);
    server::IntegratedServer server(server::ServerConfig{.jobs = &jobs,
                                                         .blocks = registry,
                                                         .flatPreset = test::makeStandardFlatPreset(*registry),
                                                         .playerMovement = tuning,
                                                         .loadRadius = 2});
    REQUIRE(server.start());

    const entity::CollisionShapes shapes = entity::CollisionShapes::fromRegistry(*registry);
    client::ClientWorld clientWorld(1);
    const client::ClientCollisionView view(clientWorld);
    const entity::CollisionWorld collision{view, shapes};
    client::LocalPlayer player(tuning);
    core::TickScheduler clock(core::kTickInterval, 3);
    std::optional<entity::PlayerState> newest;

    // A frame: world updates, the newest state, the due ticks. Walking east for the first 30 inputs.
    const auto frame = [&] {
        for (const world::ChunkUpdate& update : server.takeChunkUpdates()) {
            clientWorld.apply(update);
        }
        if (std::optional<entity::PlayerState> state = server.takePlayerState()) {
            const bool first = !player.spawned();
            player.receive(*state, collision);
            newest = state;
            if (first) {
                clock.reset(core::TickScheduler::Clock::now());
            }
        }
        if (!player.spawned()) {
            return;
        }
        const std::uint32_t ticks = clock.advance(core::TickScheduler::Clock::now()).ticksToRun;
        for (std::uint32_t i = 0; i < ticks; ++i) {
            const bool walking = player.stats().lastSent < 30;
            const entity::MovementIntent intent{.forward = static_cast<std::int8_t>(walking ? 1 : 0), .yaw = 90.0f};
            if (const std::optional<entity::PlayerInput> input = player.tick(intent, collision)) {
                server.sendPlayerInput(*input);
            }
        }
    };

    const auto deadline = std::chrono::steady_clock::now() + 20s;
    while (std::chrono::steady_clock::now() < deadline && player.stats().lastSent < 50) {
        frame();
        std::this_thread::sleep_for(4ms);
    }
    REQUIRE(player.stats().lastSent >= 50);
    // Stop making inputs and let the server settle all of them.
    while (std::chrono::steady_clock::now() < deadline &&
           (player.stats().lastInput < player.stats().lastSent || player.stats().history > 0)) {
        if (std::optional<entity::PlayerState> state = server.takePlayerState()) {
            player.receive(*state, collision);
            newest = state;
        }
        std::this_thread::sleep_for(4ms);
    }
    server.stop();

    REQUIRE(newest);
    const client::LocalPlayerStats stats = player.stats();
    CHECK(stats.lastInput == stats.lastSent);
    CHECK(stats.history == 0);
    CHECK(stats.resyncs == 0);
    // Everything settled: the client stands exactly where the server put it.
    CHECK(player.current() == newest->motion);
    CHECK(newest->motion.position.x > 0.5 + 4.0); // About 30 ticks of walking happened.
    CHECK(newest->motion.onGround);
    CHECK(server.stats().player.lastInput == stats.lastSent);
}
