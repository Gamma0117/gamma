#include "data/path_probe.h"

#include "core/utf8.h"

#include <format>
#include <iterator>
#include <system_error>

namespace aurora::data {

namespace {

namespace fs = std::filesystem;

// status() of the whole path said "not found". That covers a genuinely absent element, a link whose target is gone
// (at the end or anywhere in the middle; symlink_status() only stops following at the last element) and a middle
// element that is a file (ENOTDIR on POSIX). Walk from the root: every element before the last must resolve to a
// folder, so once one is not found, its parent is a real folder and the element is truly absent. Error codes differ
// between platforms, so the walk relies on the element types rather than on them.
PathProbe explainNotFound(const fs::path& path)
{
    fs::path prefix;
    for (auto element = path.begin(); element != path.end(); ++element) {
        prefix /= *element;
        const bool isLast = std::next(element) == path.end();

        std::error_code error;
        const fs::file_status entry = fs::symlink_status(prefix, error);
        if (entry.type() == fs::file_type::not_found) {
            return {PathKind::Missing, {}};
        }
        if (error) {
            return {PathKind::Failed, std::format("cannot inspect {}: {}", core::pathToUtf8(prefix), error.message())};
        }

        fs::file_status resolved = entry;
        if (entry.type() == fs::file_type::symlink) {
            resolved = fs::status(prefix, error);
            if (resolved.type() == fs::file_type::not_found) {
                return {PathKind::Failed,
                        std::format("broken link {} (its target does not exist)", core::pathToUtf8(prefix))};
            }
            if (error) {
                return {PathKind::Failed,
                        std::format("cannot follow {}: {}", core::pathToUtf8(prefix), error.message())};
            }
        }
        if (!isLast && resolved.type() != fs::file_type::directory) {
            return {PathKind::Failed, std::format("{} is not a folder", core::pathToUtf8(prefix))};
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
