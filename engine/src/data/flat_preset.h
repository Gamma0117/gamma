#pragma once

#include "data/block_loader.h"
#include "data/block_registry.h"
#include "data/load_issue.h"

#include <array>
#include <cstddef>
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

// A box of one state, filled after the layers. Block coordinates, both corners included; from <= to on every
// axis (x, y, z).
struct FlatBox {
    BlockStateId state = kAirState;
    std::array<std::int32_t, 3> from{};
    std::array<std::int32_t, 3> to{};
};

inline constexpr std::size_t kMaxFlatBoxes = 256;
// Horizontal limit of box coordinates (the planned world border).
inline constexpr std::int32_t kMaxFlatBoxCoordinate = 30'000'000;

// Layers stacked from the bottom of the world (core::kWorldMinY) upwards; everything above them is air. Then the
// boxes, in file order: a later box wins where boxes overlap, and an air box carves.
struct FlatPreset {
    std::vector<FlatLayer> layers;
    std::vector<FlatBox> boxes;

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
// - "boxes" (optional): at most kMaxFlatBoxes {"block": state string, "from": [x, y, z], "to": [x, y, z]}. Whole
//   numbers only (2.0 and 1e3 are errors), x and z within +-kMaxFlatBoxCoordinate, y inside the world height,
//   from <= to on every axis. "block" is parsed as strictly as a layer's.
// The file must exist. Every problem is collected as a LoadIssue.
FlatPresetLoadResult loadFlatPreset(std::span<const DataPack> packs, const BlockRegistry& registry);

// Logs every issue and a summary line.
void logFlatPresetLoadResult(const FlatPresetLoadResult& result);

} // namespace aurora::data
