#include "data/path_probe.h"

#include <system_error>

namespace aurora::data {

PathProbe probePath(const std::filesystem::path& path)
{
    namespace fs = std::filesystem;

    std::error_code error;
    const fs::file_status status = fs::status(path, error);
    if (status.type() == fs::file_type::not_found) {
        // status() follows links, so "not found" also covers a link whose target is gone. Look at the entry itself.
        std::error_code linkError;
        const fs::file_status link = fs::symlink_status(path, linkError);
        if (link.type() == fs::file_type::symlink) {
            return {PathKind::Failed, "broken link (its target does not exist)"};
        }
        if (link.type() != fs::file_type::not_found && linkError) {
            return {PathKind::Failed, linkError.message()};
        }
        return {PathKind::Missing, {}};
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
