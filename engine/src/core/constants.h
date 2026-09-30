#pragma once

#include <chrono>
#include <cstdint>

// Engine-wide constants. Game content (blocks, items, numbers) lives in game/data JSON, not here.
namespace aurora::core {

// Server simulation rate.
inline constexpr int kTicksPerSecond = 20;
inline constexpr double kSecondsPerTick = 1.0 / kTicksPerSecond;
inline constexpr std::chrono::milliseconds kTickInterval{1000 / kTicksPerSecond};
// If more ticks than this are due at once (the loop is 2 s or more behind), the backlog is dropped.
inline constexpr std::uint32_t kMaxCatchUpTicks = kTicksPerSecond * 2;

// Section = 16x16x16 blocks.
inline constexpr std::int32_t kSectionSize = 16;
inline constexpr std::int32_t kSectionBits = 4; // log2(kSectionSize): block >> 4 is the section, block & 15 the cell.
inline constexpr std::int32_t kSectionArea = kSectionSize * kSectionSize;
inline constexpr std::int32_t kSectionVolume = kSectionArea * kSectionSize;

// Internal vertical range [kWorldMinY, kWorldMaxY).
inline constexpr std::int32_t kWorldMinY = -64;
inline constexpr std::int32_t kWorldMaxY = 320;
inline constexpr std::int32_t kWorldHeight = kWorldMaxY - kWorldMinY;
inline constexpr std::int32_t kSectionsPerChunk = kWorldHeight / kSectionSize;

// Block state ids are 16-bit: at most this many states, including the built-in air and unknown states.
inline constexpr std::uint32_t kMaxBlockStates = 1u << 16;

// Gameplay limits enforced by rules, inside the internal range.
inline constexpr std::int32_t kBedrockY = -50;
inline constexpr std::int32_t kBuildLimitY = 300;

static_assert(1000 % kTicksPerSecond == 0, "kTickInterval must be a whole number of milliseconds");
static_assert((1 << kSectionBits) == kSectionSize);
static_assert(kWorldHeight % kSectionSize == 0);
static_assert(kSectionsPerChunk == 24);
static_assert(kWorldMinY <= kBedrockY && kBuildLimitY <= kWorldMaxY);

} // namespace aurora::core
