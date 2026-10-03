#include "client/movement_sampler.h"

#include <catch2/catch_test_macros.hpp>

using aurora::client::MovementKeys;
using aurora::client::MovementSampler;
using aurora::entity::MovementIntent;

namespace {

constexpr MovementKeys kNoKeys{};

// A sampler that has been accepting input for a while.
MovementSampler acceptingSampler()
{
    MovementSampler sampler;
    sampler.endFrame(true, {});
    sampler.endFrame(true, {});
    return sampler;
}

} // namespace

TEST_CASE("Held keys become the intent only while input is accepted", "[client][input]")
{
    MovementSampler sampler;
    const MovementKeys keys{.forward = true, .right = true, .sneak = true, .sprint = true};
    CHECK(sampler.sample(keys, 10.0f, 5.0f) == MovementIntent{.yaw = 10.0f, .pitch = 5.0f}); // Not yet accepting.

    sampler.endFrame(true, {});
    const MovementIntent intent = sampler.sample(keys, 10.0f, 5.0f);
    CHECK(intent.forward == 1);
    CHECK(intent.strafe == 1);
    CHECK(intent.sneak);
    CHECK(intent.sprint);
    CHECK_FALSE(intent.jump);
    const MovementKeys opposite{.forward = true, .back = true, .left = true, .right = true};
    CHECK(sampler.sample(opposite, 0.0f, 0.0f) == MovementIntent{});

    sampler.endFrame(false, {});
    CHECK(sampler.sample(keys, 10.0f, 5.0f) == MovementIntent{.yaw = 10.0f, .pitch = 5.0f});
}

TEST_CASE("A jump tap between ticks reaches the next tick once", "[client][input]")
{
    MovementSampler sampler = acceptingSampler();
    sampler.endFrame(true, {.jump = true}); // Tapped during a frame with no tick.
    sampler.endFrame(true, {});
    CHECK(sampler.sample(kNoKeys, 0.0f, 0.0f).jump);
    CHECK_FALSE(sampler.sample(kNoKeys, 0.0f, 0.0f).jump); // Used once.
    // Held: every tick.
    const MovementKeys held{.jump = true};
    CHECK(sampler.sample(held, 0.0f, 0.0f).jump);
    CHECK(sampler.sample(held, 0.0f, 0.0f).jump);
}

TEST_CASE("Every way input stops drops a pending jump", "[client][input]")
{
    MovementSampler sampler = acceptingSampler();
    sampler.endFrame(true, {.jump = true}); // A tap waiting for the next tick.

    SECTION("block() (focus lost, mouse released, minimised, free flight switched on)")
    {
        sampler.block();
        sampler.endFrame(true, {}); // Even if the same frame ends accepting again.
    }
    SECTION("A frame that ends not accepting (UI keyboard, free flight)")
    {
        sampler.endFrame(false, {});
        sampler.endFrame(true, {});
    }
    CHECK_FALSE(sampler.sample(kNoKeys, 0.0f, 0.0f).jump);
}

TEST_CASE("A press in a frame that was blocked or did not start accepting is dropped", "[client][input]")
{
    SECTION("Space then Esc then a click in one poll: blocked in between")
    {
        MovementSampler sampler = acceptingSampler();
        sampler.block();
        sampler.endFrame(true, {.jump = true});
        CHECK_FALSE(sampler.sample(kNoKeys, 0.0f, 0.0f).jump);
    }
    SECTION("The frame that captures the mouse")
    {
        MovementSampler sampler;
        sampler.endFrame(false, {});
        sampler.endFrame(true, {.jump = true});
        CHECK_FALSE(sampler.sample(kNoKeys, 0.0f, 0.0f).jump);
    }
    SECTION("Control: the next frame's press counts")
    {
        MovementSampler sampler = acceptingSampler();
        sampler.block();
        sampler.endFrame(true, {});
        sampler.endFrame(true, {.jump = true});
        CHECK(sampler.sample(kNoKeys, 0.0f, 0.0f).jump);
    }
}
