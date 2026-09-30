#pragma once

#include <compare>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace aurora::data {

// Namespace of the base game and of the engine's built-in blocks.
inline constexpr std::string_view kBaseNamespace = "aurora";

// True for a non-empty run of [a-z0-9_]: namespaces, path segments, block property names and values.
bool isValidName(std::string_view text);
// One or more '/'-separated names: "stone", "loot/ogre". No leading, trailing or empty segments.
bool isValidPath(std::string_view text);

// "namespace:path", e.g. aurora:stone or aurora:loot/ogre. Always valid once constructed.
class ResourceId {
public:
    // Empty id ("no resource"); every accessor returns empty text.
    ResourceId() = default;

    // Strict form: requires "namespace:path".
    static std::optional<ResourceId> parse(std::string_view text);
    // Also accepts a bare "path", which then gets `defaultNamespace`.
    static std::optional<ResourceId> parse(std::string_view text, std::string_view defaultNamespace);

    bool empty() const { return m_text.empty(); }
    std::string_view nameSpace() const { return std::string_view(m_text).substr(0, m_separator); }
    std::string_view path() const
    {
        return empty() ? std::string_view{} : std::string_view(m_text).substr(m_separator + 1);
    }
    // True when the path has no '/', as block ids require.
    bool isSingleSegment() const { return path().find('/') == std::string_view::npos; }
    const std::string& str() const { return m_text; }

    bool operator==(const ResourceId& other) const { return m_text == other.m_text; }
    std::strong_ordering operator<=>(const ResourceId& other) const { return m_text <=> other.m_text; }

private:
    ResourceId(std::string text, std::size_t separator)
        : m_text(std::move(text))
        , m_separator(separator)
    {
    }

    std::string m_text;
    std::size_t m_separator = 0;
};

} // namespace aurora::data
