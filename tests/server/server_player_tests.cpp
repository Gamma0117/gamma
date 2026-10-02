#include "server/server_player.h"

#include "../data/data_test_support.h"
#include "../entity/entity_test_support.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <vector>

using namespace aurora::server;
using aurora::entity::MovementIntent;
using aurora::entity::PlayerInput;
using aurora::entity::PlayerMotion;
using aurora::test::standardTuning;
using aurora::test::TestBlocks;
using Catch::Approx;

namespace {

constexpr MovementIntent kWalkEast{.forward = 1, .yaw = 90.0f};
constexpr MovementIntent kJump{.jump = true};
// Where Harness spawns the player: on the floor, not yet known to be on the ground (the first tick finds out).
const PlayerMotion kSpawned{.position = {0.5, 64.0, 0.5}};

PlayerMessage input(std::uint32_t sequence, MovementIntent intent = {})
{
    return {.kind = PlayerMessage::Kind::Input, .input = PlayerInput{sequence, intent}};
}

PlayerMessage neutralize(std::uint32_t through)
{
    return {.kind = PlayerMessage::Kind::Neutralize, .through = through};
}

// A spawned player on the test floor, with a record of every tick: which input it used and the settled run
// published after it. check() verifies the contract of lastInput() over the whole record.
struct Harness {
    TestBlocks blocks;
    ServerPlayer player{std::make_shared<const aurora::data::PlayerMovementTuning>(standardTuning())};
    std::vector<std::uint32_t> published;                  // lastInput() after each tick.
    std::vector<std::optional<std::uint32_t>> applied;     // lastTickInput() of each tick.

    Harness() { player.spawn({0.5, 64.0, 0.5}); }

    void send(const PlayerMessage& message) { player.receive(message); }
    void tick(int count = 1)
    {
        for (int i = 0; i < count; ++i) {
            player.tick(blocks.world());
            published.push_back(player.lastInput());
            applied.push_back(player.lastTickInput());
        }
    }

    // lastInput() never goes down; no input at or below a published value is applied later; every input is
    // applied at most once.
    void check() const
    {
        std::uint32_t settled = 0;
        std::map<std::uint32_t, int> uses;
        for (std::size_t i = 0; i < published.size(); ++i) {
            if (applied[i]) {
                INFO("tick " << i << " applied " << *applied[i] << " after settled " << settled);
                CHECK(*applied[i] > settled);
                ++uses[*applied[i]];
            }
            CHECK(published[i] >= settled);
            settled = published[i];
        }
        for (const auto& [sequence, count] : uses) {
            INFO("sequence " << sequence);
            CHECK(count == 1);
        }
    }

    std::vector<std::uint32_t> appliedSequences() const
    {
        std::vector<std::uint32_t> result;
        for (const auto& sequence : applied) {
            if (sequence) {
                result.push_back(*sequence);
            }
        }
        return result;
    }
};

// The same ticks through stepPlayer directly.
PlayerMotion expected(PlayerMotion motion, const std::vector<MovementIntent>& intents)
{
    const TestBlocks blocks;
    for (const MovementIntent& intent : intents) {
        aurora::entity::stepPlayer(motion, intent, standardTuning(), blocks.world());
    }
    return motion;
}

} // namespace

TEST_CASE("The settled run never skips an input that is still waiting", "[server][player]")
{
    Harness h;
    SECTION("1 2 4 3: 3 is late")
    {
        for (const std::uint32_t sequence : {1u, 2u, 4u, 3u}) {
            h.send(input(sequence));
        }
        h.tick(3);
        CHECK(h.published == std::vector<std::uint32_t>{1, 3, 4});
        CHECK(h.appliedSequences() == std::vector<std::uint32_t>{1, 2, 4});
        CHECK(h.player.stats().staleInputs == 1);
    }
    SECTION("1 2 2: the copy does not settle the original")
    {
        for (const std::uint32_t sequence : {1u, 2u, 2u}) {
            h.send(input(sequence));
        }
        h.tick(2);
        CHECK(h.published == std::vector<std::uint32_t>{1, 2});
        CHECK(h.appliedSequences() == std::vector<std::uint32_t>{1, 2});
    }
    SECTION("1..40 at once and a copy of 40")
    {
        for (std::uint32_t sequence = 1; sequence <= 40; ++sequence) {
            h.send(input(sequence));
        }
        h.send(input(40));
        h.tick(5);
        CHECK(h.published == std::vector<std::uint32_t>{37, 38, 39, 40, 40});
        CHECK(h.appliedSequences() == std::vector<std::uint32_t>{37, 38, 39, 40});
        CHECK(h.player.stats().droppedInputs == 36);
        CHECK(h.player.stats().staleInputs == 1);
    }
    h.check();
}

TEST_CASE("Messages before the spawn change nothing", "[server][player]")
{
    ServerPlayer player(std::make_shared<const aurora::data::PlayerMovementTuning>(standardTuning()));
    const TestBlocks blocks;
    for (std::uint32_t sequence = 1; sequence <= 5; ++sequence) {
        player.receive(input(sequence, kWalkEast));
    }
    player.receive(neutralize(5));
    player.tick(blocks.world()); // Not spawned: nothing.
    CHECK(player.lastInput() == 0);

    player.spawn({0.5, 64.0, 0.5});
    player.receive(input(1, kWalkEast));
    player.receive(input(2, kWalkEast));
    player.tick(blocks.world());
    CHECK(player.lastTickInput() == 1u);
    CHECK(player.lastTickIntent() == kWalkEast); // Not neutralised by the early Neutralize(5).
    CHECK(player.lastInput() == 1);
}

TEST_CASE("Physics time is one step per tick however many inputs arrive", "[server][player]")
{
    Harness h;
    for (std::uint32_t sequence = 1; sequence <= 40; ++sequence) {
        h.send(input(sequence, kWalkEast));
    }
    h.tick();
    // Exactly one step of walking, not forty.
    CHECK(h.player.motion() == expected(kSpawned, {kWalkEast}));
}

TEST_CASE("Without input the player falls and lands and nothing is repeated", "[server][player]")
{
    SECTION("Gravity goes on without input")
    {
        TestBlocks blocks;
        ServerPlayer player(std::make_shared<const aurora::data::PlayerMovementTuning>(standardTuning()));
        player.spawn({0.5, 70.0, 0.5});
        for (int i = 0; i < 30; ++i) {
            player.tick(blocks.world());
        }
        CHECK(player.motion().onGround);
        CHECK(player.motion().position.y == Approx(64.0).margin(1e-9));
        CHECK(player.stats().starvedTicks == 30);
    }
    SECTION("A single W: two filling ticks, the step, then neutral ticks")
    {
        Harness h;
        h.send(input(1, kWalkEast));
        h.tick(10);
        CHECK(h.published[0] == 0);
        CHECK(h.published[2] == 1); // Settled by the third tick.
        const MovementIntent neutral = kWalkEast.neutral();
        std::vector<MovementIntent> intents{{}, {}, kWalkEast};
        intents.resize(10, neutral);
        CHECK(h.player.motion() == expected(kSpawned, intents));
        CHECK(h.player.stats().primingTicks == 2);
        h.check();
    }
    SECTION("A single jump is used once")
    {
        Harness h;
        h.send(input(1, kJump));
        h.tick(40);
        std::vector<MovementIntent> intents{{}, {}, kJump};
        intents.resize(40, MovementIntent{});
        CHECK(h.player.motion() == expected(kSpawned, intents));
        CHECK(h.player.motion().onGround); // One jump, landed, no second one.
    }
    SECTION("The last neutral input alone is settled too")
    {
        Harness h;
        h.send(input(1));
        h.tick(3);
        CHECK(h.player.lastInput() == 1);
        CHECK(h.applied[2] == 1u);
    }
}

TEST_CASE("Neutralize makes waiting and later inputs in its range neutral", "[server][player]")
{
    SECTION("A waiting input becomes neutral and is still settled")
    {
        Harness h;
        h.send(input(1, kWalkEast));
        h.send(neutralize(1));
        h.tick(3);
        CHECK(h.applied[2] == 1u);
        CHECK(h.player.lastTickIntent() == kWalkEast.neutral());
        CHECK(h.player.lastInput() == 1);
        CHECK(h.player.motion().position.x == 0.5);
    }
    SECTION("A smaller range never shrinks it, and later inputs inside it are neutral")
    {
        Harness h;
        h.send(neutralize(10));
        h.send(neutralize(3));
        h.send(input(10, kJump));
        h.tick(3);
        CHECK(h.applied[2] == 10u);
        CHECK(h.player.lastTickIntent() == MovementIntent{});
        CHECK(h.player.motion().position.y == 64.0); // No jump.
        h.send(input(11, kJump));
        h.send(input(12));
        h.tick();
        CHECK(h.applied.back() == 11u);
        CHECK(h.player.lastTickIntent() == kJump); // Above the range: as sent.
    }
    SECTION("Inputs outside the range keep their intent")
    {
        Harness h;
        h.send(input(1, kWalkEast));
        h.send(input(2, kWalkEast));
        h.send(neutralize(1));
        h.tick(2);
        CHECK(h.applied[0] == 1u);
        CHECK(h.applied[1] == 2u);
        CHECK(h.player.lastTickIntent() == kWalkEast);
    }
}

TEST_CASE("A look that is not a finite number becomes the last valid look without movement", "[server][player]")
{
    const aurora::test::QuietLog quiet; // Warned once on purpose.
    Harness h;
    h.send(input(1, MovementIntent{.forward = 1, .yaw = 45.0f, .pitch = 10.0f}));
    h.send(input(2, MovementIntent{.forward = 1, .jump = true, .yaw = std::numeric_limits<float>::quiet_NaN()}));
    h.send(input(3, MovementIntent{.forward = 1, .pitch = std::numeric_limits<float>::infinity()}));
    h.tick(3);
    CHECK(h.applied[1] == 2u);
    CHECK(h.applied[2] == 3u);
    CHECK(h.player.lastTickIntent() == MovementIntent{.yaw = 45.0f, .pitch = 10.0f});
}

TEST_CASE("A frozen player settles inputs without moving", "[server][player]")
{
    Harness h;
    h.blocks.unload({0, 0});
    h.send(input(1, kWalkEast));
    h.send(input(2, kWalkEast));
    h.tick(2);
    CHECK(h.player.stats().frozen);
    CHECK(h.player.lastInput() == 2);
    CHECK(h.player.motion().position == glm::dvec3(0.5, 64.0, 0.5));
}

TEST_CASE("A steady stream keeps one input in reserve", "[server][player]")
{
    Harness h;
    std::uint32_t sequence = 0;
    // One input per tick, but every other one arrives a tick late (0, 2, 0, 2, ...): after filling, never starved.
    h.send(input(++sequence, kWalkEast));
    h.tick(); // Filling.
    for (int i = 0; i < 40; ++i) {
        if (i % 2 == 0) {
            h.send(input(++sequence, kWalkEast));
            h.send(input(++sequence, kWalkEast));
        }
        h.tick();
    }
    CHECK(h.player.stats().starvedTicks == 0);
    CHECK(h.player.stats().droppedInputs == 0);
    h.check();
}

TEST_CASE("Random arrivals keep the settled run contract", "[server][player]")
{
    std::mt19937 random(20261001);
    Harness h;
    std::uint32_t next = 1;
    for (int step = 0; step < 20000; ++step) {
        const auto roll = random() % 100;
        if (roll < 40) {
            h.send(input(next++, kWalkEast));
        } else if (roll < 50 && next > 3) {
            h.send(input(next - 1 - random() % 3)); // Duplicate or late.
        } else if (roll < 55) {
            next += 1 + random() % 3; // A gap: these never arrive (or arrive late below).
        } else if (roll < 60) {
            h.send(neutralize(next - 1 - (next > 5 ? random() % 5 : 0)));
        } else {
            h.tick();
        }
    }
    h.check();
}
