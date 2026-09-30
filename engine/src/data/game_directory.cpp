#include "data/game_directory.h"

#include "core/utf8.h"
#include "data/resource_id.h"

#include <format>
#include <system_error>

namespace aurora::data {

GameDirectory resolveGameDirectory(const std::optional<std::filesystem::path>& explicitFolder,
                                   const std::vector<std::filesystem::path>& candidates)
{
    std::error_code error;
    if (explicitFolder) {
        if (std::filesystem::is_directory(*explicitFolder, error)) {
            return {*explicitFolder, {}};
        }
        return {{}, std::format("--game-dir {} is not an existing folder", core::pathToUtf8(*explicitFolder))};
    }

    std::string tried;
    for (const std::filesystem::path& candidate : candidates) {
        if (std::filesystem::is_directory(candidate / "data" / std::string(kBaseNamespace), error)) {
            return {candidate, {}};
        }
        tried += std::format("{}{}", tried.empty() ? "" : ", ", core::pathToUtf8(candidate));
    }
    return {{}, std::format("no game folder found (none of these has data/{}: {}); pass --game-dir <folder>",
                            kBaseNamespace, tried)};
}

} // namespace aurora::data
