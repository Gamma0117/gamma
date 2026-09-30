#pragma once

#include <filesystem>
#include <string>

namespace aurora::core {

// UTF-8 text of a path, for logs and messages. path::string() would use the Windows code page instead.
inline std::string pathToUtf8(const std::filesystem::path& path)
{
    const std::u8string text = path.generic_u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

} // namespace aurora::core
