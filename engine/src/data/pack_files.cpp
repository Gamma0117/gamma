#include "data/pack_files.h"

#include "data/path_probe.h"

#include <format>

namespace aurora::data {

std::optional<std::filesystem::path> findLastPackFile(std::span<const DataPack> packs,
                                                      const std::filesystem::path& relative, std::string_view what,
                                                      std::vector<LoadIssue>& issues)
{
    for (auto pack = packs.rbegin(); pack != packs.rend(); ++pack) {
        const std::filesystem::path candidate = pack->root / relative;
        const PathProbe probe = probePath(candidate);
        switch (probe.kind) {
        case PathKind::File:
            return candidate;
        case PathKind::Missing:
            continue;
        case PathKind::Failed:
            issues.push_back({IssueSeverity::Error, candidate, {}, std::format("cannot read: {}", probe.failure)});
            return std::nullopt;
        case PathKind::Directory:
        case PathKind::Other:
            issues.push_back({IssueSeverity::Error, candidate, {}, "expected a file"});
            return std::nullopt;
        }
    }
    const std::filesystem::path expected = packs.empty() ? relative : packs.front().root / relative;
    issues.push_back({IssueSeverity::Error, expected, {}, std::format("missing required file ({})", what)});
    return std::nullopt;
}

} // namespace aurora::data
