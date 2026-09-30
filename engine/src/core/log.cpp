#include "core/log.h"

#include "core/profiler.h"
#include "core/thread.h"
#include "core/utf8.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <mutex>
#include <string>
#include <system_error>

namespace aurora::core {

namespace {

constexpr const char* kLatestFileName = "latest.log";
constexpr const char* kPreviousFileName = "previous.log";

struct LogState {
    std::mutex mutex;
    std::ofstream file;
    bool console = true;
};

// Constant-initialized, so logging works before main() and during static destruction.
std::atomic<LogLevel> g_minLevel{LogLevel::Info};

LogState& state()
{
    // Intentionally leaked: threads or static destructors that log late must never see a destroyed mutex.
    static LogState* instance = new LogState();
    return *instance;
}

[[maybe_unused]] std::uint32_t profilerColor(LogLevel level)
{
    switch (level) {
    case LogLevel::Trace:
        return 0x808080;
    case LogLevel::Debug:
        return 0xb0b0b0;
    case LogLevel::Info:
        return 0xffffff;
    case LogLevel::Warn:
        return 0xffd040;
    case LogLevel::Error:
    case LogLevel::Off:
        return 0xff4040;
    }
    return 0xffffff;
}

std::string timestamp()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    const auto millis =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;

    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &seconds);
#else
    localtime_r(&seconds, &local);
#endif
    return std::format("{:02}:{:02}:{:02}.{:03}", local.tm_hour, local.tm_min, local.tm_sec, millis);
}

} // namespace

std::string_view logLevelName(LogLevel level)
{
    switch (level) {
    case LogLevel::Trace:
        return "TRACE";
    case LogLevel::Debug:
        return "DEBUG";
    case LogLevel::Info:
        return "INFO ";
    case LogLevel::Warn:
        return "WARN ";
    case LogLevel::Error:
        return "ERROR";
    case LogLevel::Off:
        return "OFF  ";
    }
    return "?    ";
}

bool Log::init(const LogConfig& config)
{
    setMinLevel(config.minLevel);

    LogState& s = state();
    std::filesystem::path latestPath;
    bool opened = false;
    {
        std::lock_guard lock(s.mutex);
        s.console = config.console;
        if (s.file.is_open()) {
            s.file.close();
        }
        if (config.directory.empty()) {
            return true;
        }

        std::error_code error;
        std::filesystem::create_directories(config.directory, error);
        latestPath = config.directory / kLatestFileName;
        if (std::filesystem::exists(latestPath, error)) {
            const std::filesystem::path previousPath = config.directory / kPreviousFileName;
            std::filesystem::remove(previousPath, error);
            std::filesystem::rename(latestPath, previousPath, error);
        }
        s.file.open(latestPath, std::ios::out | std::ios::trunc | std::ios::binary);
        opened = s.file.is_open();
    }

    if (!opened) {
        logError("log", "Cannot open log file {}; logging to the console only", pathToUtf8(latestPath));
        return false;
    }
    std::error_code error;
    const std::filesystem::path absolutePath = std::filesystem::absolute(latestPath, error);
    logInfo("log", "Log file: {}", pathToUtf8(error ? latestPath : absolutePath));
    return true;
}

void Log::shutdown()
{
    LogState& s = state();
    std::lock_guard lock(s.mutex);
    if (s.file.is_open()) {
        s.file.flush();
        s.file.close();
    }
    s.console = true;
}

void Log::setMinLevel(LogLevel level)
{
    g_minLevel.store(level, std::memory_order_relaxed);
}

LogLevel Log::minLevel()
{
    return g_minLevel.load(std::memory_order_relaxed);
}

bool Log::isEnabled(LogLevel level)
{
    return level != LogLevel::Off && level >= minLevel();
}

void Log::write(LogLevel level, std::string_view category, std::string_view message)
{
    if (!isEnabled(level)) {
        return;
    }

    // Format outside the lock; only the writes are serialized.
    const std::string body = std::format("[{}] ({}) {}", category, currentThreadName(), message);
    const std::string line = std::format("[{}] [{}] {}\n", timestamp(), logLevelName(level), body);

    AURORA_PROFILE_MESSAGE(body.data(), body.size(), profilerColor(level));

    LogState& s = state();
    std::lock_guard lock(s.mutex);
    if (s.console) {
        // Flushed per line so nothing is lost if the process crashes right after.
        std::fwrite(line.data(), 1, line.size(), stdout);
        std::fflush(stdout);
    }
    if (s.file.is_open()) {
        s.file.write(line.data(), static_cast<std::streamsize>(line.size()));
        s.file.flush();
    }
}

} // namespace aurora::core
