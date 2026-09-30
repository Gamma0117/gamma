#include "server/integrated_server.h"

#include "core/constants.h"
#include "core/log.h"
#include "core/profiler.h"
#include "core/thread.h"
#include "core/tick_scheduler.h"

#include <chrono>
#include <cstdint>

namespace aurora::server {

namespace {

using Clock = core::TickScheduler::Clock;

// Tick-time statistics cover the last 5 seconds.
constexpr std::size_t kTickHistory = core::kTicksPerSecond * 5;

float toMilliseconds(Clock::duration duration)
{
    return std::chrono::duration<float, std::milli>(duration).count();
}

} // namespace

IntegratedServer::~IntegratedServer()
{
    stop();
}

bool IntegratedServer::start()
{
    if (m_thread.joinable()) {
        core::logWarn("server", "Server is already running");
        return false;
    }
    {
        std::lock_guard lock(m_mutex);
        m_stopRequested = false;
        m_stats = ServerStats{};
        m_stats.running = true;
    }
    try {
        m_thread = std::thread(&IntegratedServer::run, this);
    } catch (...) {
        std::lock_guard lock(m_mutex);
        m_stats.running = false;
        throw;
    }
    return true;
}

void IntegratedServer::stop()
{
    if (!m_thread.joinable()) {
        return;
    }
    {
        std::lock_guard lock(m_mutex);
        m_stopRequested = true;
    }
    m_wake.notify_all();
    m_thread.join();

    std::lock_guard lock(m_mutex);
    m_stats.running = false;
}

ServerStats IntegratedServer::stats() const
{
    std::lock_guard lock(m_mutex);
    return m_stats;
}

void IntegratedServer::run()
{
    core::setCurrentThreadName("Server");
    core::logInfo("server", "Server thread started ({} ticks per second)", core::kTicksPerSecond);

    // Owned by this thread only; other threads see copies in m_stats.
    core::TickScheduler scheduler(core::kTickInterval, core::kMaxCatchUpTicks);
    core::TimingHistory tickTimes(kTickHistory);
    std::uint64_t tickCount = 0;
    std::uint64_t skippedTicks = 0;
    double ticksPerSecond = 0.0;

    const Clock::time_point startTime = Clock::now();
    scheduler.reset(startTime);
    // TPS counts the ticks that end within a window of at least one second after a reference tick.
    Clock::time_point rateWindowStart = startTime;
    std::uint32_t ticksInRateWindow = 0;

    std::unique_lock lock(m_mutex);
    while (!m_stopRequested) {
        lock.unlock();

        const core::TickScheduler::Advance advance = scheduler.advance(Clock::now());
        if (advance.ticksSkipped > 0) {
            skippedTicks += advance.ticksSkipped;
            const auto intervalMs = static_cast<std::uint64_t>(core::kTickInterval.count());
            const std::uint64_t behindMs = advance.ticksSkipped * intervalMs;
            core::logWarn("server", "Can't keep up! {} ms behind, skipping {} ticks", behindMs, advance.ticksSkipped);
        }

        for (std::uint32_t i = 0; i < advance.ticksToRun; ++i) {
            const Clock::time_point tickStart = Clock::now();
            tick();
            const Clock::time_point tickEnd = Clock::now();
            AURORA_PROFILE_FRAME_N("Server");

            ++tickCount;
            tickTimes.add(toMilliseconds(tickEnd - tickStart));
            if (tickCount == 1) {
                rateWindowStart = tickEnd; // The first tick is the reference, not part of a window.
            } else {
                ++ticksInRateWindow;
                const std::chrono::duration<double> rateWindow = tickEnd - rateWindowStart;
                if (rateWindow.count() >= 1.0) {
                    ticksPerSecond = ticksInRateWindow / rateWindow.count();
                    rateWindowStart = tickEnd;
                    ticksInRateWindow = 0;
                }
            }

            lock.lock();
            m_stats.tickCount = tickCount;
            m_stats.skippedTicks = skippedTicks;
            m_stats.ticksPerSecond = ticksPerSecond;
            m_stats.tickTime = tickTimes.summary();
            lock.unlock();
        }

        lock.lock();
        // Sleeps until the next deadline; stop() wakes it early.
        m_wake.wait_until(lock, scheduler.nextTickTime(), [this] { return m_stopRequested; });
    }
    lock.unlock();

    const std::chrono::duration<double> uptime = Clock::now() - startTime;
    core::logInfo("server", "Server thread stopped: {} ticks in {:.2f} s, last measured {:.2f} TPS, {} skipped",
                  tickCount, uptime.count(), ticksPerSecond, skippedTicks);
}

void IntegratedServer::tick()
{
    AURORA_PROFILE_ZONE_N("Server tick");
    // World, entities and game rules are simulated here from P0-4 on.
}

} // namespace aurora::server
