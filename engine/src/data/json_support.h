#pragma once

// JSON reading shared by the data loaders (internal to the data module): files are parsed with duplicate-key
// detection, and every problem is reported as a LoadIssue with the file and the JSON pointer of the field.

#include "data/load_issue.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace aurora::data::json {

using Json = nlohmann::json;

template <std::size_t N>
bool contains(const std::array<std::string_view, N>& list, std::string_view value)
{
    return std::ranges::find(list, value) != list.end();
}

// RFC 6901 token escaping, so reported pointers stay unambiguous.
std::string pointerToken(std::string_view token);
std::string childPointer(std::string_view parent, std::string_view key);

// "string \"abc\"", "number 7.5", "array", ... for messages.
std::string describe(const Json& value);

const Json* findField(const Json& object, std::string_view key);

// Issues of one file.
class FileIssues {
public:
    FileIssues(std::vector<LoadIssue>& issues, std::filesystem::path file)
        : m_issues(issues)
        , m_file(std::move(file))
    {
    }

    void error(std::string pointer, std::string message)
    {
        m_issues.push_back({IssueSeverity::Error, m_file, std::move(pointer), std::move(message)});
        m_hasErrors = true;
    }
    void warning(std::string pointer, std::string message)
    {
        m_issues.push_back({IssueSeverity::Warning, m_file, std::move(pointer), std::move(message)});
    }
    void info(std::string pointer, std::string message)
    {
        m_issues.push_back({IssueSeverity::Info, m_file, std::move(pointer), std::move(message)});
    }

    bool hasErrors() const { return m_hasErrors; }
    const std::filesystem::path& file() const { return m_file; }

private:
    std::vector<LoadIssue>& m_issues;
    std::filesystem::path m_file;
    bool m_hasErrors = false;
};

std::optional<std::string> readFile(const std::filesystem::path& file, FileIssues& issues);

// Parses `text`; syntax errors and keys repeated within one object (at any depth) become errors of the file.
// nlohmann's exceptions stop here.
std::optional<Json> parseJson(const std::string& text, FileIssues& issues);

std::optional<bool> readBool(const Json& value, const std::string& pointer, FileIssues& issues);

// A warning for every key of `object` (at `pointer`) that is not in `known`: probably a typo.
void warnUnknownFields(const Json& object, std::span<const std::string_view> known, const std::string& pointer,
                       FileIssues& issues);

} // namespace aurora::data::json
