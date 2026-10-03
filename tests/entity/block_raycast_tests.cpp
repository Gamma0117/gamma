#include "client/camera.h"
#include "entity/block_raycast.h"

#include "entity_test_support.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

using namespace aurora;
using entity::RaycastResult;
using entity::RaycastStatus;
using test::kAir;
using test::kSolid;
using test::TestBlocks;

namespace {

constexpr data::BlockStateId kInvisible = 6; // Not selectable, like air.
constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

// Everything but air and kInvisible is picked.
const std::vector<bool>& selectable()
{
    static const std::vector<bool> table{false, true, true, true, true, true, false};
    return table;
}

// Open space with nothing solid inside the world height unless set.
TestBlocks openWorld()
{
    return TestBlocks(-1000);
}

RaycastResult cast(const TestBlocks& blocks, glm::dvec3 origin, glm::dvec3 direction, double reach = 5.0)
{
    return entity::raycastBlocks(blocks, selectable(), origin, direction, reach);
}

void checkHit(const RaycastResult& result, world::BlockPos cell, data::BlockFace face, glm::ivec3 normal,
              double distance)
{
    REQUIRE(result.status == RaycastStatus::Hit);
    CHECK(result.cell == cell);
    CHECK(result.face == face);
    CHECK(result.normal == normal);
    CHECK(result.distance == Catch::Approx(distance).margin(1e-9));
}

} // namespace

TEST_CASE("A ray picks the face of the block it enters on all six sides", "[entity][raycast]")
{
    TestBlocks blocks = openWorld();
    blocks.set(0, 65, 0, kSolid);
    using data::BlockFace;
    checkHit(cast(blocks, {0.5, 65.5, 3.5}, {0, 0, -1}), {0, 65, 0}, BlockFace::South, {0, 0, 1}, 2.5);
    checkHit(cast(blocks, {0.5, 65.5, -2.5}, {0, 0, 1}), {0, 65, 0}, BlockFace::North, {0, 0, -1}, 2.5);
    checkHit(cast(blocks, {3.5, 65.5, 0.5}, {-1, 0, 0}), {0, 65, 0}, BlockFace::East, {1, 0, 0}, 2.5);
    checkHit(cast(blocks, {-2.5, 65.5, 0.5}, {1, 0, 0}), {0, 65, 0}, BlockFace::West, {-1, 0, 0}, 2.5);
    checkHit(cast(blocks, {0.5, 68.5, 0.5}, {0, -1, 0}), {0, 65, 0}, BlockFace::Up, {0, 1, 0}, 2.5);
    checkHit(cast(blocks, {0.5, 62.5, 0.5}, {0, 1, 0}), {0, 65, 0}, BlockFace::Down, {0, -1, 0}, 2.5);

    // Down and north at 45 degrees: through (0, 66, 0) (air) onto the top of the block.
    const RaycastResult slanted = cast(blocks, {0.5, 67.0, 1.5}, {0, -1, -1});
    checkHit(slanted, {0, 65, 0}, BlockFace::Up, {0, 1, 0}, std::sqrt(2.0));
    CHECK(slanted.point.x == Catch::Approx(0.5));
    CHECK(slanted.point.y == Catch::Approx(66.0));
    CHECK(slanted.point.z == Catch::Approx(0.5));
}

TEST_CASE("Negative coordinates and starts on a grid plane", "[entity][raycast]")
{
    TestBlocks blocks = openWorld();
    using data::BlockFace;
    blocks.set(-3, -10, -7, kSolid);
    checkHit(cast(blocks, {-2.5, -9.5, -3.5}, {0, 0, -1}), {-3, -10, -7}, BlockFace::South, {0, 0, 1}, 2.5);
    checkHit(cast(blocks, {-4.25, -9.5, -6.5}, {1, 0, 0}), {-3, -10, -7}, BlockFace::West, {-1, 0, 0}, 1.25);

    // On the plane z = 1 moving -z: the first cell is z = 0 (air here), then the block at z = -1.
    blocks.set(0, 65, -1, kSolid);
    checkHit(cast(blocks, {0.5, 65.5, 1.0}, {0, 0, -1}), {0, 65, -1}, BlockFace::South, {0, 0, 1}, 1.0);
    // On the plane z = 0 moving +z: the first cell is z = 0.
    blocks.set(0, 70, 2, kSolid);
    checkHit(cast(blocks, {0.5, 70.5, 0.0}, {0, 0, 1}), {0, 70, 2}, BlockFace::North, {0, 0, -1}, 2.0);
    // On the plane x = 0 moving along it: the cells with x >= 0 (floor) are the ones walked.
    blocks.set(-1, 75, 0, kSolid);
    CHECK(cast(blocks, {0.0, 75.5, 3.5}, {0, 0, -1}).status == RaycastStatus::Miss);
    blocks.set(0, 75, 0, kSolid);
    checkHit(cast(blocks, {0.0, 75.5, 3.5}, {0, 0, -1}), {0, 75, 0}, BlockFace::South, {0, 0, 1}, 2.5);
    // A -0 component crosses nothing on its axis.
    checkHit(cast(blocks, {0.5, 75.5, 3.5}, {-0.0, 0.0, -1.0}), {0, 75, 0}, BlockFace::South, {0, 0, 1}, 2.5);
}

TEST_CASE("Rays through edges and corners step on every axis at once", "[entity][raycast]")
{
    using data::BlockFace;
    SECTION("An edge: the two cells it only grazes are never hit")
    {
        TestBlocks blocks = openWorld();
        blocks.set(1, 65, 0, kSolid);
        blocks.set(0, 65, 1, kSolid);
        blocks.set(1, 65, 1, kSolid);
        // From the middle of (0, 65, 0) through the edge at x = 1, z = 1.
        checkHit(cast(blocks, {0.5, 65.5, 0.5}, {1, 0, 1}), {1, 65, 1}, BlockFace::West, {-1, 0, 0},
                 std::sqrt(0.5));
    }
    SECTION("A corner: X then Y then Z decides the entry face")
    {
        TestBlocks blocks = openWorld();
        for (const auto& [x, y, z] : {std::array{1, 65, 0}, std::array{0, 66, 0}, std::array{0, 65, 1},
                                      std::array{1, 66, 0}, std::array{1, 65, 1}, std::array{0, 66, 1}}) {
            blocks.set(x, y, z, kSolid);
        }
        blocks.set(1, 66, 1, kSolid);
        checkHit(cast(blocks, {0.5, 65.5, 0.5}, {1, 1, 1}), {1, 66, 1}, BlockFace::West, {-1, 0, 0},
                 std::sqrt(0.75));
        // Moving -x, +y, -z through the corner of (0, 65, 0) at x = 0, y = 66, z = 0.
        TestBlocks other = openWorld();
        other.set(-1, 66, -1, kSolid);
        checkHit(cast(other, {0.5, 65.5, 0.5}, {-1, 1, -1}), {-1, 66, -1}, BlockFace::East, {1, 0, 0},
                 std::sqrt(0.75));
    }
}

TEST_CASE("A ray that starts inside a block or anywhere invalid picks nothing", "[entity][raycast]")
{
    TestBlocks blocks = openWorld();
    blocks.set(0, 65, 0, kSolid);
    CHECK(cast(blocks, {0.5, 65.5, 0.5}, {0, 0, -1}).status == RaycastStatus::Invalid);
    // On the face plane moving into the block: the block is the first cell.
    CHECK(cast(blocks, {0.5, 65.5, 1.0}, {0, 0, -1}).status == RaycastStatus::Invalid);

    const glm::dvec3 origin{0.5, 65.5, 3.5};
    CHECK(cast(blocks, origin, {0, 0, 0}).status == RaycastStatus::Invalid);
    CHECK(cast(blocks, origin, {-0.0, 0.0, -0.0}).status == RaycastStatus::Invalid);
    CHECK(cast(blocks, origin, {0, 0, kNaN}).status == RaycastStatus::Invalid);
    CHECK(cast(blocks, origin, {0, kInf, -1}).status == RaycastStatus::Invalid);
    CHECK(cast(blocks, {kNaN, 65.5, 3.5}, {0, 0, -1}).status == RaycastStatus::Invalid);
    CHECK(cast(blocks, {0.5, -kInf, 3.5}, {0, 0, -1}).status == RaycastStatus::Invalid);
    CHECK(cast(blocks, origin, {0, 0, -1}, kNaN).status == RaycastStatus::Invalid);
    CHECK(cast(blocks, origin, {0, 0, -1}, 0.09).status == RaycastStatus::Invalid);
    CHECK(cast(blocks, origin, {0, 0, -1}, 16.01).status == RaycastStatus::Invalid);
    CHECK(cast(blocks, {3.0e9, 65.5, 3.5}, {0, 0, -1}).status == RaycastStatus::Invalid);
    CHECK(cast(blocks, origin, {0, 0, -1}, 16.0).status == RaycastStatus::Hit);
    CHECK(cast(blocks, origin, {0, 0, -1}, 0.1).status == RaycastStatus::Miss);
}

TEST_CASE("Reach is measured to the face in blocks whatever the direction's length", "[entity][raycast]")
{
    TestBlocks blocks = openWorld();
    blocks.set(0, 65, 0, kSolid);
    const glm::dvec3 origin{0.5, 65.5, 5.0}; // The south face (z = 1) is exactly 4 away.
    CHECK(cast(blocks, origin, {0, 0, -1}, 4.0).status == RaycastStatus::Hit);
    CHECK(cast(blocks, origin, {0, 0, -1}, 4.0 - 1e-6).status == RaycastStatus::Miss);
    CHECK(cast(blocks, origin, {0, 0, -1}, 4.0 + 1e-6).status == RaycastStatus::Hit);
    // The block's centre is 4.5 away, but the face counts.
    CHECK(cast(blocks, origin, {0, 0, -1}, 4.2).status == RaycastStatus::Hit);

    for (const double length : {1.0, 10.0, 1e-30, 1e30}) {
        INFO("direction length " << length);
        const RaycastResult result = cast(blocks, origin, {0, 0, -length}, 4.0);
        checkHit(result, {0, 65, 0}, data::BlockFace::South, {0, 0, 1}, 4.0);
        CHECK(cast(blocks, origin, {0, 0, -length}, 3.9).status == RaycastStatus::Miss);
    }
    // Through the top edge of the block (y 66, z 1 at once): a tiny direction still measures in blocks.
    checkHit(cast(blocks, {0.5, 66.5, 1.5}, {0, -1e-20, -1e-20}, 5.0), {0, 65, 0}, data::BlockFace::Up, {0, 1, 0},
             std::sqrt(0.5));
}

TEST_CASE("Air and invisible blocks are passed and unloaded columns stop the ray", "[entity][raycast]")
{
    TestBlocks blocks = openWorld();
    blocks.set(0, 65, 0, kSolid);
    blocks.set(0, 65, 2, kInvisible);
    blocks.set(0, 65, 1, kAir);
    checkHit(cast(blocks, {0.5, 65.5, 3.5}, {0, 0, -1}), {0, 65, 0}, data::BlockFace::South, {0, 0, 1}, 2.5);

    // A block beyond an unloaded column (z -16..-1) is never picked.
    TestBlocks gap = openWorld();
    gap.set(0, 65, -17, kSolid);
    gap.unload({0, -1});
    CHECK(cast(gap, {0.5, 65.5, 0.5}, {0, 0, -1}, 16.0).status == RaycastStatus::Unloaded);
    // Starting in an unloaded column.
    CHECK(cast(gap, {0.5, 65.5, -5.5}, {0, 0, 1}).status == RaycastStatus::Unloaded);

    // Outside the world height is open space in a loaded column.
    TestBlocks high = openWorld();
    high.set(0, aurora::core::kWorldMaxY - 1, 0, kSolid);
    checkHit(cast(high, {0.5, 325.5, 0.5}, {0, -1, 0}, 6.0), {0, 319, 0}, data::BlockFace::Up, {0, 1, 0}, 5.5);
    TestBlocks low(-60); // "Solid" below -60 in the test view: the cells below the world are never read.
    CHECK(cast(low, {0.5, -66.5, 0.5}, {0, -1, 0}, 16.0).status == RaycastStatus::Miss);
}

TEST_CASE("Raycasts work at plus and minus thirty million", "[entity][raycast]")
{
    for (const std::int32_t base : {30'000'000, -30'000'000}) {
        INFO("base " << base);
        TestBlocks blocks = openWorld();
        blocks.set(base + 3, 65, base, kSolid);
        const double x = base + 0.5;
        checkHit(cast(blocks, {x, 65.5, base + 0.5}, {1, 0, 0}), {base + 3, 65, base}, data::BlockFace::West,
                 {-1, 0, 0}, 2.5);
        blocks.set(base, 65, base - 2, kSolid);
        checkHit(cast(blocks, {x, 65.5, base + 0.5}, {0, 0, -1}), {base, 65, base - 2}, data::BlockFace::South,
                 {0, 0, 1}, 1.5);
    }
    // The last cells of the coordinate range: nothing beyond them.
    TestBlocks edge = openWorld();
    const double top = static_cast<double>(std::numeric_limits<std::int32_t>::max());
    CHECK(cast(edge, {top + 0.5, 65.5, 0.5}, {1, 0, 0}).status == RaycastStatus::Miss);
}

TEST_CASE("The look direction and eye position are shared with the camera", "[entity][raycast]")
{
    const auto close = [](glm::dvec3 a, glm::dvec3 b) {
        return std::abs(a.x - b.x) < 1e-12 && std::abs(a.y - b.y) < 1e-12 && std::abs(a.z - b.z) < 1e-12;
    };
    CHECK(close(entity::lookDirection(0.0, 0.0), {0, 0, -1}));
    CHECK(close(entity::lookDirection(90.0, 0.0), {1, 0, 0}));
    CHECK(close(entity::lookDirection(180.0, 0.0), {0, 0, 1}));
    CHECK(close(entity::lookDirection(0.0, 90.0), {0, 1, 0}));
    for (const auto& [yaw, pitch] : {std::array{12.5, -25.0}, std::array{300.0, 80.0}}) {
        const client::Camera camera({0, 0, 0}, yaw, pitch);
        CHECK(close(camera.forward(), entity::lookDirection(yaw, pitch)));
    }

    const data::PlayerMovementTuning tuning = test::standardTuning();
    entity::PlayerMotion motion = test::standingAt(1.5, 64.0, -2.5);
    CHECK(entity::eyePosition(motion, tuning) == glm::dvec3(1.5, 64.0 + tuning.eyeHeight, -2.5));
    motion.sneaking = true;
    CHECK(entity::eyePosition(motion, tuning) == glm::dvec3(1.5, 64.0 + tuning.sneakEyeHeight, -2.5));
}
