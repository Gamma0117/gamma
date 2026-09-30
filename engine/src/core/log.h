#pragma once

#include <cstdint>
#include <filesystem>
#include <format>
#include <string_view>
#include <utility>

namespace aurora::core {

enum class LogLevel : std::uint8_t {
    Trace,
    Debug,
    Info,
    Warn,
    Error,
    Off, // Only as a minimum level: disables all output.
};

// Fixed-width upper-case name ("INFO ", "WARN ", ...).
std::string_view logLevelName(LogLevel level);

struct LogConfig {
#ifdef NDEBUG
    LogLevel minLevel = LogLevel::Info;
#else
    LogLevel minLevel = LogLevel::Debug;
#endif
    bool console = true;
    // Folder for latest.log. The file from the previous run is kept as previous.log. Empty = no file output.
    std::filesystem::path directory;
};

// Process-wide, thread-safe logger writing to the console, a file and the profiler timeline.
// Line format: [12:34:56.789] [INFO ] [category] (thread) message
// Before init() and after shutdown() messages still reach the console, so late shutdown logs are never lost.
class Log {
public:
    // Opens <directory>/latest.log. Returns false if the file cannot be opened; console output keeps working.
    static bool init(const LogConfig& config);
    // Flushes and closes the file. Call last, after every thread that logs has been joined. Safe to repeat.
    static void shutdown();

    static void setMinLevel(LogLevel level);
    static LogLevel minLevel();
    static bool isEnabled(LogLevel level);

    // Writes one line. Prefer the logInfo/logWarn/... helpers, which skip formatting for disabled levels.
    static void write(LogLevel level, std::string_view category, std::string_view message);
};

template <typename... Args>
void logMessage(LogLevel level, std::string_view category, std::format_string<Args...> format, Args&&... args)
{
    if (Log::isEnabled(level)) {
        Log::write(level, category, std::format(format, std::forward<Args>(args)...));
    }
}

template <typename... Args>
void logTrace(std::string_view category, std::format_string<Args...> format, Args&&... args)
{
    logMessage(LogLevel::Trace, category, format, std::forward<Args>(args)...);
}

template <typename... Args>
void logDebug(std::string_view category, std::format_string<Args...> format, Args&&... args)
{
    logMessage(LogLevel::Debug, category, format, std::forward<Args>(args)...);
}

template <typename... Args>
void logInfo(std::string_view category, std::format_string<Args...> format, Args&&... args)
{
    logMessage(LogLevel::Info, category, format, std::forward<Args>(args)...);
}

template <typename... Args>
void logWarn(std::string_view category, std::format_string<Args...> format, Args&&... args)
{
    logMessage(LogLevel::Warn, category, format, std::forward<Args>(args)...);
}

template <typename... Args>
void logError(std::string_view category, std::format_string<Args...> format, Args&&... args)
{
    logMessage(LogLevel::Error, category, format, std::forward<Args>(args)...);
}

} // namespace aurora::core
