// Block actions from the client's input to the server and back to the client's world, prediction and meshes.

#include "client/client_collision_view.h"
#include "client/client_world.h"
#include "client/local_player.h"
#include "client/mesh_scheduler.h"
#include "client/player_control.h"
#include "core/job_system.h"
#include "core/log.h"
#include "render/chunk_mesher.h"
#include "render/mesh_resources.h"
#include "render/upload_queue.h"
#include "server/integrated_server.h"

#include "../render/gpu_table_support.h"
#include "../server/interaction_test_support.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

using namespace aurora;
using namespace std::chrono_literals;
using entity::MovementIntent;
using test::InteractionRig;

namespace {

constexpr MovementIntent kDigDown{.pitch = -90.0f, .attack = true};
constexpr MovementIntent kStand{.pitch = -90.0f};

// The client's side of the lockstep: its world copy and prediction, fed from an InteractionRig.
struct ClientSide {
    entity::CollisionShapes shapes;
    client::ClientWorld world{1};
    client::ClientCollisionView view{world};
    entity::CollisionWorld collision{view, shapes};
    client::LocalPlayer player;

    explicit ClientSide(const InteractionRig& rig)
        : shapes(*rig.shapes)
        , player(rig.tuning)
    {
    }

    // One server frame: its chunk updates in order, then its state (as the app does).
    void receive(const std::vector<world::ChunkUpdate>& updates, const entity::PlayerState& state)
    {
        for (const world::ChunkUpdate& update : updates) {
            world.apply(update);
        }
        player.receive(state, collision);
    }
};

// Client tick -> input to the server -> server tick -> the frame back to the client, every tick. `intents` are the
// client's, one per tick; afterwards no more inputs until the server applied them all.
struct LockstepResult {
    std::uint64_t corrections = 0;
    std::uint64_t edits = 0;
    bool converged = false;
};

LockstepResult runLockstep(InteractionRig& rig, ClientSide& client, const std::vector<MovementIntent>& intents)
{
    rig.tick(); // The spawn tick: the loads and the first state.
    client.receive(std::exchange(rig.updates, {}), rig.state());
    for (const MovementIntent& intent : intents) {
        if (const std::optional<entity::PlayerInput> input = client.player.tick(intent, client.collision)) {
            rig.player.receive({.kind = server::PlayerMessage::Kind::Input, .input = *input});
        }
        rig.tick();
        client.receive(std::exchange(rig.updates, {}), rig.state());
    }
    const std::uint64_t corrections = client.player.stats().corrections;
    for (int i = 0; i < 5; ++i) { // Settle: the server applies what is left, the client sees it.
        rig.tick();
        client.receive(std::exchange(rig.updates, {}), rig.state());
    }
    const client::LocalPlayerStats stats = client.player.stats();
    return {.corrections = corrections,
            .edits = rig.blocks.stats().blocksBroken,
            .converged = stats.lastInput == stats.lastSent && stats.history == 0 &&
                         test::sameBits(client.player.current(), rig.player.motion())};
}

std::vector<MovementIntent> digThenStand(int dig, int stand)
{
    std::vector<MovementIntent> intents(static_cast<std::size_t>(dig), kDigDown);
    intents.insert(intents.end(), static_cast<std::size_t>(stand), kStand);
    return intents;
}

} // namespace

TEST_CASE("The first tick after the floor is mined away uses the old ground", "[client][interaction][floor]")
{
    for (const bool jump : {false, true}) {
        INFO("jump " << jump);
        InteractionRig rig;
        rig.start({.pitch = -90.0f});
        for (int i = 0; i < 6; ++i) { // Grass: 6 ticks. The movement of the breaking tick comes before the break.
            rig.step(kDigDown);
        }
        REQUIRE(rig.blockAt({0, 63, 0}) == data::kAirState);
        REQUIRE(rig.player.motion().onGround);
        REQUIRE(rig.player.motion().position.y == 64.0);
        rig.step({.jump = jump, .pitch = -90.0f});
        if (jump) {
            CHECK(rig.player.motion().velocity.y > 0.0); // Still on the old ground at the start of the tick.
            CHECK(rig.player.motion().position.y > 64.0);
        } else {
            CHECK(rig.player.motion().position.y < 64.0); // Falls into the hole at once.
            CHECK_FALSE(rig.player.motion().onGround);
        }
    }
}

TEST_CASE("In lockstep one edit under the feet corrects at most once and converges", "[client][interaction][floor]")
{
    InteractionRig rig;
    ClientSide client(rig);
    // Six attack inputs break the grass under the feet; then standing, the player falls one block.
    const LockstepResult result = runLockstep(rig, client, digThenStand(6, 30));
    CHECK(result.edits == 1);
    CHECK(result.corrections <= 1);
    CHECK(result.converged);
    CHECK(client.player.current().position.y == 63.0); // On the dirt, one below.
    CHECK(client.world.snapshot({0, 0})->getBlock(0, 63, 0) == data::kAirState);

    // Control: the same inputs on unbreakable ground never correct.
    InteractionRig hard;
    REQUIRE(hard.world.setBlock({0, 63, 0}, hard.state("aurora:bedrock")));
    ClientSide other(hard);
    const LockstepResult control = runLockstep(hard, other, digThenStand(6, 30));
    CHECK(control.edits == 0);
    CHECK(control.corrections == 0);
    CHECK(control.converged);
}

TEST_CASE("With late states and inputs an edit adds corrections but everything converges",
          "[client][interaction][floor]")
{
    // Frames from the server reach the client 0..2 ticks late and inputs reach the server 0..1 tick late, by a
    // fixed pattern; the client keeps predicting. Corrections come from the edit, but also from the neutral ticks
    // of late inputs, so the edit run is compared with the same timeline on unbreakable ground, and no bound per
    // edit is claimed. Afterwards, with nothing late any more, the counts stop growing.
    const auto run = [](bool breakable) {
        InteractionRig rig;
        if (!breakable) {
            REQUIRE(rig.world.setBlock({0, 63, 0}, rig.state("aurora:bedrock")));
        }
        ClientSide client(rig);
        struct Frame {
            std::vector<world::ChunkUpdate> updates;
            entity::PlayerState state;
        };
        std::deque<std::pair<int, Frame>> toClient;                 // (tick it arrives, frame)
        std::deque<std::pair<int, entity::PlayerInput>> toServer;    // (tick it arrives, input)
        rig.tick();
        client.receive(std::exchange(rig.updates, {}), rig.state());
        const std::vector<MovementIntent> intents = digThenStand(20, 40);
        constexpr int kStateLag[] = {0, 2, 1, 0, 0, 2, 2, 1, 0, 1};
        constexpr int kInputLag[] = {0, 1, 0, 0, 1, 1, 0, 0, 0, 1, 0};
        for (int tick = 0; tick < static_cast<int>(intents.size()) + 10; ++tick) {
            if (tick < static_cast<int>(intents.size())) {
                if (auto input = client.player.tick(intents[static_cast<std::size_t>(tick)], client.collision)) {
                    toServer.emplace_back(tick + kInputLag[tick % 11], *input);
                }
            }
            while (!toServer.empty() && toServer.front().first <= tick) {
                rig.player.receive({.kind = server::PlayerMessage::Kind::Input, .input = toServer.front().second});
                toServer.pop_front();
            }
            rig.tick();
            toClient.emplace_back(tick + kStateLag[tick % 10], Frame{std::exchange(rig.updates, {}), rig.state()});
            std::optional<entity::PlayerState> newest;
            while (!toClient.empty() && toClient.front().first <= tick) {
                for (const world::ChunkUpdate& update : toClient.front().second.updates) {
                    client.world.apply(update);
                }
                newest = toClient.front().second.state;
                toClient.pop_front();
            }
            if (newest) {
                client.player.receive(*newest, client.collision);
            }
        }
        // Nothing late any more: everything arrives, the server settles.
        for (; !toClient.empty(); toClient.pop_front()) {
            client.receive(toClient.front().second.updates, toClient.front().second.state);
        }
        for (; !toServer.empty(); toServer.pop_front()) {
            rig.player.receive({.kind = server::PlayerMessage::Kind::Input, .input = toServer.front().second});
        }
        for (int i = 0; i < 5; ++i) {
            rig.tick();
            client.receive(std::exchange(rig.updates, {}), rig.state());
        }
        const std::uint64_t settled = client.player.stats().corrections;
        for (int i = 0; i < 20; ++i) { // Standing still in lockstep: nothing new to correct.
            if (auto input = client.player.tick(kStand, client.collision)) {
                rig.player.receive({.kind = server::PlayerMessage::Kind::Input, .input = *input});
            }
            rig.tick();
            client.receive(std::exchange(rig.updates, {}), rig.state());
        }
        const client::LocalPlayerStats stats = client.player.stats();
        CHECK(stats.corrections == settled);
        CHECK(test::sameBits(client.player.current(), rig.player.motion()));
        core::logInfo("test", "Late frames, {} ground: {} edits, {} corrections, {} neutral ticks on the server",
                      breakable ? "breakable" : "unbreakable", rig.blocks.stats().blocksBroken, settled,
                      rig.player.stats().starvedTicks + rig.player.stats().primingTicks);
        return std::pair{rig.blocks.stats().blocksBroken, settled};
    };
    const auto [edits, corrections] = run(true);
    const auto [noEdits, baseline] = run(false);
    CHECK(edits >= 1);
    CHECK(noEdits == 0);
    CHECK(corrections >= baseline); // The edit only adds; how many is recorded, not bounded.
}

namespace {

std::shared_ptr<const data::PlayerMovementTuning> standardMovement()
{
    return std::make_shared<const data::PlayerMovementTuning>(test::standardTuning());
}

} // namespace

TEST_CASE("A frame always holds every change up to its player state", "[client][interaction][frame]")
{
    const auto registry = test::makeInteractionRegistry();
    core::JobSystem jobs(2);
    server::IntegratedServer server(server::ServerConfig{.jobs = &jobs,
                                                         .blocks = registry,
                                                         .flatPreset = test::makeStandardFlatPreset(*registry),
                                                         .playerMovement = standardMovement(),
                                                         .playerInteraction = test::loadShippedInteraction(*registry),
                                                         .loadRadius = 1});
    REQUIRE(server.start());

    std::uint64_t newestState = 0; // The newest state tick of the frames taken before the current one.
    std::uint64_t lastEventTick = 0;
    std::size_t changes = 0;
    std::size_t events = 0;
    const auto take = [&] {
        const server::ServerFrame frame = server.takeFrame();
        for (const world::ChunkUpdate& update : frame.chunkUpdates) {
            CHECK(update.serverTick > newestState); // Never a change of a tick whose state was already seen.
            changes += update.kind == world::ChunkUpdate::Kind::Changed ? 1 : 0;
        }
        for (const entity::BlockBrokenEvent& event : frame.broken) {
            CHECK(event.serverTick > newestState);
            CHECK(event.serverTick >= lastEventTick); // In order.
            lastEventTick = event.serverTick;
            ++events;
        }
        if (frame.playerState) {
            for (const world::ChunkUpdate& update : frame.chunkUpdates) {
                CHECK(update.serverTick <= frame.playerState->serverTick);
            }
            newestState = frame.playerState->serverTick;
        }
        return frame;
    };

    const auto deadline = std::chrono::steady_clock::now() + 20s;
    while (newestState == 0 && std::chrono::steady_clock::now() < deadline) {
        take();
        std::this_thread::sleep_for(2ms);
    }
    REQUIRE(newestState > 0);
    // Dig down for 3 s, taking frames at irregular times, with one long gap in which frames pile up.
    std::uint32_t sequence = 0;
    const auto digStart = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - digStart < 3s) {
        server.sendPlayerInput({++sequence, kDigDown});
        std::this_thread::sleep_for(50ms);
        if (sequence % 3 != 0) {
            take();
        }
        if (sequence == 20) {
            const std::size_t eventsBefore = events;
            for (int i = 0; i < 12; ++i) { // 600 ms without taking a frame.
                server.sendPlayerInput({++sequence, kDigDown});
                std::this_thread::sleep_for(50ms);
            }
            take();
            CHECK(events > eventsBefore); // The events of the gap all arrive, in order.
        }
    }
    take();
    server.stop();
    CHECK(events >= 3);
    CHECK(changes >= events);
}

TEST_CASE("Mining and placing through the real server thread reach the client's meshes",
          "[client][interaction][integration]")
{
    const auto registry = test::makeInteractionRegistry();
    const auto interaction = test::loadShippedInteraction(*registry);
    const auto movement = standardMovement();
    core::JobSystem jobs(2);
    server::IntegratedServer server(server::ServerConfig{.jobs = &jobs,
                                                         .blocks = registry,
                                                         .flatPreset = test::makeStandardFlatPreset(*registry),
                                                         .playerMovement = movement,
                                                         .playerInteraction = interaction,
                                                         .loadRadius = 2});
    REQUIRE(server.start());

    struct Mailbox final : client::PlayerMessageSink {
        server::IntegratedServer& target;
        explicit Mailbox(server::IntegratedServer& server)
            : target(server)
        {
        }
        void sendInput(const entity::PlayerInput& input) override { target.sendPlayerInput(input); }
        void sendNeutralize(std::uint32_t through) override { target.neutralizePlayerInputs(through); }
    } mailbox(server);

    const entity::CollisionShapes shapes = entity::CollisionShapes::fromRegistry(*registry);
    client::ClientWorld clientWorld(1);
    const client::ClientCollisionView view(clientWorld);
    const entity::CollisionWorld collision{view, shapes};
    client::PlayerControl control(movement, collision, mailbox, interaction->tuning.palette.size());
    const auto resources = render::buildMeshResources(*registry, *render::assignTextureLayers({}));
    client::MeshScheduler scheduler(jobs, render::makeChunkMesher(resources), 16);
    render::UploadQueue queue;
    test::GpuTable gpu;
    std::vector<entity::BlockBrokenEvent> broken;

    // The app's frame without a window: the server's frame, the player, the meshes.
    client::PlayerFrameInput events{.focused = true};
    const auto frame = [&] {
        control.input(events);
        events.clickPressed = false;
        events.usePressed = false;
        server::ServerFrame serverFrame = server.takeFrame();
        for (const world::ChunkUpdate& update : serverFrame.chunkUpdates) {
            clientWorld.apply(update);
        }
        control.update(serverFrame.playerState, std::chrono::steady_clock::now(), {}, 0.0f, -55.0f);
        broken.insert(broken.end(), serverFrame.broken.begin(), serverFrame.broken.end());
        scheduler.update(clientWorld, {0, 0}, 7);
        queue.push(scheduler.takeReady());
        gpu.update(clientWorld, queue);
        std::this_thread::sleep_for(5ms);
    };
    const auto runUntil = [&](const auto& done) {
        const auto deadline = std::chrono::steady_clock::now() + 20s;
        while (!done() && std::chrono::steady_clock::now() < deadline) {
            frame();
        }
        return done();
    };

    REQUIRE(runUntil([&] { return control.localPlayer().spawned() && clientWorld.isAreaComplete(); }));
    events.clickPressed = true; // Captures the mouse.
    frame();
    frame();
    REQUIRE(control.accepting());

    // Down and north: the top of the grass at (0, 63, -1). Hold the button until it breaks.
    events.clickPressed = true;
    events.attackDown = true;
    REQUIRE(runUntil([&] { return !broken.empty(); }));
    events.attackDown = false;
    frame();
    CHECK(broken.front().position == world::BlockPos{0, 63, -1});
    CHECK(broken.front().previousState == test::stateOf(*registry, "aurora:grass_block"));
    REQUIRE(runUntil([&] { return clientWorld.snapshot({0, -1})->getBlock(0, 63, 15) == data::kAirState; }));

    // One right click places dirt (slot 1) back where the grass was.
    events.slotPressed = 1;
    frame();
    events.slotPressed.reset();
    events.usePressed = true;
    const data::BlockStateId dirt = test::stateOf(*registry, "aurora:dirt");
    REQUIRE(runUntil([&] { return clientWorld.snapshot({0, -1})->getBlock(0, 63, 15) == dirt; }));
    CHECK(server.stats().interaction.blocksPlaced == 1);
    CHECK(server.stats().interaction.blocksBroken == 1);

    // The meshes settle and equal meshing the client's world from scratch.
    REQUIRE(runUntil([&] { return scheduler.isSettled() && queue.size() == 0; }));
    server.stop();
    test::checkMatchesFullMesh(clientWorld, gpu, *resources);
    CHECK(gpu.uploads > 0);
}
