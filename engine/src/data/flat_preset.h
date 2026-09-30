#pragma once

#include "data/block_loader.h"
#include "data/block_registry.h"
#include "data/load_issue.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>

namespace aurora::data {

struct FlatLayer {
    BlockStateId state = kAirState;
    std::uint32_t height = 0;
};

// Layers stacked from the bottom of the world (core::kWorldMinY) upwards; everything above them is air.
struct FlatPreset {
    std::vector<FlatLayer> layers;

    std::uint32_t totalHeight() const;
};

struct FlatPresetLoadResult {
    // Set only when the file had no errors.
    std::shared_ptr<const FlatPreset> preset;
    std::vector<LoadIssue> issues;
    std::filesystem::path file; // The file that was read; empty if none was found.
};

// Reads data/aurora/worldgen/flat.json from the last pack that has one (a mod replaces the base game's).
// - "layers": a non-empty array of {"block": state string, "height": whole number >= 1}, bottom first.
// - "block" is parsed strictly against `registry`: "aurora:oak_log" is its default state, "aurora:oak_log[axis=x]"
//   a given one. An unknown block is an error, never the unknown placeholder.
// - The heights must fit in the world (core::kWorldHeight); each one is checked against what is left before adding.
// The file must exist. Every problem is collected as a LoadIssue.
FlatPresetLoadResult loadFlatPreset(std::span<const DataPack> packs, const BlockRegistry& registry);

// Logs every issue and a summary line.
void logFlatPresetLoadResult(const FlatPresetLoadResult& result);

} // namespace aurora::data
