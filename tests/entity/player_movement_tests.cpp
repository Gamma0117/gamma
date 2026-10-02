#include "core/constants.h"
#include "entity/player_movement.h"

#include "entity_test_support.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

using namespace aurora::entity;
using aurora::test::kHalf;
using aurora::test::kRise06;
using aurora::test::kRise06625;
using aurora::test::kSolid;
using aurora::test::kStair;
using aurora::test::run;
using aurora::test::standardTuning;
using aurora::test::standingAt;
using aurora::test::TestBlocks;
using aurora::test::walk;
using Catch::Approx;

namespace {

constexpr float kEast = 90.0f; // Yaw towards +x.

double horizontalSpeed(const PlayerMotion& motion)
{
    return std::hypot(motion.velocity.x, motion.velocity.z);
}

MovementIntent with(MovementIntent intent, bool jump, bool sneak, bool sprint)
{
    intent.jump = jump;
    intent.sneak = sneak;
    intent.sprint = sprint;
    return intent;
}

} // namespace

TEST_CASE("A falling player lands on the floor and stays on the ground", "[entity][movement]")
{
    const TestBlocks blocks;
    PlayerMotion motion{.position = {0.5, 65.0, 0.5}};
    run(motion, {}, 20, blocks.world());
    CHECK(motion.position.y == Approx(64.0).margin(1e-9));
    CHECK(motion.onGround);
    CHECK(motion.velocity.y == 0.0);
    const PlayerMotion landed = motion;
    run(motion, {}, 20, blocks.world()); // Standing still changes nothing.
    CHECK(motion == landed);
}

TEST_CASE("Walking sprinting and sneaking reach their speeds and diagonals are not faster", "[entity][movement]")
{
    const TestBlocks blocks(-1000);
    struct Case {
        MovementIntent intent;
        double speed;
    };
    const MovementIntent forward = walk(1, 0, 30.0f);
    const Case cases[] = {
        {forward, 4.3},
        {walk(1, 1, 30.0f), 4.3},                      // Diagonal.
        {with(forward, false, false, true), 5.6},      // Sprint.
        {with(forward, false, true, true), 1.3},       // Sneak wins over sprint.
        {with(walk(0, 1, 0.0f), false, false, true), 4.3},  // Sprint needs forward.
        {with(walk(-1, 0, 0.0f), false, false, true), 4.3}, // Not backwards either.
    };
    for (const Case& c : cases) {
        // An endless floor: below the world is solid.
        PlayerMotion motion = standingAt(0.5, aurora::core::kWorldMinY, 0.5);
        run(motion, c.intent, 40, blocks.world());
        INFO("forward " << int(c.intent.forward) << " strafe " << int(c.intent.strafe) << " sprint " << c.intent.sprint
                        << " sneak " << c.intent.sneak);
        CHECK(horizontalSpeed(motion) == Approx(c.speed).margin(1e-6));
        CHECK(motion.onGround);
    }
}

TEST_CASE("A jump rises about 1.26 blocks and climbs one block but not two", "[entity][movement]")
{
    SECTION("Height of a standing jump")
    {
        const TestBlocks blocks;
        PlayerMotion motion = standingAt(0.5, 64.0, 0.5);
        stepPlayer(motion, with({}, true, false, false), standardTuning(), blocks.world());
        double top = motion.position.y;
        run(motion, {}, 30, blocks.world(), standardTuning(), [&](const PlayerMotion& m) {
            top = std::max(top, m.position.y);
        });
        CHECK(top - 64.0 >= 1.2);
        CHECK(top - 64.0 <= 1.3);
        CHECK(motion.onGround);
    }
    SECTION("Onto a one-block platform")
    {
        TestBlocks blocks;
        for (int x = 1; x <= 10; ++x) {
            blocks.set(x, 64, 0, kSolid);
        }
        PlayerMotion motion = standingAt(0.5, 64.0, 0.5);
        run(motion, with(walk(1, 0, kEast), true, false, false), 12, blocks.world());
        run(motion, walk(1, 0, kEast), 10, blocks.world());
        CHECK(motion.position.y == Approx(65.0).margin(1e-9));
        CHECK(motion.position.x > 1.5);
        CHECK(motion.onGround);
    }
    SECTION("Not over a two-block wall")
    {
        TestBlocks blocks;
        blocks.set(1, 64, 0, kSolid);
        blocks.set(1, 65, 0, kSolid);
        PlayerMotion motion = standingAt(0.5, 64.0, 0.5);
        run(motion, with(walk(1, 0, kEast), true, false, false), 60, blocks.world());
        CHECK(motion.position.x <= 0.7 + 1e-9);
    }
}

TEST_CASE("Falling speed stops at the terminal velocity", "[entity][movement]")
{
    const TestBlocks blocks;
    PlayerMotion motion{.position = {0.5, 300.0, 0.5}};
    run(motion, {}, 60, blocks.world());
    CHECK(motion.velocity.y == -78.0);
    CHECK_FALSE(motion.onGround);
}

TEST_CASE("Fast movement never passes through thin floors ceilings or walls", "[entity][movement]")
{
    // Several starting points, so some tick starts just above the thin block and ends with the whole box past it:
    // only a sweep over the whole way finds it then.
    TestBlocks blocks(-1000);
    SECTION("A one-block floor at terminal speed")
    {
        blocks.set(0, 100, 0, kSolid); // Nothing below it.
        for (const double start : {200.0, 179.5, 150.3, 141.8, 120.0}) {
            INFO("start " << start);
            PlayerMotion motion{.position = {0.5, start, 0.5}, .velocity = {0.0, -78.0, 0.0}};
            for (int i = 0; i < 60 && !motion.onGround; ++i) {
                stepPlayer(motion, {}, standardTuning(), blocks.world());
            }
            CHECK(motion.onGround);
            CHECK(motion.position.y == Approx(101.0).margin(1e-9));
        }
    }
    SECTION("A one-block ceiling while rising fast")
    {
        blocks.set(0, 110, 0, kSolid);
        for (const double start : {100.0, 102.5, 104.0, 105.2}) {
            INFO("start " << start);
            PlayerMotion motion{.position = {0.5, start, 0.5}, .velocity = {0.0, 60.0, 0.0}};
            double top = motion.position.y;
            run(motion, {}, 6, blocks.world(), standardTuning(), [&](const PlayerMotion& m) {
                top = std::max(top, m.position.y);
            });
            CHECK(top <= 110.0 - 1.8 + 1e-9);
        }
    }
    SECTION("A one-block wall at nearly five blocks per tick")
    {
        for (int y = 95; y <= 102; ++y) {
            blocks.set(10, y, 0, kSolid);
        }
        for (const double start : {0.5, 3.0, 4.5, 7.0}) {
            INFO("start " << start);
            PlayerMotion motion{.position = {start, 100.0, 0.5}, .velocity = {100.0, 0.0, 0.0}};
            run(motion, {}, 3, blocks.world());
            CHECK(motion.position.x <= 9.7 + 1e-9);
            CHECK(motion.velocity.x == 0.0);
        }
    }
}

TEST_CASE("Walls stop the player and sliding along them never sticks", "[entity][movement]")
{
    TestBlocks blocks;
    for (int z = -5; z <= 45; ++z) {
        blocks.set(2, 64, z, kSolid);
        blocks.set(2, 65, z, kSolid);
    }
    PlayerMotion motion = standingAt(0.5, 64.0, 0.5);
    run(motion, walk(1, 0, kEast), 20, blocks.world());
    CHECK(motion.position.x == Approx(1.7).margin(1e-9));
    CHECK(motion.velocity.x == 0.0);

    // Diagonally into the wall: x stays, z keeps going every tick.
    double lastZ = motion.position.z;
    run(motion, walk(1, 0, 135.0f), 40, blocks.world(), standardTuning(), [&](const PlayerMotion& m) {
        CHECK(m.position.z > lastZ);
        CHECK(m.position.x <= 1.7 + 1e-9);
        lastZ = m.position.z;
    });
    CHECK(motion.position.z > 5.0);

    // Into a corner: stops on both axes.
    for (int x = -3; x <= 2; ++x) {
        blocks.set(x, 64, 42, kSolid);
        blocks.set(x, 65, 42, kSolid);
    }
    run(motion, walk(1, 0, 135.0f), 400, blocks.world());
    CHECK(motion.position.x == Approx(1.7).margin(1e-9));
    CHECK(motion.position.z == Approx(41.7).margin(1e-9));
}

TEST_CASE("Two blocks of headroom fit the 1.8 block player", "[entity][movement]")
{
    TestBlocks blocks;
    for (int x = 2; x <= 10; ++x) {
        blocks.set(x, 66, 0, kSolid); // Ceiling: free space y 64..66.
    }
    PlayerMotion motion = standingAt(0.5, 64.0, 0.5);
    run(motion, walk(1, 0, kEast), 40, blocks.world());
    CHECK(motion.position.x > 5.0);
    CHECK(motion.position.y == Approx(64.0).margin(1e-9));

    // A jump under the ceiling hits it and comes back down.
    double top = motion.position.y;
    run(motion, with({}, true, false, false), 1, blocks.world());
    run(motion, {}, 20, blocks.world(), standardTuning(), [&](const PlayerMotion& m) {
        top = std::max(top, m.position.y);
    });
    CHECK(top <= 66.0 - 1.8 + 1e-9);
    CHECK(motion.onGround);
}

TEST_CASE("Steps up to 0.6 blocks are climbed without jumping", "[entity][movement]")
{
    TestBlocks blocks;
    PlayerMotion motion = standingAt(0.5, 64.0, 0.5);
    motion.velocity.x = 4.3; // Already walking.

    SECTION("A half block: up in one tick")
    {
        blocks.set(1, 64, 0, kHalf);
        stepPlayer(motion, walk(1, 0, kEast), standardTuning(), blocks.world());
        CHECK(motion.position.y == Approx(64.5).margin(1e-9));
        CHECK(motion.onGround);
        CHECK(motion.velocity.y == 0.0);
        CHECK(motion.velocity.x > 0.0);
        CHECK(motion.position.x > 0.7);
    }
    SECTION("Exactly the step height")
    {
        blocks.set(1, 64, 0, kRise06);
        stepPlayer(motion, walk(1, 0, kEast), standardTuning(), blocks.world());
        CHECK(motion.position.y == Approx(64.6).margin(1e-9));
    }
    SECTION("A sixteenth more is a wall")
    {
        blocks.set(1, 64, 0, kRise06625);
        run(motion, walk(1, 0, kEast), 10, blocks.world());
        CHECK(motion.position.y == Approx(64.0).margin(1e-9));
        CHECK(motion.position.x <= 0.7 + 1e-9);
    }
    SECTION("A full block is a wall")
    {
        blocks.set(1, 64, 0, kSolid);
        run(motion, walk(1, 0, kEast), 10, blocks.world());
        CHECK(motion.position.x <= 0.7 + 1e-9);
    }
    SECTION("Not without the headroom")
    {
        blocks.set(1, 64, 0, kHalf);
        blocks.set(1, 66, 0, kSolid); // 1.5 blocks above the half block.
        run(motion, walk(1, 0, kEast), 10, blocks.world());
        CHECK(motion.position.y == Approx(64.0).margin(1e-9));
        CHECK(motion.position.x <= 0.7 + 1e-9);
    }
    SECTION("Not in the air")
    {
        blocks.set(1, 64, 0, kHalf);
        motion.position.y = 64.3;
        motion.onGround = false;
        stepPlayer(motion, walk(1, 0, kEast), standardTuning(), blocks.world());
        CHECK(motion.position.x <= 0.7 + 1e-9);
        CHECK(motion.position.y < 64.3);
    }
}

TEST_CASE("Ground and velocity come from the step path that was taken", "[entity][movement]")
{
    // The block under the player has just gone (from P0-7 on a player can stand where nothing is left): on the
    // ground at the start of the tick, but the normal path falls. The step onto the half block ahead lands, and
    // that decides: on the ground, no vertical speed, still walking.
    TestBlocks blocks;
    blocks.set(0, 63, 0, aurora::test::kAir);
    blocks.set(1, 64, 0, kHalf);
    PlayerMotion motion = standingAt(0.5, 64.0, 0.5);
    motion.velocity.x = 4.3;
    stepPlayer(motion, walk(1, 0, kEast), standardTuning(), blocks.world());
    CHECK(motion.position.y == Approx(64.5).margin(1e-9));
    CHECK(motion.onGround);
    CHECK(motion.velocity.y == 0.0);
    CHECK(motion.velocity.x == Approx(4.3));
}

TEST_CASE("A jump next to a half block is not turned into a step", "[entity][movement]")
{
    TestBlocks blocks;
    blocks.set(1, 64, 0, kHalf);
    PlayerMotion motion = standingAt(0.7, 64.0, 0.5); // Touching it.
    stepPlayer(motion, with(walk(1, 0, kEast), true, false, false), standardTuning(), blocks.world());
    double top = motion.position.y;
    run(motion, walk(1, 0, kEast), 20, blocks.world(), standardTuning(), [&](const PlayerMotion& m) {
        top = std::max(top, m.position.y);
    });
    CHECK(top >= 64.0 + 1.2);
    CHECK(motion.position.x > 1.0); // Landed on or beyond the half block.
}

TEST_CASE("Four stairs are climbed by walking", "[entity][movement]")
{
    TestBlocks blocks;
    for (int i = 0; i < 4; ++i) {
        blocks.set(1 + i, 64 + i, 0, kStair);
    }
    for (int x = 5; x <= 9; ++x) {
        for (int y = 64; y <= 67; ++y) {
            blocks.set(x, y, 0, kSolid); // Landing at the top: y 68.
        }
    }
    const auto climb = [&] {
        PlayerMotion motion = standingAt(0.5, 64.0, 0.5);
        run(motion, walk(1, 0, kEast), 30, blocks.world());
        return motion;
    };
    const PlayerMotion motion = climb();
    CHECK(motion.position.y == Approx(68.0).margin(1e-9));
    CHECK(motion.position.x > 6.0);
    CHECK(motion.onGround);
    // The same inputs on the same blocks give the same bits (same build and platform).
    CHECK(climb() == motion);
}

TEST_CASE("Unloaded columns are walls and a player across one freezes", "[entity][movement]")
{
    TestBlocks blocks;
    SECTION("East border of a loaded chunk")
    {
        blocks.unload({1, 0});
        PlayerMotion motion = standingAt(15.0, 64.0, 0.5);
        run(motion, walk(1, 0, kEast), 20, blocks.world());
        CHECK(motion.position.x == Approx(15.7).margin(1e-9));
        // Touching the border is not across it: walking back works.
        run(motion, walk(-1, 0, kEast), 10, blocks.world());
        CHECK(motion.position.x < 15.0);
    }
    SECTION("West border at a negative coordinate")
    {
        blocks.unload({-1, 0});
        PlayerMotion motion = standingAt(1.0, 64.0, 0.5);
        run(motion, walk(1, 0, -kEast), 20, blocks.world());
        CHECK(motion.position.x == Approx(0.3).margin(1e-9));
        run(motion, walk(1, 0, kEast), 10, blocks.world());
        CHECK(motion.position.x > 1.0);
    }
    SECTION("Across the border: nothing moves")
    {
        blocks.unload({1, 0});
        PlayerMotion motion{.position = {16.0, 66.0, 0.5}, .velocity = {3.0, -5.0, 0.0}};
        const StepResult result = stepPlayer(motion, walk(1, 0, kEast), standardTuning(), blocks.world());
        CHECK(result.frozen);
        CHECK(motion.position == glm::dvec3(16.0, 66.0, 0.5));
        CHECK(motion.velocity == glm::dvec3(0.0));
    }
}

TEST_CASE("The smallest player falls onto the floor across a chunk border", "[entity][movement]")
{
    const TestBlocks blocks;
    aurora::data::PlayerMovementTuning tuning = standardTuning();
    tuning.width = aurora::data::kMinPlayerSize;
    PlayerMotion motion{.position = {16.0, 70.0, 0.5}};
    run(motion, {}, 40, blocks.world(), tuning);
    CHECK(motion.onGround);
    CHECK(motion.position.y == Approx(64.0).margin(1e-9));
}

TEST_CASE("Below the world is solid and above it is empty", "[entity][movement]")
{
    const TestBlocks blocks(-1000); // No blocks inside the world.
    PlayerMotion motion{.position = {0.5, aurora::core::kWorldMinY + 3.0, 0.5}};
    run(motion, {}, 40, blocks.world());
    CHECK(motion.onGround);
    CHECK(motion.position.y == Approx(aurora::core::kWorldMinY).margin(1e-9));

    PlayerMotion high{.position = {0.5, aurora::core::kWorldMaxY - 0.5, 0.5}, .velocity = {0.0, 10.0, 0.0}};
    run(high, {}, 2, blocks.world());
    CHECK(high.position.y > aurora::core::kWorldMaxY - 0.5); // Nothing stops it above the top.
}

TEST_CASE("Intents are sanitised before use", "[entity][movement]")
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const MovementIntent moving{.forward = 1, .strafe = -1, .jump = true, .sneak = true, .sprint = true};

    for (const auto& [yaw, pitch] : {std::pair{nan, 0.0f}, std::pair{0.0f, nan}, std::pair{inf, 0.0f},
                                    std::pair{0.0f, -inf}}) {
        MovementIntent bad = moving;
        bad.yaw = yaw;
        bad.pitch = pitch;
        const MovementIntent result = sanitizeIntent(bad, 123.0f, -45.0f);
        CHECK(result == MovementIntent{.yaw = 123.0f, .pitch = -45.0f}); // Neutral, last valid angles.
    }
    // A last valid value that is not finite either falls back to zero.
    MovementIntent bad = moving;
    bad.yaw = nan;
    CHECK(sanitizeIntent(bad, inf, nan) == MovementIntent{});

    MovementIntent wild = moving;
    wild.forward = 100;
    wild.strafe = -7;
    wild.yaw = -30.0f;
    wild.pitch = 95.0f;
    const MovementIntent tamed = sanitizeIntent(wild, 0.0f, 0.0f);
    CHECK(tamed.forward == 1);
    CHECK(tamed.strafe == -1);
    CHECK(tamed.yaw == 330.0f);
    CHECK(tamed.pitch == 90.0f);
    CHECK(tamed.jump);
    CHECK(sanitizeIntent(tamed, 0.0f, 0.0f) == tamed); // Already valid: unchanged.
}

TEST_CASE("The motion carries the pose of the intent the step used", "[entity][movement]")
{
    TestBlocks blocks;
    PlayerMotion motion = standingAt(0.5, 64.0, 0.5);
    stepPlayer(motion, with(walk(1, 0, kEast), false, true, true), standardTuning(), blocks.world());
    CHECK(motion.sneaking);
    CHECK_FALSE(motion.sprinting); // Sneaking wins.
    stepPlayer(motion, with(walk(1, 0, kEast), false, false, true), standardTuning(), blocks.world());
    CHECK_FALSE(motion.sneaking);
    CHECK(motion.sprinting);
    stepPlayer(motion, MovementIntent{}, standardTuning(), blocks.world());
    CHECK_FALSE(motion.sneaking);
    CHECK_FALSE(motion.sprinting);

    // Frozen (rule B): nothing moves, but the pose is the intent's.
    blocks.unload({0, 0});
    const glm::dvec3 before = motion.position;
    CHECK(stepPlayer(motion, with(walk(1, 0, kEast), false, true, false), standardTuning(), blocks.world()).frozen);
    CHECK(motion.position == before);
    CHECK(motion.sneaking);
}
