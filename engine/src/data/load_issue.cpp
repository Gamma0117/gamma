#include "data/load_issue.h"

#include "core/log.h"
#include "core/utf8.h"

#include <algorithm>

namespace aurora::data {

std::string formatIssue(const LoadIssue& issue)
{
    std::string text;
    if (!issue.file.empty()) {
        text = core::pathToUtf8(issue.file);
    }
    if (!issue.pointer.empty()) {
        if (!text.empty()) {
            text += ' ';
        }
        text += issue.pointer;
    }
    if (!text.empty()) {
        text += ": ";
    }
    text += issue.message;
    return text;
}

std::size_t countIssues(const std::vector<LoadIssue>& issues, IssueSeverity severity)
{
    return static_cast<std::size_t>(
        std::ranges::count_if(issues, [severity](const LoadIssue& issue) { return issue.severity == severity; }));
}

void logIssues(const std::vector<LoadIssue>& issues)
{
    for (const LoadIssue& issue : issues) {
        const core::LogLevel level = issue.severity == IssueSeverity::Error     ? core::LogLevel::Error
                                     : issue.severity == IssueSeverity::Warning ? core::LogLevel::Warn
                                                                                : core::LogLevel::Info;
        core::logMessage(level, "data", "{}", formatIssue(issue));
    }
}

} // namespace aurora::data
