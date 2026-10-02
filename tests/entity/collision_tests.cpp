#include "entity/aabb.h"
#include "entity/collision.h"
#include "entity/collision_shapes.h"

#include "../data/data_test_support.h"
#include "entity_test_support.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <vector>

using namespace aurora::entity;
using aurora::test::kSolid;
using aurora::test::TestBlocks;

TEST_CASE("Boxes overlap only by more than the epsilon", "[entity][collision]")
{
    const Aabb a{{0.0, 0.0, 0.0}, {1.0, 1.0, 1.0}};
    CHECK_FALSE(overlaps(a, a.moved({1.0, 0.0, 0.0})));           // Touching.
    CHECK_FALSE(overlaps(a, a.moved({1.0 - 1e-9, 0.0, 0.0})));    // Rounding-sized overlap.
    CHECK(overlaps(a, a.moved({1.0 - 1e-6, 0.0, 0.0})));
    CHECK(overlaps(a, a.moved({0.5, 0.5, 0.5})));
    CHECK_FALSE(overlapsOnAxis(a, a.moved({0.0, 2.0, 0.0}), kAxisY));
}

TEST_CASE("Cell ranges of a box on and across cell borders", "[entity][collision]")
{
    // On the border: [15.7, 16] covers cell 15 only, [16, 16.3] cell 16 only.
    CellRange cells = overlappedCells(15.7, 16.0);
    CHECK(cells.first == 15);
    CHECK(cells.last == 15);
    cells = overlappedCells(16.0, 16.3);
    CHECK(cells.first == 16);
    CHECK(cells.last == 16);
    cells = overlappedCells(-0.3, 0.3);
    CHECK(cells.first == -1);
    CHECK(cells.last == 0);
    // Candidates include touched cells.
    cells = touchedCells(15.7, 16.0);
    CHECK(cells.first == 15);
    CHECK(cells.last == 16);

    // The smallest and largest players never get an empty range, at the origin, chunk borders and the world border.
    for (const double centre : {0.0, 16.0, -16.0, 30'000'000.0, -30'000'000.0}) {
        for (const double width : {0.1, 4.0}) {
            INFO("centre " << centre << " width " << width);
            CHECK_FALSE(overlappedCells(centre - width / 2, centre + width / 2).empty());
        }
    }
}

TEST_CASE("Collision shapes come from the registry and are checked", "[entity][collision]")
{
    const auto registry = aurora::test::makeTestRegistry();
    const CollisionShapes shapes = CollisionShapes::fromRegistry(*registry);
    CHECK(shapes.stateCount() == registry->stateCount());
    CHECK(shapes.boxes(aurora::data::kAirState).empty());
    const auto stone = registry->parseState("aurora:stone").state;
    REQUIRE(stone);
    REQUIRE(shapes.boxes(*stone).size() == 1);
    CHECK(shapes.boxes(*stone)[0].max.y == 1.0);
    // A state beyond the table blocks.
    CHECK(shapes.boxes(static_cast<aurora::data::BlockStateId>(registry->stateCount())).size() == 1);

    CollisionShapes table(3);
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const Aabb inf[] = {{{0.0, 0.0, 0.0}, {1.0, std::numeric_limits<double>::infinity(), 1.0}}};
    const Aabb notANumber[] = {{{nan, 0.0, 0.0}, {1.0, 1.0, 1.0}}};
    const Aabb outside[] = {{{0.0, 0.0, 0.0}, {1.0, 1.5, 1.0}}};
    const Aabb thin[] = {{{0.0, 0.0, 0.0}, {1.0, 0.05, 1.0}}};
    const Aabb inverted[] = {{{0.0, 0.5, 0.0}, {1.0, 0.25, 1.0}}};
    const Aabb sixteenth[] = {{{0.0, 0.0, 0.0}, {1.0, 1.0 / 16.0, 1.0}}};
    CHECK(table.setShape(1, inf));
    CHECK(table.setShape(1, notANumber));
    CHECK(table.setShape(1, outside));
    CHECK(table.setShape(1, thin));
    CHECK(table.setShape(1, inverted));
    CHECK(table.setShape(5, sixteenth)); // No such state.
    const std::vector<Aabb> tooMany(CollisionShapes::kMaxBoxesPerState + 1, kFullCube);
    CHECK(table.setShape(1, tooMany));
    CHECK(table.boxes(1).empty()); // Unchanged by the failures.
    CHECK_FALSE(table.setShape(1, sixteenth));
    CHECK(table.boxes(1).size() == 1);
}

TEST_CASE("A sweep stops at the first box on the whole way", "[entity][collision]")
{
    const Aabb box{{0.2, 64.0, 0.2}, {0.8, 65.8, 0.8}};
    // A thin wall five blocks ahead, a move of eight blocks in one call.
    const std::vector<Aabb> wall{kFullCube.moved({5.0, 64.0, 0.0})};
    const SweepResult result = sweep(box, {8.0, 0.0, 0.0}, wall);
    CHECK(result.moved.x == 5.0 - 0.8);
    CHECK(result.blocked[kAxisX]);
    CHECK_FALSE(result.blocked[kAxisY]);

    // Already inside a box by more than the epsilon: it does not stop the way out.
    const std::vector<Aabb> inside{kFullCube.moved({0.0, 64.0, 0.0})};
    CHECK(clipAxis(box, kAxisX, 1.0, inside) == 1.0);
    // Touching ahead: no movement at all.
    const std::vector<Aabb> touching{kFullCube.moved({0.8, 64.0, 0.0})};
    CHECK(clipAxis(box, kAxisX, 1.0, touching) == 0.0);
    CHECK(clipAxis(box, kAxisX, -1.0, touching) == -1.0); // Away from it is free.
}

TEST_CASE("Unloaded columns are walls everywhere and inside the world only loaded columns hold blocks",
          "[entity][collision]")
{
    TestBlocks blocks;
    blocks.unload({1, 0});
    std::vector<Aabb> boxes;
    collectBoxes(blocks.world(), {{16.2, 300.2, 0.2}, {16.8, 300.8, 0.8}}, boxes);
    CHECK_FALSE(boxes.empty()); // Rule A at a height far above any block.

    // A loaded column: above the world is empty, below it is solid.
    boxes.clear();
    collectBoxes(blocks.world(), {{0.2, 330.2, 0.2}, {0.8, 330.8, 0.8}}, boxes);
    CHECK(boxes.empty());
    TestBlocks empty(-1000); // No floor at all inside the world.
    boxes.clear();
    collectBoxes(empty.world(), {{0.2, -70.0, 0.2}, {0.8, -69.0, 0.8}}, boxes);
    CHECK_FALSE(boxes.empty());
}

TEST_CASE("Only columns overlapped with positive extent freeze", "[entity][collision]")
{
    TestBlocks blocks;
    blocks.unload({1, 0});
    blocks.unload({-1, 0});
    const auto box = [](double x, double width) {
        return Aabb{{x - width / 2, 64.0, 0.5 - width / 2}, {x + width / 2, 65.8, 0.5 + width / 2}};
    };
    CHECK_FALSE(touchesUnloadedColumn(blocks, box(15.7, 0.6))); // Touches x = 16.
    CHECK(touchesUnloadedColumn(blocks, box(16.0, 0.6)));
    CHECK_FALSE(touchesUnloadedColumn(blocks, box(0.3, 0.6))); // Touches x = 0.
    CHECK(touchesUnloadedColumn(blocks, box(0.0, 0.6)));
    CHECK(touchesUnloadedColumn(blocks, box(16.0, 0.1)));       // The smallest player across the border.
    CHECK_FALSE(touchesUnloadedColumn(blocks, box(15.95, 0.1))); // ... and touching it.
    CHECK(touchesUnloadedColumn(blocks, box(14.6, 3.0))); // Reaches 16.1.
    CHECK_FALSE(touchesUnloadedColumn(blocks, box(8.0, 3.0)));
}
