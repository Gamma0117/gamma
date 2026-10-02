#include "client/local_player.h"
#include "core/constants.h"
#include "core/tick_scheduler.h"
#include "server/server_player.h"

#include "../entity/entity_test_support.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

using namespace std::chrono_literals;
using aurora::client::LocalPlayer;
using aurora::core::TickScheduler;
using aurora::entity::MovementIntent;
using aurora::entity::PlayerState;
using aurora::server::PlayerMessage;
using aurora::server::ServerPlayer;

namespace {

struct Outcome {
    std::uint64_t serverTicks = 0;
    std::uint64_t neutralTicks = 0; // Starving or filling.
    std::vector<std::uint32_t> applied;
    std::uint64_t neutralAfterFirstInput = 0; // Starving or filling once inputs flow: should never happen.
    std::uint64_t dropped = 0;
    std::uint64_t corrections = 0;
    std::uint32_t lastSent = 0;
};

// The real game loops as discrete events on one fake timeline: frames at `fps` drawing client ticks from a
// TickScheduler (the client's catch-up limit), server ticks on their own 20 Hz grid shifted by `serverPhase`, messages delivered the
// moment they are sent. At equal times `serverFirst` decides which runs first. The player holds W all along.
Outcome simulate(int fps, std::chrono::nanoseconds serverPhase, bool serverFirst)
{
    const aurora::test::TestBlocks blocks;
    const auto tuning = std::make_shared<const aurora::data::PlayerMovementTuning>(aurora::test::standardTuning());
    ServerPlayer server(tuning);
    LocalPlayer client(tuning);
    const MovementIntent walk{.forward = 1, .yaw = 90.0f};

    const TickScheduler::TimePoint start = TickScheduler::TimePoint{} + 1000s;
    const TickScheduler::TimePoint end = start + 3s;
    TickScheduler serverClock(50ms, aurora::core::kMaxCatchUpTicks);
    serverClock.reset(start + serverPhase);
    TickScheduler clientClock(50ms, aurora::core::kMaxClientCatchUpTicks);
    bool clientStarted = false;
    const std::chrono::nanoseconds frame(1'000'000'000 / fps);
    TickScheduler::TimePoint nextFrame = start;

    std::vector<PlayerMessage> toServer;
    std::optional<PlayerState> toClient;
    server.spawn({0.5, 64.0, 0.5});
    Outcome outcome;
    while (true) {
        const TickScheduler::TimePoint serverTime = serverClock.nextTickTime();
        const bool serverNext = serverTime < nextFrame || (serverTime == nextFrame && serverFirst);
        const TickScheduler::TimePoint now = serverNext ? serverTime : nextFrame;
        if (now > end) {
            break;
        }
        if (serverNext) {
            const std::uint32_t ticks = serverClock.advance(now).ticksToRun;
            for (std::uint32_t i = 0; i < ticks; ++i) {
                for (const PlayerMessage& message : toServer) {
                    server.receive(message);
                }
                toServer.clear();
                server.tick(blocks.world());
                ++outcome.serverTicks;
                if (const std::optional<std::uint32_t> input = server.lastTickInput()) {
                    outcome.applied.push_back(*input);
                } else {
                    ++outcome.neutralTicks;
                    outcome.neutralAfterFirstInput += outcome.applied.empty() ? 0 : 1;
                }
                toClient = server.state(outcome.serverTicks);
            }
            continue;
        }

        if (toClient) {
            client.receive(*toClient, blocks.world());
            toClient.reset();
            if (!clientStarted) {
                clientStarted = true;
                clientClock.reset(now);
            }
        }
        if (clientStarted) {
            const std::uint32_t ticks = clientClock.advance(now).ticksToRun;
            for (std::uint32_t i = 0; i < ticks; ++i) {
                if (const auto input = client.tick(walk, blocks.world())) {
                    toServer.push_back({.kind = PlayerMessage::Kind::Input, .input = *input});
                }
            }
        }
        nextFrame += frame;
    }
    outcome.dropped = server.stats().droppedInputs;
    outcome.corrections = client.stats().corrections;
    outcome.lastSent = client.stats().lastSent;
    return outcome;
}

} // namespace

TEST_CASE("At 30 60 and 144 fps the server steps once per tick and uses every input once", "[client][player][loop]")
{
    for (const int fps : {30, 60, 144}) {
        for (int phase = 0; phase < 50; ++phase) {
            for (const bool serverFirst : {true, false}) {
                INFO(fps << " fps, server phase " << phase << " ms, server first " << serverFirst);
                const Outcome outcome = simulate(fps, std::chrono::milliseconds(phase), serverFirst);
                // Physics time: one step per server tick, each either an input or a neutral tick.
                CHECK(outcome.applied.size() + outcome.neutralTicks == outcome.serverTicks);
                CHECK(outcome.serverTicks >= 59);
                // Every input exactly once, in order, none dropped.
                CHECK(outcome.dropped == 0);
                bool consecutive = !outcome.applied.empty();
                for (std::size_t i = 0; i < outcome.applied.size(); ++i) {
                    consecutive = consecutive && outcome.applied[i] == i + 1;
                }
                CHECK(consecutive);
                CHECK(outcome.applied.size() + 2 >= outcome.lastSent); // Only the newest may still be on the way.
                CHECK(outcome.neutralAfterFirstInput == 0);
                CHECK(outcome.corrections == 0);
            }
        }
    }
}
