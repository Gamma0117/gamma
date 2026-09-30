#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace aurora::data {

enum class PathKind : std::uint8_t {
    Missing,   // Nothing at this path.
    Directory, // Following links.
    File,      // A regular file, following links.
    Other,     // Something else: a device, a socket, ...
    Failed,    // Something is there but cannot be inspected: permissions, I/O, a looping or broken link.
};

struct PathProbe {
    PathKind kind = PathKind::Missing;
    std::string failure; // Why, when kind is Failed.
};

// Classifies `path`, following links. A link whose target does not exist, as the last element or anywhere in the
// middle of the path, is Failed, not Missing: status() alone reports it as "not found", which would let data behind
// a broken link be skipped silently. Only a genuinely absent element makes the path Missing.
PathProbe probePath(const std::filesystem::path& path);

} // namespace aurora::data
