#pragma once

// Single data files that a later pack replaces as a whole (internal to the data module).

#include "data/block_loader.h"
#include "data/load_issue.h"

#include <filesystem>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace aurora::data {

// The file at `relative` (below a pack root) from the last pack that has one. Anything unusable in its place (a
// broken link, a folder) is an error rather than a reason to fall back to an earlier pack. No pack having it is an
// error too: "missing required file (<what>)".
std::optional<std::filesystem::path> findLastPackFile(std::span<const DataPack> packs,
                                                      const std::filesystem::path& relative, std::string_view what,
                                                      std::vector<LoadIssue>& issues);

} // namespace aurora::data
