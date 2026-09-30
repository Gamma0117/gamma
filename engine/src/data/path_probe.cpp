#include "data/path_probe.h"

#include "core/utf8.h"

#include <format>
#include <system_error>

namespace aurora::data {

namespace {

namespace fs = std::filesystem;

// status() of the whole path said "not found". That covers a genuinely absent element and a link whose target is
// gone, at the end or anywhere in the middle (symlink_status() only stops following at the last element). Walk
// from the root to the first element that does not resolve and tell the two apart.
PathProbe explainNotFound(const fs::path& path)
{
    fs::path prefix;
    for (const fs::path& element : path) {
        prefix /= element;
        std::error_code error;
        const fs::file_status entry = fs::symlink_status(prefix, error);
        if (entry.type() == fs::file_type::not_found) {
            return {PathKind::Missing, {}};
        }
        if (error) {
            return {PathKind::Failed, std::format("cannot inspect {}: {}", core::pathToUtf8(prefix), error.message())};
        }
        if (entry.type() == fs::file_type::symlink) {
            const fs::file_status target = fs::status(prefix, error);
            if (target.type() == fs::file_type::not_found) {
                return {PathKind::Failed,
                        std::format("broken link {} (its target does not exist)", core::pathToUtf8(prefix))};
            }
            if (error) {
                return {PathKind::Failed,
                        std::format("cannot follow {}: {}", core::pathToUtf8(prefix), error.message())};
            }
        }
    }
    // Every element resolved on the second look: the path appeared in between. Treat it as absent this time.
    return {PathKind::Missing, {}};
}

} // namespace

PathProbe probePath(const fs::path& path)
{
    std::error_code error;
    const fs::file_status status = fs::status(path, error);
    if (status.type() == fs::file_type::not_found) {
        return explainNotFound(path);
    }
    if (error) {
        return {PathKind::Failed, error.message()};
    }
    switch (status.type()) {
    case fs::file_type::directory:
        return {PathKind::Directory, {}};
    case fs::file_type::regular:
        return {PathKind::File, {}};
    default:
        return {PathKind::Other, {}};
    }
}

} // namespace aurora::data
