#pragma once

#include <cstdint>

// Engine-wide constants. Game content (blocks, items, numbers) lives in game/data JSON, not here.
namespace aurora::core {

// Server simulation rate.
inline constexpr int kTicksPerSecond = 20;
inline constexpr double kSecondsPerTick = 1.0 / kTicksPerSecond;

// Section = 16x16x16 blocks.
inline constexpr std::int32_t kSectionSize = 16;
inline constexpr std::int32_t kSectionArea = kSectionSize * kSectionSize;
inline constexpr std::int32_t kSectionVolume = kSectionArea * kSectionSize;

// Internal vertical range [kWorldMinY, kWorldMaxY).
inline constexpr std::int32_t kWorldMinY = -64;
inline constexpr std::int32_t kWorldMaxY = 320;
inline constexpr std::int32_t kWorldHeight = kWorldMaxY - kWorldMinY;
inline constexpr std::int32_t kSectionsPerChunk = kWorldHeight / kSectionSize;

// Gameplay limits enforced by rules, inside the internal range.
inline constexpr std::int32_t kBedrockY = -50;
inline constexpr std::int32_t kBuildLimitY = 300;

static_assert(kWorldHeight % kSectionSize == 0);
static_assert(kSectionsPerChunk == 24);
static_assert(kWorldMinY <= kBedrockY && kBuildLimitY <= kWorldMaxY);

} // namespace aurora::core
