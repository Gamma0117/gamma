#include "data/block_loader.h"
#include "data/flat_preset.h"
#include "world/chunk.h"
#include "world/flat_generator.h"

#include "../data/data_test_support.h"
#include "world_test_support.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

using namespace aurora::world;
using aurora::test::stateOf;

namespace {

// Every block of two chunks is the same.
bool sameBlocks(const Chunk& a, const Chunk& b)
{
    for (std::int32_t y = aurora::core::kWorldMinY; y < aurora::core::kWorldMaxY; ++y) {
        for (std::int32_t z = 0; z < 16; ++z) {
            for (std::int32_t x = 0; x < 16; ++x) {
                if (a.getBlock(x, y, z) != b.getBlock(x, y, z)) {
                    return false;
                }
            }
        }
    }
    return true;
}

} // namespace

TEST_CASE("The standard flat chunk has stone to 59 dirt to 62 and grass at 63", "[world][flat]")
{
    const auto registry = aurora::test::makeTestRegistry();
    const auto preset = aurora::test::makeStandardFlatPreset(*registry);
    const BlockStateId stone = stateOf(*registry, "aurora:stone");
    const BlockStateId dirt = stateOf(*registry, "aurora:dirt");
    const BlockStateId grass = stateOf(*registry, "aurora:grass_block");

    const std::unique_ptr<Chunk> chunk = generateFlatChunk(*preset, ChunkPos{0, 0});
    REQUIRE(chunk);
    CHECK(chunk->pos() == ChunkPos{0, 0});
    CHECK(chunk->status() == ChunkStatus::Generated);

    for (const auto& [x, z] : {std::pair{0, 0}, std::pair{15, 15}, std::pair{7, 3}}) {
        INFO("column " << x << ", " << z);
        CHECK(chunk->getBlock(x, -64, z) == stone);
        CHECK(chunk->getBlock(x, 59, z) == stone);
        CHECK(chunk->getBlock(x, 60, z) == dirt);
        CHECK(chunk->getBlock(x, 62, z) == dirt);
        CHECK(chunk->getBlock(x, 63, z) == grass);
        CHECK(chunk->getBlock(x, 64, z) == aurora::data::kAirState);
        CHECK(chunk->getBlock(x, 319, z) == aurora::data::kAirState);
        CHECK(chunk->height(x, z) == 63);
    }

    // y -64..47 are seven whole stone sections of one state each; 48..63 mixes three layers; the sky is not stored.
    CHECK(chunk->sectionCount() == 8);
    for (std::int32_t index = 0; index < 7; ++index) {
        REQUIRE(chunk->section(index) != nullptr);
        CHECK(chunk->section(index)->bitsPerEntry() == 0);
        CHECK(chunk->section(index)->nonAirCount() == 4096);
    }
    REQUIRE(chunk->section(7) != nullptr);
    CHECK(chunk->section(7)->nonAirCount() == 4096);
    CHECK(chunk->section(8) == nullptr);
}

TEST_CASE("Flat chunks are the same at every position and on every run", "[world][flat]")
{
    const auto registry = aurora::test::makeTestRegistry();
    const auto preset = aurora::test::makeStandardFlatPreset(*registry);
    const std::unique_ptr<Chunk> origin = generateFlatChunk(*preset, ChunkPos{0, 0});

    for (const ChunkPos pos : {ChunkPos{-1, -1}, ChunkPos{-17, 5}, ChunkPos{1000, -2000}}) {
        INFO("chunk " << pos.x << ", " << pos.z);
        const std::unique_ptr<Chunk> chunk = generateFlatChunk(*preset, pos);
        CHECK(chunk->pos() == pos);
        CHECK(sameBlocks(*chunk, *origin));
    }
    CHECK(sameBlocks(*generateFlatChunk(*preset, ChunkPos{0, 0}), *origin));
}

TEST_CASE("Flat layers need not line up with sections and may hold air", "[world][flat]")
{
    const auto registry = aurora::test::makeTestRegistry();
    const BlockStateId stone = stateOf(*registry, "aurora:stone");
    const BlockStateId log = stateOf(*registry, "aurora:oak_log[axis=x]");
    aurora::data::FlatPreset preset;
    preset.layers = {{stone, 20}, {aurora::data::kAirState, 12}, {log, 5}};

    const std::unique_ptr<Chunk> chunk = generateFlatChunk(preset, ChunkPos{2, 2});
    CHECK(chunk->getBlock(5, -64, 5) == stone);
    CHECK(chunk->getBlock(5, -45, 5) == stone);
    CHECK(chunk->getBlock(5, -44, 5) == aurora::data::kAirState);
    CHECK(chunk->getBlock(5, -33, 5) == aurora::data::kAirState);
    CHECK(chunk->getBlock(5, -32, 5) == log);
    CHECK(chunk->getBlock(5, -28, 5) == log);
    CHECK(chunk->getBlock(5, -27, 5) == aurora::data::kAirState);
    CHECK(chunk->height(5, 5) == -28);
    CHECK(chunk->sectionCount() == 3);
    CHECK(chunk->section(0)->bitsPerEntry() == 0); // -64..-49, all stone.
}

TEST_CASE("A flat generator keeps its preset alive", "[world][flat]")
{
    const auto registry = aurora::test::makeTestRegistry();
    auto preset = aurora::test::makeStandardFlatPreset(*registry);
    const std::weak_ptr<const aurora::data::FlatPreset> watch = preset;
    const ChunkGenerator generator = makeFlatGenerator(std::move(preset));
    CHECK_FALSE(watch.expired());

    const std::unique_ptr<Chunk> chunk = generator(ChunkPos{-4, 9});
    REQUIRE(chunk);
    CHECK(chunk->pos() == ChunkPos{-4, 9});
    CHECK(chunk->height(0, 0) == 63);
}

TEST_CASE("Flat preset boxes fill only their part of each chunk in order", "[world][flat]")
{
    const auto registry = aurora::test::makeTestRegistry();
    const BlockStateId stone = stateOf(*registry, "aurora:stone");
    const BlockStateId dirt = stateOf(*registry, "aurora:dirt");
    const BlockStateId grass = stateOf(*registry, "aurora:grass_block");
    auto preset = std::make_shared<aurora::data::FlatPreset>(*aurora::test::makeStandardFlatPreset(*registry));
    preset->boxes = {
        // Across the border of chunks -1 and 0 (x -2..1) and of 0 and 1 in z (z 14..17).
        {dirt, {-2, 64, 14}, {1, 65, 17}},
        // A later box wins: one column of the first one becomes stone.
        {stone, {0, 65, 15}, {0, 65, 15}},
        // Carving: a pit in chunk (-1, -1) through the grass and dirt.
        {aurora::data::kAirState, {-5, 61, -5}, {-4, 63, -4}},
        // At the coordinate limits; no chunk tested here has any of it.
        {grass, {29'999'990, -64, 29'999'990}, {30'000'000, 319, 30'000'000}},
    };

    const std::unique_ptr<Chunk> origin = generateFlatChunk(*preset, ChunkPos{0, 0});
    CHECK(origin->getBlock(0, 64, 14) == dirt);
    CHECK(origin->getBlock(1, 65, 15) == dirt);
    CHECK(origin->getBlock(0, 65, 15) == stone);
    CHECK(origin->getBlock(2, 64, 14) == aurora::data::kAirState);  // Past x = 1.
    CHECK(origin->getBlock(0, 66, 14) == aurora::data::kAirState);  // Above y = 65.
    CHECK(origin->getBlock(0, 64, 13) == aurora::data::kAirState);  // Before z = 14.
    CHECK(origin->height(0, 14) == 65);
    CHECK(origin->height(5, 5) == 63);

    const std::unique_ptr<Chunk> west = generateFlatChunk(*preset, ChunkPos{-1, 0});
    CHECK(west->getBlock(14, 64, 14) == dirt); // x -2.
    CHECK(west->getBlock(13, 64, 14) == aurora::data::kAirState);
    const std::unique_ptr<Chunk> south = generateFlatChunk(*preset, ChunkPos{0, 1});
    CHECK(south->getBlock(1, 65, 1) == dirt); // z 17.
    CHECK(south->getBlock(1, 65, 2) == aurora::data::kAirState);

    // The carved pit: the height map sees it.
    const std::unique_ptr<Chunk> pit = generateFlatChunk(*preset, ChunkPos{-1, -1});
    CHECK(pit->getBlock(11, 63, 11) == aurora::data::kAirState); // x -5, z -5.
    CHECK(pit->getBlock(11, 61, 11) == aurora::data::kAirState);
    CHECK(pit->getBlock(11, 60, 11) == dirt);
    CHECK(pit->height(11, 11) == 60);
    CHECK(pit->height(10, 10) == 63); // x -6: untouched.

    // The far corner of the world: chunk 1874999 holds 29999984..29999999, the box starts at 29999990.
    const std::unique_ptr<Chunk> far = generateFlatChunk(*preset, ChunkPos{1'874'999, 1'874'999});
    CHECK(far->getBlock(6, 300, 6) == grass);
    CHECK(far->getBlock(5, 300, 6) == aurora::data::kAirState);
    const std::unique_ptr<Chunk> edge = generateFlatChunk(*preset, ChunkPos{1'875'000, 1'875'000});
    CHECK(edge->getBlock(0, 319, 0) == grass); // 30000000 itself.
    CHECK(edge->getBlock(1, 319, 0) == aurora::data::kAirState);
}

TEST_CASE("The shipped course leaves the spawn column and the view north clear", "[world][flat]")
{
    const std::vector<aurora::data::DataPack> packs{
        {"aurora", std::filesystem::path(AURORA_SOURCE_DIR) / "game", true}};
    const aurora::data::BlockLoadResult blocks = aurora::data::loadBlocks(packs);
    REQUIRE(blocks.registry);
    const aurora::data::FlatPresetLoadResult loaded = aurora::data::loadFlatPreset(packs, *blocks.registry);
    INFO(aurora::test::describeIssues(loaded.issues));
    REQUIRE(loaded.preset);
    CHECK_FALSE(loaded.preset->boxes.empty());

    // The spawn column (0, 0): grass on top at 63, so the player stands at 64 with nothing above.
    const std::unique_ptr<Chunk> origin = generateFlatChunk(*loaded.preset, ChunkPos{0, 0});
    CHECK(origin->height(0, 0) == 63);
    // The start view looks north (-z): the course lies south of z = 4.
    for (std::int32_t z = 0; z <= 4; ++z) {
        for (std::int32_t x = 0; x < 16; ++x) {
            CHECK(origin->height(x, z) == 63);
        }
    }
    for (const ChunkPos pos : {ChunkPos{0, -1}, ChunkPos{-1, -1}}) {
        const std::unique_ptr<Chunk> north = generateFlatChunk(*loaded.preset, pos);
        for (std::int32_t z = 0; z < 16; ++z) {
            for (std::int32_t x = 0; x < 16; ++x) {
                CHECK(north->height(x, z) == 63);
            }
        }
    }
}
