#include "data/resource_id.h"

#include <algorithm>

namespace aurora::data {

namespace {

bool isNameChar(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
}

} // namespace

bool isValidName(std::string_view text)
{
    return !text.empty() && std::ranges::all_of(text, isNameChar);
}

bool isValidPath(std::string_view text)
{
    std::size_t start = 0;
    for (;;) {
        const std::size_t slash = text.find('/', start);
        if (!isValidName(text.substr(start, slash - start))) {
            return false;
        }
        if (slash == std::string_view::npos) {
            return true;
        }
        start = slash + 1;
    }
}

std::optional<ResourceId> ResourceId::parse(std::string_view text)
{
    const std::size_t colon = text.find(':');
    if (colon == std::string_view::npos) {
        return std::nullopt;
    }
    const std::string_view ns = text.substr(0, colon);
    const std::string_view path = text.substr(colon + 1);
    if (!isValidName(ns) || !isValidPath(path)) {
        return std::nullopt;
    }
    return ResourceId(std::string(text), colon);
}

std::optional<ResourceId> ResourceId::parse(std::string_view text, std::string_view defaultNamespace)
{
    if (text.find(':') != std::string_view::npos) {
        return parse(text);
    }
    if (!isValidName(defaultNamespace) || !isValidPath(text)) {
        return std::nullopt;
    }
    std::string full;
    full.reserve(defaultNamespace.size() + 1 + text.size());
    full.append(defaultNamespace).append(":").append(text);
    return ResourceId(std::move(full), defaultNamespace.size());
}

} // namespace aurora::data
