#include "core/constants.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Test harness runs", "[sanity]")
{
    REQUIRE(1 + 1 == 2);
}

TEST_CASE("World layout constants match the P0 design", "[core][constants]")
{
    using namespace aurora::core;

    CHECK(kTicksPerSecond == 20);
    CHECK(kSectionSize == 16);
    CHECK(kSectionVolume == 4096);
    CHECK(kWorldMinY == -64);
    CHECK(kWorldMaxY == 320);
    CHECK(kSectionsPerChunk == 24);
    CHECK(kBedrockY == -50);
    CHECK(kBuildLimitY == 300);
}
