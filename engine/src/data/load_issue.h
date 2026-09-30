#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace aurora::data {

enum class IssueSeverity : std::uint8_t {
    Info,    // Worth knowing, e.g. a later data pack replacing a block.
    Warning, // Suspicious but loadable, e.g. an unknown field (likely a typo).
    Error,   // The data cannot be used; loading fails.
};

// One problem found while loading game data. Bad data is reported this way, never by exceptions.
struct LoadIssue {
    IssueSeverity severity = IssueSeverity::Error;
    std::filesystem::path file; // Empty when the issue is not about one file.
    std::string pointer;        // JSON pointer of the field ("/textures/top"); empty for the whole file.
    std::string message;
};

// "<file> <pointer>: <message>", or without the parts that are empty.
std::string formatIssue(const LoadIssue& issue);

std::size_t countIssues(const std::vector<LoadIssue>& issues, IssueSeverity severity);

// Logs every issue at its severity under the "data" category.
void logIssues(const std::vector<LoadIssue>& issues);

} // namespace aurora::data
