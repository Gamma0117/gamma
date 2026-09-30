#include "data/flat_preset.h"
#include "world/chunk.h"
#include "world/flat_generator.h"

#include "../data/data_test_support.h"
#include "world_test_support.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>

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
