#pragma once

#include "data/block_loader.h"
#include "data/block_registry.h"
#include "data/block_textures.h"
#include "data/load_issue.h"
#include "data/resource_id.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace aurora::data {

// How the player breaks and places blocks and how breaking looks (data/<ns>/player/interaction.json). Lengths in
// blocks, times in seconds.
struct PlayerInteractionTuning {
    double reach = 0.0;                    // From the eyes to the face that is hit.
    double miningSecondsPerHardness = 0.0; // P0 rule: a block takes hardness x this many seconds of held attack.
    std::vector<ResourceId> palette;       // Blocks for the number keys, slot 0 first (P0 only: no inventory).
    std::uint32_t particleCount = 0;       // Fragments of one broken block.
    double particleLifetime = 0.0;
    double particleGravity = 0.0; // Blocks per second squared.
    double particleSpeed = 0.0;   // Largest start speed, blocks per second.
    double particleSize = 0.0;    // Edge of a fragment's square, in blocks.
    std::uint32_t maxParticles = 0; // Live fragments on the client at most; the oldest go first.
};

inline constexpr double kMinReach = 0.1;
inline constexpr double kMaxReach = 16.0;
inline constexpr std::size_t kMaxPaletteSize = 9;
// A block that needs more ticks than this to break cannot be broken (warned about when loading).
inline constexpr std::uint32_t kMaxMiningTicks = 1'000'000;
inline constexpr std::size_t kCrackStageCount = 10;

// Ticks of held attack that break a block of `hardness`: ceil(hardness x secondsPerHardness x ticks per second),
// at least 1, nullopt above kMaxMiningTicks. The product is computed in double, and a value just above a whole
// number by no more than the rounding of the float hardness (half the gap to its neighbouring floats, times the
// scale, plus a few double rounding steps) counts as that whole number: 0.6f is 0.6000000238, so 0.6 x 10 is 6
// ticks, not 7, while 1.00005 x 10 is still 11. This does not recover the decimal written in the JSON in general;
// with large hardness or many decimals the result can be one tick off the exact decimal product.
std::optional<std::uint32_t> miningTicks(float hardness, double secondsPerHardness);

// The loaded settings with the tables built from them against the block registry.
struct PlayerInteraction {
    PlayerInteractionTuning tuning;
    std::vector<BlockStateId> paletteStates; // Default state of each palette block, by slot.
    // Ticks to break each state (index = state id); 0 when it cannot be broken: air, the unknown block,
    // unbreakable blocks and blocks above kMaxMiningTicks.
    std::vector<std::uint32_t> miningTicks;
};

struct PlayerInteractionLoadResult {
    // Set only when the file had no errors.
    std::shared_ptr<const PlayerInteraction> interaction;
    std::vector<LoadIssue> issues;
    std::filesystem::path file; // The file that was read; empty if none was found.
};

// Reads data/aurora/player/interaction.json from the last pack that has one (a mod replaces the file as a whole).
// Every field is required:
//   reach                          finite, [kMinReach, kMaxReach]
//   mining_seconds_per_hardness    finite, (0, 30]
//   palette                        1..kMaxPaletteSize different block ids ("ns:block") of the registry, not air or
//                                  the unknown block
//   particle_count                 whole number [8, 16]
//   particle_lifetime              finite, (0, 5]
//   particle_gravity               finite, [0, 200]
//   particle_speed                 finite, [0, 20]
//   particle_size                  finite, (0, 0.25]
//   max_particles                  whole number [16, 4096]
// Unknown fields are warnings, repeated keys errors; a block that cannot be broken within kMaxMiningTicks is a
// warning naming it.
PlayerInteractionLoadResult loadPlayerInteraction(std::span<const DataPack> packs, const BlockRegistry& registry);

// Logs every issue and a summary line.
void logPlayerInteractionLoadResult(const PlayerInteractionLoadResult& result);

struct CrackTextureLoadResult {
    // assets/aurora/textures/block/destroy_stage_0..9.png in order, decoded; filled only without errors.
    std::vector<RgbaImage> stages;
    std::vector<LoadIssue> issues;
};

// Reads the ten crack stages from the last pack that has each one, with the block texture rules: a PNG of exactly
// kBlockTextureSize x kBlockTextureSize. Missing files, broken links and unreadable or wrong files are errors.
CrackTextureLoadResult loadCrackTextures(std::span<const DataPack> packs);

// Logs every issue and a summary line.
void logCrackTextureLoadResult(const CrackTextureLoadResult& result);

using Rgb = std::array<std::uint8_t, 3>;
// The colour of a block's fragments when no texel can give one.
inline constexpr Rgb kMissingParticleColour{255, 0, 255};

// One colour per texture: the mean RGB of the texels with alpha >= 128, or kMissingParticleColour when there are
// none (a fully cut-out texture).
Rgb meanOpaqueColour(const RgbaImage& image);

// A fragment colour for every state (index = state id): meanOpaqueColour of its block's north face texture. Blocks
// with an axis property are defined for axis=y, so the north face is a side face for every state. A block without
// that texture among `textures` (air, unknown) gets kMissingParticleColour.
std::vector<Rgb> blockParticleColours(const BlockRegistry& registry, std::span<const BlockTexture> textures);

} // namespace aurora::data
