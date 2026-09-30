#include "core/log.h"
#include "core/thread.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <regex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

using aurora::core::Log;
using aurora::core::LogConfig;
using aurora::core::LogLevel;

// Fresh folder under the system temp directory. Closes the log file and restores the level afterwards.
class TempLogDirectory {
public:
    explicit TempLogDirectory(std::string_view name)
        : m_path(std::filesystem::temp_directory_path() /
                 std::format("aurora_log_test_{}_{}", name,
                             std::chrono::steady_clock::now().time_since_epoch().count()))
        , m_previousLevel(Log::minLevel())
    {
        std::filesystem::remove_all(m_path);
    }

    ~TempLogDirectory()
    {
        Log::shutdown();
        Log::setMinLevel(m_previousLevel);
        std::error_code error;
        std::filesystem::remove_all(m_path, error);
    }

    TempLogDirectory(const TempLogDirectory&) = delete;
    TempLogDirectory& operator=(const TempLogDirectory&) = delete;

    const std::filesystem::path& path() const { return m_path; }

    LogConfig config(LogLevel minLevel) const
    {
        return LogConfig{.minLevel = minLevel, .console = false, .directory = m_path};
    }

private:
    std::filesystem::path m_path;
    LogLevel m_previousLevel;
};

std::vector<std::string> readLines(const std::filesystem::path& path)
{
    std::vector<std::string> lines;
    std::ifstream file(path);
    for (std::string line; std::getline(file, line);) {
        lines.push_back(line);
    }
    return lines;
}

bool containsText(const std::vector<std::string>& lines, std::string_view text)
{
    for (const std::string& line : lines) {
        if (line.find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("Log writes formatted lines to latest.log", "[core][log]")
{
    TempLogDirectory directory("format");
    REQUIRE(Log::init(directory.config(LogLevel::Trace)));

    aurora::core::setCurrentThreadName("LogTest");
    aurora::core::logInfo("test", "value {} and {}", 42, "text");
    Log::shutdown();

    const std::vector<std::string> lines = readLines(directory.path() / "latest.log");
    const std::regex expected(R"(^\[\d{2}:\d{2}:\d{2}\.\d{3}\] \[INFO \] \[test\] \(LogTest\) value 42 and text$)");
    std::size_t matches = 0;
    for (const std::string& line : lines) {
        matches += std::regex_match(line, expected) ? 1 : 0;
    }
    CHECK(matches == 1);
}

TEST_CASE("Log drops messages below the minimum level", "[core][log]")
{
    TempLogDirectory directory("level");
    REQUIRE(Log::init(directory.config(LogLevel::Warn)));

    CHECK_FALSE(Log::isEnabled(LogLevel::Debug));
    CHECK_FALSE(Log::isEnabled(LogLevel::Info));
    CHECK(Log::isEnabled(LogLevel::Warn));
    CHECK(Log::isEnabled(LogLevel::Error));

    aurora::core::logTrace("test", "trace line");
    aurora::core::logDebug("test", "debug line");
    aurora::core::logInfo("test", "info line");
    aurora::core::logWarn("test", "warn line");
    aurora::core::logError("test", "error line");

    Log::setMinLevel(LogLevel::Off);
    CHECK_FALSE(Log::isEnabled(LogLevel::Error));
    aurora::core::logError("test", "line while off");
    Log::shutdown();

    const std::vector<std::string> lines = readLines(directory.path() / "latest.log");
    CHECK(containsText(lines, "[WARN ] [test]"));
    CHECK(containsText(lines, "warn line"));
    CHECK(containsText(lines, "[ERROR] [test]"));
    CHECK(containsText(lines, "error line"));
    CHECK_FALSE(containsText(lines, "trace line"));
    CHECK_FALSE(containsText(lines, "debug line"));
    CHECK_FALSE(containsText(lines, "info line"));
    CHECK_FALSE(containsText(lines, "line while off"));
}

TEST_CASE("Log keeps the previous run as previous.log", "[core][log]")
{
    TempLogDirectory directory("rotate");

    REQUIRE(Log::init(directory.config(LogLevel::Info)));
    aurora::core::logInfo("test", "first run");
    Log::shutdown();

    REQUIRE(Log::init(directory.config(LogLevel::Info)));
    aurora::core::logInfo("test", "second run");
    Log::shutdown();

    const std::vector<std::string> previous = readLines(directory.path() / "previous.log");
    const std::vector<std::string> latest = readLines(directory.path() / "latest.log");
    CHECK(containsText(previous, "first run"));
    CHECK_FALSE(containsText(previous, "second run"));
    CHECK(containsText(latest, "second run"));
    CHECK_FALSE(containsText(latest, "first run"));
}

TEST_CASE("Concurrent log lines are never interleaved", "[core][log]")
{
    constexpr int kThreads = 8;
    constexpr int kLinesPerThread = 500;

    TempLogDirectory directory("threads");
    REQUIRE(Log::init(directory.config(LogLevel::Info)));

    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([t] {
            aurora::core::setCurrentThreadName(std::format("W{}", t));
            for (int i = 0; i < kLinesPerThread; ++i) {
                aurora::core::logInfo("mt", "thread {} line {}", t, i);
            }
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
    Log::shutdown();

    // Every line must be whole, carry its own thread's name, and keep that thread's order.
    const std::regex expected(R"(^\[\d{2}:\d{2}:\d{2}\.\d{3}\] \[INFO \] \[mt\] \(W(\d+)\) thread (\d+) line (\d+)$)");
    std::vector<int> nextLine(kThreads, 0);
    int wholeLines = 0;
    bool ordered = true;
    for (const std::string& line : readLines(directory.path() / "latest.log")) {
        if (line.find("[mt]") == std::string::npos) {
            continue;
        }
        std::smatch match;
        if (!std::regex_match(line, match, expected) || match[1] != match[2]) {
            continue;
        }
        const int thread = std::stoi(match[2]);
        const int index = std::stoi(match[3]);
        if (thread < 0 || thread >= kThreads) {
            continue;
        }
        ordered = ordered && index == nextLine[thread];
        nextLine[thread] = index + 1;
        ++wholeLines;
    }
    CHECK(wholeLines == kThreads * kLinesPerThread);
    CHECK(ordered);
}
