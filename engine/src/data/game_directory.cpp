#include "data/game_directory.h"

#include "core/utf8.h"
#include "data/path_probe.h"
#include "data/resource_id.h"

#include <format>
#include <system_error>

namespace aurora::data {

GameDirectory resolveGameDirectory(const std::optional<std::filesystem::path>& explicitFolder,
                                   const std::vector<std::filesystem::path>& candidates)
{
    namespace fs = std::filesystem;
    if (explicitFolder) {
        const PathProbe probe = probePath(*explicitFolder);
        if (probe.kind == PathKind::Directory) {
            return {*explicitFolder, {}};
        }
        if (probe.kind == PathKind::Failed) {
            return {{}, std::format("cannot access --game-dir {}: {}", core::pathToUtf8(*explicitFolder),
                                    probe.failure)};
        }
        return {{}, std::format("--game-dir {} is not an existing folder", core::pathToUtf8(*explicitFolder))};
    }

    std::string tried;
    for (const fs::path& candidate : candidates) {
        // A candidate that is there but cannot be inspected (permissions, a broken link) stops the search: falling
        // through to the next folder would silently load different data.
        const fs::path marker = candidate / "data" / std::string(kBaseNamespace);
        for (const fs::path& path : {candidate, marker}) {
            const PathProbe probe = probePath(path);
            if (probe.kind == PathKind::Failed) {
                return {{}, std::format("cannot access {}: {}", core::pathToUtf8(path), probe.failure)};
            }
            if (probe.kind != PathKind::Directory) {
                break;
            }
            if (path == marker) {
                return {candidate, {}};
            }
        }
        tried += std::format("{}{}", tried.empty() ? "" : ", ", core::pathToUtf8(candidate));
    }
    return {{}, std::format("no game folder found (none of these has data/{}: {}); pass --game-dir <folder>",
                            kBaseNamespace, tried)};
}

} // namespace aurora::data
