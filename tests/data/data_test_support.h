#pragma once

#include "core/log.h"
#include "core/utf8.h"
#include "data/block_loader.h"
#include "data/block_registry.h"
#include "data/load_issue.h"
#include "data/resource_id.h"

#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace aurora::test {

// Silences logging for a scope, for tests that log errors on purpose.
class QuietLog {
public:
    QuietLog()
        : m_previous(core::Log::minLevel())
    {
        core::Log::setMinLevel(core::LogLevel::Off);
    }
    ~QuietLog() { core::Log::setMinLevel(m_previous); }

    QuietLog(const QuietLog&) = delete;
    QuietLog& operator=(const QuietLog&) = delete;

private:
    core::LogLevel m_previous;
};

// A throwaway folder under the system temp directory holding data packs as sub-folders. Removed afterwards.
class TempGame {
public:
    explicit TempGame(std::string_view name)
        : m_root(std::filesystem::temp_directory_path() /
                 std::format("aurora_data_test_{}_{}", name,
                             std::chrono::steady_clock::now().time_since_epoch().count()))
    {
        std::filesystem::remove_all(m_root);
        std::filesystem::create_directories(m_root);
    }

    ~TempGame()
    {
        std::error_code error;
        std::filesystem::remove_all(m_root, error);
    }

    TempGame(const TempGame&) = delete;
    TempGame& operator=(const TempGame&) = delete;

    const std::filesystem::path& root() const { return m_root; }

    // Writes <root>/<relativePath>, creating folders.
    void write(const std::filesystem::path& relativePath, std::string_view text) const
    {
        const std::filesystem::path path = m_root / relativePath;
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary);
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
    }

    // Texture files only need to exist for the loader; the content is not read.
    void texture(std::string_view pack, std::string_view nameSpace, std::string_view path) const
    {
        write(std::filesystem::path(std::string(pack)) / "assets" / std::string(nameSpace) / "textures" /
                  (std::string(path) + ".png"),
              "");
    }

    void block(std::string_view pack, std::string_view nameSpace, std::string_view fileName,
               std::string_view json) const
    {
        write(std::filesystem::path(std::string(pack)) / "data" / std::string(nameSpace) / "blocks" /
                  std::string(fileName),
              json);
    }

    data::DataPack pack(std::string_view name, bool isBase = false) const
    {
        return data::DataPack{std::string(name), m_root / std::string(name), isBase};
    }

private:
    std::filesystem::path m_root;
};

// Makes `link` a symbolic link to itself: every status query on it fails (ELOOP), even for root, which ignores
// permission bits. False where symlinks cannot be created (e.g. Windows without developer mode).
inline bool makeSelfLoop(const std::filesystem::path& link)
{
    std::error_code error;
    std::filesystem::create_directories(link.parent_path(), error);
    std::filesystem::create_symlink(link.filename(), link, error);
    return !error;
}

// Makes `link` a symbolic link to a path that does not exist. status() reports "not found" for it, just as for
// a missing path, so it tests that broken links are told apart. False where symlinks cannot be created.
inline bool makeBrokenLink(const std::filesystem::path& link)
{
    std::error_code error;
    std::filesystem::create_directories(link.parent_path(), error);
    std::filesystem::create_symlink("no_such_link_target", link, error);
    return !error;
}

// Every issue on its own line, for failure messages.
inline std::string describeIssues(const std::vector<data::LoadIssue>& issues)
{
    std::string text;
    for (const data::LoadIssue& issue : issues) {
        const char* severity = issue.severity == data::IssueSeverity::Error     ? "error"
                               : issue.severity == data::IssueSeverity::Warning ? "warning"
                                                                                : "info";
        text += std::format("  {}: {}\n", severity, data::formatIssue(issue));
    }
    return text.empty() ? "  (no issues)\n" : text;
}

// True if an issue has this severity, a file whose name is `fileName` (empty: any), this JSON pointer and a
// message containing `text`.
inline bool hasIssue(const std::vector<data::LoadIssue>& issues, data::IssueSeverity severity,
                     std::string_view fileName, std::string_view pointer, std::string_view text)
{
    for (const data::LoadIssue& issue : issues) {
        if (issue.severity == severity && (fileName.empty() || core::pathToUtf8(issue.file.filename()) == fileName) &&
            issue.pointer == pointer && issue.message.find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
}

// A registry built in memory (no files): aurora:dirt, aurora:grass_block, aurora:oak_log (axis x/y/z, default y)
// and aurora:stone after the built-in air (0) and unknown (1). State numbers follow the sorted ids: dirt 2,
// grass_block 3, oak_log 4-6 (axis=y is 5), stone 7.
inline std::shared_ptr<const data::BlockRegistry> makeTestRegistry()
{
    const auto block = [](std::string_view id) {
        data::BlockDefinition definition;
        definition.id = *data::ResourceId::parse(id);
        return definition;
    };
    std::vector<data::BlockDefinition> blocks{block("aurora:stone"), block("aurora:dirt"), block("aurora:grass_block")};
    data::BlockDefinition log = block("aurora:oak_log");
    log.properties = {data::BlockProperty{"axis", {"x", "y", "z"}}};
    log.defaultValues = {1};
    blocks.push_back(std::move(log));

    std::vector<data::LoadIssue> issues;
    return data::BlockRegistry::create(std::move(blocks), issues);
}

} // namespace aurora::test
