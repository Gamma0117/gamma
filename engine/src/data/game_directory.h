#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace aurora::data {

struct GameDirectory {
    std::filesystem::path path; // Empty when none was found.
    std::string error;          // Why, when path is empty.
};

// Picks the game folder (the one holding data/ and assets/).
// - With `explicitFolder` (--game-dir): that folder if it exists, otherwise an error. Never another folder, so a
//   wrong path or broken data there is reported instead of silently loading something else.
// - Without: the first of `candidates` that has data/aurora.
GameDirectory resolveGameDirectory(const std::optional<std::filesystem::path>& explicitFolder,
                                   const std::vector<std::filesystem::path>& candidates);

} // namespace aurora::data
