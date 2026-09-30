#include "data/game_directory.h"

#include "core/utf8.h"
#include "data/resource_id.h"

#include <format>
#include <system_error>

namespace aurora::data {

GameDirectory resolveGameDirectory(const std::optional<std::filesystem::path>& explicitFolder,
                                   const std::vector<std::filesystem::path>& candidates)
{
    namespace fs = std::filesystem;
    std::error_code error;
    if (explicitFolder) {
        const fs::file_status status = fs::status(*explicitFolder, error);
        if (status.type() == fs::file_type::directory) {
            return {*explicitFolder, {}};
        }
        if (status.type() == fs::file_type::not_found || !error) {
            return {{}, std::format("--game-dir {} is not an existing folder", core::pathToUtf8(*explicitFolder))};
        }
        return {{}, std::format("cannot access --game-dir {}: {}", core::pathToUtf8(*explicitFolder),
                                error.message())};
    }

    std::string tried;
    for (const fs::path& candidate : candidates) {
        // A candidate that exists but cannot be inspected stops the search: falling through to the next folder
        // would silently load different data.
        const fs::path marker = candidate / "data" / std::string(kBaseNamespace);
        const fs::file_status status = fs::status(marker, error);
        if (status.type() == fs::file_type::directory) {
            return {candidate, {}};
        }
        if (status.type() != fs::file_type::not_found && error) {
            return {{}, std::format("cannot access {}: {}", core::pathToUtf8(marker), error.message())};
        }
        tried += std::format("{}{}", tried.empty() ? "" : ", ", core::pathToUtf8(candidate));
    }
    return {{}, std::format("no game folder found (none of these has data/{}: {}); pass --game-dir <folder>",
                            kBaseNamespace, tried)};
}

} // namespace aurora::data
