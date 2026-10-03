// The server's block actions with a real World, ServerPlayer and BlockInteraction, ticked in IntegratedServer's
// order (see InteractionRig).

#include "interaction_test_support.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>
#include <vector>

using namespace aurora;
using entity::MovementIntent;
using entity::PlaceResult;
using test::attackAt;
using test::InteractionRig;
using test::kDown;
using test::useAt;
using world::BlockPos;

namespace {

// From the default spawn (0.5, 64, 0.5), eyes at 65.62, looking north: the south face of (0, 65, -2) is 1.5 away.
constexpr BlockPos kFront{0, 65, -2};
constexpr float kNorth = 0.0f;
const MovementIntent kMineFront = attackAt(kNorth, 0.0f);

std::uint32_t progress(const InteractionRig& rig)
{
    return rig.blocks.digState().progress;
}

// Holds attack on what the look hits until a block breaks (at most `limit` ticks); returns the ticks it took.
std::uint32_t ticksToBreak(InteractionRig& rig, const MovementIntent& intent, std::uint32_t limit = 100)
{
    const std::size_t brokenBefore = rig.broken.size();
    for (std::uint32_t tick = 1; tick <= limit; ++tick) {
        rig.step(intent);
        if (rig.broken.size() > brokenBefore) {
            return tick;
        }
    }
    return 0;
}

} // namespace

TEST_CASE("Mining takes the ticks of the block's hardness and breaks it once", "[server][interaction]")
{
    InteractionRig rig;
    rig.start();
    struct Case {
        const char* state;
        std::uint32_t ticks;
    };
    for (const Case& c : {Case{"aurora:stone", 15}, Case{"aurora:dirt", 5}, Case{"aurora:grass_block", 6},
                          Case{"aurora:cobblestone", 20}, Case{"aurora:oak_log[axis=x]", 20},
                          Case{"aurora:oak_planks", 20}, Case{"aurora:soft", 1}}) {
        INFO(c.state);
        const data::BlockStateId state = rig.state(c.state);
        REQUIRE(rig.world.setBlock(kFront, state));
        const std::size_t brokenBefore = rig.broken.size();
        for (std::uint32_t tick = 1; tick < c.ticks; ++tick) {
            rig.step(kMineFront);
            REQUIRE(rig.blocks.digState().target == kFront);
            CHECK(progress(rig) == tick);
            CHECK(rig.blocks.digState().required == c.ticks);
            const entity::PlayerState published = rig.state();
            CHECK(published.digTarget == kFront);
            CHECK(published.digProgress == tick);
            CHECK(published.digRequired == c.ticks);
            CHECK(published.digGeneration == *rig.world.generation({0, -1}));
            CHECK(rig.blockAt(kFront) == state);
        }
        rig.step(kMineFront);
        CHECK(rig.blockAt(kFront) == data::kAirState);
        REQUIRE(rig.broken.size() == brokenBefore + 1);
        const entity::BlockBrokenEvent& event = rig.broken.back();
        CHECK(event.position == kFront);
        CHECK(event.previousState == state);
        CHECK(event.generation == *rig.world.generation({0, -1}));
        CHECK(event.serverTick == rig.tickNumber);
        CHECK(event.occurredAt == rig.now);
        CHECK_FALSE(rig.blocks.digState().target);
        CHECK_FALSE(rig.state().digTarget);
        // Nothing left in reach: holding on does nothing more.
        rig.step(kMineFront);
        CHECK(rig.broken.size() == brokenBefore + 1);
        CHECK(progress(rig) == 0);
    }
    CHECK(rig.blocks.stats().blocksBroken == 7);
}

TEST_CASE("Holding on mines the next block from the start", "[server][interaction]")
{
    InteractionRig rig;
    rig.start();
    REQUIRE(rig.world.setBlock(kFront, rig.state("aurora:dirt")));
    REQUIRE(rig.world.setBlock({0, 65, -3}, rig.state("aurora:stone")));
    CHECK(ticksToBreak(rig, kMineFront) == 5);
    rig.step(kMineFront);
    CHECK(rig.blocks.digState().target == BlockPos{0, 65, -3});
    CHECK(progress(rig) == 1);
    CHECK(rig.blocks.digState().required == 15);
}

TEST_CASE("Mining progress resets on release on a new target and on a changed block", "[server][interaction]")
{
    InteractionRig rig;
    rig.start();
    const data::BlockStateId stone = rig.state("aurora:stone");
    REQUIRE(rig.world.setBlock(kFront, stone));
    const auto mine = [&](int ticks) {
        for (int i = 0; i < ticks; ++i) {
            rig.step(kMineFront);
        }
    };

    SECTION("An applied input without attack")
    {
        mine(3);
        rig.step({.yaw = kNorth});
        CHECK(progress(rig) == 0);
        rig.step(kMineFront);
        CHECK(progress(rig) == 1);
    }
    SECTION("Use without attack resets even when the placing fails")
    {
        REQUIRE(rig.world.setBlock({0, 65, -1}, rig.state("aurora:light_air"))); // Not air: the place fails.
        mine(3);
        rig.step(useAt(kNorth, 0.0f, 0));
        CHECK(rig.blocks.lastPlaceResult() == PlaceResult::Occupied);
        CHECK(progress(rig) == 0);
        rig.step(kMineFront);
        CHECK(progress(rig) == 1);
    }
    SECTION("Use with attack: a failed place keeps the progress without adding to it")
    {
        REQUIRE(rig.world.setBlock({0, 65, -1}, rig.state("aurora:light_air")));
        mine(3);
        rig.step(useAt(kNorth, 0.0f, 0, true));
        CHECK(rig.blocks.lastPlaceResult() == PlaceResult::Occupied);
        CHECK(progress(rig) == 3);
        rig.step(kMineFront);
        CHECK(progress(rig) == 4);
    }
    SECTION("Use with attack: a placed block resets the progress")
    {
        mine(3);
        rig.step(useAt(kNorth, 0.0f, 3, true)); // Cobblestone in front of the stone.
        CHECK(rig.blocks.lastPlaceResult() == PlaceResult::Applied);
        CHECK(rig.blockAt({0, 65, -1}) == rig.state("aurora:cobblestone"));
        CHECK(progress(rig) == 0);
    }
    SECTION("Another cell, then back")
    {
        REQUIRE(rig.world.setBlock({1, 65, -2}, stone));
        mine(3);
        rig.step(attackAt(20.0f, 0.0f)); // A little east: (1, 65, -2).
        CHECK(rig.blocks.digState().target == BlockPos{1, 65, -2});
        CHECK(progress(rig) == 1);
        rig.step(kMineFront);
        CHECK(rig.blocks.digState().target == kFront);
        CHECK(progress(rig) == 1); // Not 4.
    }
    SECTION("The same cell with another state")
    {
        mine(3);
        REQUIRE(rig.world.setBlock(kFront, rig.state("aurora:cobblestone")));
        rig.step(kMineFront);
        CHECK(progress(rig) == 1);
        CHECK(rig.blocks.digState().required == 20);
    }
    SECTION("Changed and changed back between two ticks of the same load: progress goes on")
    {
        mine(3);
        REQUIRE(rig.world.setBlock(kFront, rig.state("aurora:dirt")));
        REQUIRE(rig.world.setBlock(kFront, stone));
        rig.step(kMineFront);
        CHECK(progress(rig) == 4);
    }
    SECTION("Starving and filling ticks wait")
    {
        mine(3);
        rig.tick(); // Starving: no input, so nothing happens and nothing resets.
        rig.tick();
        CHECK(progress(rig) == 3);
        rig.start(kMineFront); // Filling again with attack inputs: both apply.
        CHECK(progress(rig) == 5);
    }
    SECTION("Nothing breakable in front resets")
    {
        mine(3);
        rig.step(attackAt(180.0f, 0.0f)); // South: nothing within reach.
        CHECK(progress(rig) == 0);
        CHECK_FALSE(rig.blocks.digState().target);
    }
}

TEST_CASE("A reloaded chunk starts mining over even at the same cell and state", "[server][interaction]")
{
    InteractionRig rig;
    rig.start();
    const data::BlockStateId stone = rig.state("aurora:stone");
    REQUIRE(rig.world.setBlock(kFront, stone));
    for (int i = 0; i < 5; ++i) {
        rig.step(kMineFront);
    }
    const std::uint64_t firstLoad = *rig.world.generation({0, -1});
    rig.load({0, 20}, 1); // Everything around the player unloads (the player freezes)...
    rig.tick();
    rig.load({0, 0}, 1); // ...and comes back as new loads, without the edit.
    REQUIRE(rig.world.generation({0, -1}) != firstLoad);
    REQUIRE(rig.world.setBlock(kFront, stone));
    rig.tick();
    CHECK(progress(rig) == 5); // Waiting: no input arrived.
    rig.start(kMineFront);
    CHECK(progress(rig) == 2); // Started over in the new load.
    CHECK(rig.blocks.digState().generation == *rig.world.generation({0, -1}));
}

TEST_CASE("Blocks that cannot be mined get no progress", "[server][interaction]")
{
    SECTION("Unbreakable and unknown blocks")
    {
        InteractionRig rig;
        rig.start();
        for (const data::BlockStateId state : {rig.state("aurora:bedrock"), data::kUnknownState}) {
            REQUIRE(rig.world.setBlock(kFront, state));
            for (int i = 0; i < 30; ++i) {
                rig.step(kMineFront);
                CHECK(progress(rig) == 0);
            }
            CHECK(rig.blockAt(kFront) == state);
        }
        CHECK(rig.broken.empty());
    }
    SECTION("Blocks at the bedrock layer: y -49 can be mined, y -50 cannot")
    {
        // The player stands at y -48 in a shaft dug into the stone, looking down.
        InteractionRig rig({0.5, -48.0, 0.5});
        for (const std::int32_t y : {-48, -47, -46, -45}) {
            REQUIRE(rig.world.setBlock({0, y, 0}, data::kAirState));
        }
        rig.start();
        rig.step(attackAt(kNorth, kDown));
        CHECK(rig.blocks.digState().target == BlockPos{0, -49, 0});
        CHECK(progress(rig) == 1);
        REQUIRE(rig.world.setBlock({0, -49, 0}, data::kAirState)); // The player falls onto y -50.
        for (int i = 0; i < 10; ++i) {
            rig.step(attackAt(kNorth, kDown));
            CHECK(progress(rig) == 0);
        }
        CHECK(rig.blockAt({0, -50, 0}) == rig.state("aurora:stone"));
    }
}

TEST_CASE("Placing puts the palette block in front of the face that is looked at", "[server][interaction]")
{
    InteractionRig rig;
    rig.start();
    const data::BlockStateId stone = rig.state("aurora:stone");
    REQUIRE(rig.world.setBlock({0, 65, -4}, stone));

    // In a row towards the player: each new block's face is there for the next tick (the server's own world).
    for (std::int32_t z = -3; z <= -1; ++z) {
        rig.step(useAt(kNorth, 0.0f, 1)); // Dirt.
        INFO("z " << z);
        CHECK(rig.blocks.lastPlaceResult() == PlaceResult::Applied);
        CHECK(rig.blockAt({0, 65, z}) == rig.state("aurora:dirt"));
        CHECK(rig.state().lastPlaceInput == rig.player.lastTickInput());
        CHECK(rig.state().lastPlaceResult == PlaceResult::Applied);
    }
    // The next one would be inside the player.
    rig.step(useAt(kNorth, 0.0f, 1));
    CHECK(rig.blocks.lastPlaceResult() == PlaceResult::BlocksPlayer);
    CHECK(rig.blockAt({0, 65, 0}) == data::kAirState);
    // So would one under the feet... on the grass the player stands on.
    rig.step(useAt(kNorth, kDown, 0));
    CHECK(rig.blocks.lastPlaceResult() == PlaceResult::BlocksPlayer);
    CHECK(rig.blockAt({0, 64, 0}) == data::kAirState);

    // A slot the palette does not have; nothing hit.
    rig.step(useAt(kNorth, 0.0f, 6));
    CHECK(rig.blocks.lastPlaceResult() == PlaceResult::InvalidSlot);
    rig.step(useAt(180.0f, 0.0f, 0));
    CHECK(rig.blocks.lastPlaceResult() == PlaceResult::NoTarget);
    // Never more than one place per applied input.
    CHECK(rig.blocks.stats().blocksPlaced == 3);
    CHECK(rig.blocks.stats().placeAttempts == 7);
}

TEST_CASE("Placing needs air and never replaces a block or the unknown block", "[server][interaction]")
{
    InteractionRig rig;
    rig.start();
    REQUIRE(rig.world.setBlock(kFront, rig.state("aurora:stone")));
    // An invisible block in front of the face: the look passes it, but its cell is not air.
    REQUIRE(rig.world.setBlock({0, 65, -1}, rig.state("aurora:light_air")));
    rig.step(useAt(kNorth, 0.0f, 0));
    CHECK(rig.blocks.lastPlaceResult() == PlaceResult::Occupied);
    CHECK(rig.blockAt({0, 65, -1}) == rig.state("aurora:light_air"));

    // The unknown block is picked like any block and placed against, but never replaced or mined.
    REQUIRE(rig.world.setBlock({0, 65, -1}, data::kAirState));
    REQUIRE(rig.world.setBlock(kFront, data::kUnknownState));
    rig.step(useAt(kNorth, 0.0f, 0));
    CHECK(rig.blocks.lastPlaceResult() == PlaceResult::Applied);
    CHECK(rig.blockAt(kFront) == data::kUnknownState);
}

TEST_CASE("A placed log lies along the axis of the face it is placed on", "[server][interaction]")
{
    InteractionRig rig;
    rig.start();
    const data::BlockStateId stone = rig.state("aurora:stone");
    constexpr std::uint8_t kLog = 4;

    REQUIRE(rig.world.setBlock(kFront, stone)); // South face (+z): along z.
    rig.step(useAt(kNorth, 0.0f, kLog));
    CHECK(rig.blockAt({0, 65, -1}) == rig.state("aurora:oak_log[axis=z]"));

    REQUIRE(rig.world.setBlock({-2, 65, 0}, stone)); // East face (+x), looking west: along x.
    rig.step(useAt(270.0f, 0.0f, kLog));
    CHECK(rig.blockAt({-1, 65, 0}) == rig.state("aurora:oak_log[axis=x]"));

    rig.step(useAt(kNorth, -60.0f, kLog)); // The top of the grass in front: along y.
    CHECK(rig.blocks.lastPlaceResult() == PlaceResult::Applied);
    CHECK(rig.blockAt({0, 64, -1}) == rig.state("aurora:oak_log[axis=y]"));

    // Blocks without an axis use their default state.
    rig.step(useAt(180.0f, -60.0f, 0));
    CHECK(rig.blockAt({0, 64, 1}) == stone);
}

TEST_CASE("Placing touches the player but never overlaps it", "[server][interaction]")
{
    // The player's box runs x 0..0.6, z 0..0.6: its west side is on the plane x = 0.
    InteractionRig rig({0.3, 64.0, 0.3});
    rig.start();
    REQUIRE(rig.world.setBlock({-2, 64, 0}, rig.state("aurora:stone")));
    // Down to the west onto the east face of (-2, 64, 0): the new block at (-1, 64, 0) touches the player's side.
    rig.step(useAt(270.0f, -40.7f, 0));
    CHECK(rig.blocks.lastPlaceResult() == PlaceResult::Applied);
    CHECK(rig.blockAt({-1, 64, 0}) == rig.state("aurora:stone"));
}

TEST_CASE("Placing stays within the editable height", "[server][interaction]")
{
    SECTION("At the top: y 299 yes, y 300 no")
    {
        InteractionRig rig({0.5, 297.0, 0.5});
        const data::BlockStateId stone = rig.state("aurora:stone");
        REQUIRE(rig.world.setBlock({0, 296, 0}, stone)); // Something to stand on.
        REQUIRE(rig.world.setBlock({0, 299, -2}, stone));
        REQUIRE(rig.world.setBlock({1, 300, -2}, stone));
        rig.start();
        // Eyes at 298.62. Up north-east onto the south face of (1, 300, -2): the cell in front is y 300.
        rig.step(useAt(30.0f, 45.0f, 0));
        CHECK(rig.blocks.lastPlaceResult() == PlaceResult::Height);
        CHECK(rig.blockAt({1, 300, -1}) == data::kAirState);
        for (int i = 0; i < 20; ++i) {
            rig.step(attackAt(30.0f, 45.0f)); // Not minable either.
            CHECK(progress(rig) == 0);
        }
        // Up north onto the south face of (0, 299, -2): y 299 is fine.
        rig.step(useAt(kNorth, 30.0f, 0));
        CHECK(rig.blocks.lastPlaceResult() == PlaceResult::Applied);
        CHECK(rig.blockAt({0, 299, -1}) == stone);
    }
    SECTION("At the bottom: y -49 yes, y -50 no")
    {
        InteractionRig rig({0.5, -48.0, 0.5});
        for (const std::int32_t y : {-48, -47, -46, -45}) {
            REQUIRE(rig.world.setBlock({0, y, 0}, data::kAirState));
        }
        REQUIRE(rig.world.setBlock({0, -48, -1}, data::kAirState));
        REQUIRE(rig.world.setBlock({0, -49, -1}, data::kAirState));
        REQUIRE(rig.world.setBlock({0, -50, -1}, data::kAirState));
        rig.start();
        // Eyes at -46.38, steeply down north through the dug cells: onto the south face of (0, -50, -2), so the
        // cell in front is (0, -50, -1).
        rig.step(useAt(kNorth, -70.0f, 0));
        CHECK(rig.blocks.lastPlaceResult() == PlaceResult::Height);
        CHECK(rig.blockAt({0, -50, -1}) == data::kAirState);
        REQUIRE(rig.world.setBlock({0, -50, -1}, rig.state("aurora:stone")));
        rig.step(useAt(kNorth, -70.0f, 0)); // Now onto the top of (0, -50, -1): the cell in front is y -49.
        CHECK(rig.blocks.lastPlaceResult() == PlaceResult::Applied);
        CHECK(rig.blockAt({0, -49, -1}) == rig.state("aurora:stone"));
    }
}

TEST_CASE("Actions act once per applied input whatever arrives", "[server][interaction]")
{
    InteractionRig rig;
    rig.start();
    REQUIRE(rig.world.setBlock(kFront, rig.state("aurora:stone")));

    SECTION("A burst of inputs adds one tick of progress per server tick")
    {
        for (int i = 0; i < 4; ++i) {
            rig.send(kMineFront);
        }
        for (std::uint32_t tick = 1; tick <= 4; ++tick) {
            rig.tick();
            CHECK(progress(rig) == tick);
        }
    }
    SECTION("Inputs dropped from a full queue never act")
    {
        for (int i = 0; i < 6; ++i) {
            rig.send(useAt(kNorth, 0.0f, 0));
        }
        for (int tick = 0; tick < 6; ++tick) {
            rig.tick();
        }
        CHECK(rig.player.stats().droppedInputs == 2);
        CHECK(rig.blocks.stats().placeAttempts == 4);
    }
    SECTION("Neutralised inputs and a look that is not a number do nothing")
    {
        for (int i = 0; i < 3; ++i) {
            rig.step(kMineFront);
        }
        std::uint32_t last = 0;
        for (int i = 0; i < 3; ++i) {
            last = rig.send(useAt(kNorth, 0.0f, 0, true));
        }
        rig.neutralize(last);
        for (int tick = 0; tick < 3; ++tick) {
            rig.tick();
            CHECK(progress(rig) == 0);
        }
        CHECK(rig.blocks.stats().placeAttempts == 0);
        // Later inputs in the range arrive neutral too.
        rig.send(useAt(kNorth, 0.0f, 0));
        rig.tick();
        CHECK(rig.blocks.stats().placeAttempts == 1); // Sequence last + 1: outside the range, it acts.
    }
}

TEST_CASE("A look that is not a number neutralises the actions of the input", "[server][interaction]")
{
    const test::QuietLog quiet; // The bad look is logged.
    InteractionRig rig;
    rig.start();
    REQUIRE(rig.world.setBlock(kFront, rig.state("aurora:stone")));
    rig.step(kMineFront);
    rig.step({.yaw = std::numeric_limits<float>::quiet_NaN(), .attack = true, .use = true});
    CHECK(progress(rig) == 0);
    CHECK(rig.blocks.stats().placeAttempts == 0);
}

TEST_CASE("A Neutralize cancels mining even with no input left to neutralise", "[server][interaction]")
{
    InteractionRig rig;
    rig.start();
    REQUIRE(rig.world.setBlock(kFront, rig.state("aurora:stone")));
    std::uint32_t lastAttack = 0;
    for (int i = 0; i < 5; ++i) {
        lastAttack = rig.send(kMineFront);
        rig.tick();
    }
    REQUIRE(progress(rig) == 5);

    SECTION("The queue is empty: the Neutralize itself clears the progress, and a new attack starts at 1")
    {
        rig.neutralize(lastAttack);
        rig.tick(); // Starving: no input at all.
        CHECK(progress(rig) == 0);
        rig.start(kMineFront);
        CHECK(progress(rig) == 2);
    }
    SECTION("A Neutralize and the next attack in the same tick: the new attack starts at 1")
    {
        rig.neutralize(lastAttack);
        rig.step(kMineFront); // Sequence lastAttack + 1, applied in this very tick.
        CHECK(progress(rig) == 1);
    }
    SECTION("A lower range never cancels newer mining")
    {
        rig.neutralize(lastAttack);
        rig.step(kMineFront);
        rig.step(kMineFront);
        REQUIRE(progress(rig) == 2);
        rig.neutralize(lastAttack - 2); // An old, lower range: the max stays.
        rig.step(kMineFront);
        CHECK(progress(rig) == 3);
    }
}

TEST_CASE("While frozen mining waits unless it is cancelled", "[server][interaction]")
{
    // The player in chunk (1, 0), mining west into chunk (0, 0): (1, 0) can unload while (0, 0) stays.
    InteractionRig rig({16.5, 64.0, 0.5}, 2);
    rig.start();
    const BlockPos target{14, 65, 0};
    REQUIRE(rig.world.setBlock(target, rig.state("aurora:stone")));
    const MovementIntent mineWest = attackAt(270.0f, 0.0f);
    for (int i = 0; i < 3; ++i) {
        rig.step(mineWest);
    }
    REQUIRE(progress(rig) == 3);
    const std::uint64_t targetLoad = *rig.world.generation({0, 0});

    rig.load({-5, 0}, 4); // (1, 0) is 6 away and goes; (0, 0) is 5 away and stays.
    REQUIRE_FALSE(rig.world.generation({1, 0}));
    REQUIRE(rig.world.generation({0, 0}) == targetLoad);

    SECTION("Attack inputs while frozen: nothing happens, progress waits")
    {
        rig.step(mineWest);
        REQUIRE(rig.state().frozen);
        rig.step(mineWest);
        CHECK(progress(rig) == 3);
        rig.load({0, 0}, 2);
        rig.step(mineWest);
        REQUIRE_FALSE(rig.state().frozen);
        CHECK(progress(rig) == 4); // The same load of (0, 0): it goes on.
    }
    SECTION("A release while frozen still resets")
    {
        rig.step(mineWest);
        REQUIRE(rig.state().frozen);
        rig.step({.yaw = 270.0f});
        CHECK(progress(rig) == 0);
    }
    SECTION("A Neutralize while frozen still cancels")
    {
        const std::uint32_t last = rig.send(mineWest);
        rig.tick();
        REQUIRE(rig.state().frozen);
        rig.neutralize(last);
        rig.tick();
        CHECK(progress(rig) == 0);
    }
}
