#include "server/integrated_server.h"

#include "core/constants.h"
#include "core/log.h"
#include "core/profiler.h"
#include "core/thread.h"
#include "core/tick_scheduler.h"
#include "core/timing_history.h"
#include "world/flat_generator.h"
#include "world/world.h"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <string>
#include <utility>

namespace aurora::server {

namespace {

using Clock = ServerTestHooks::Clock;

// Tick-time statistics cover the last 5 seconds.
constexpr std::size_t kTickHistory = core::kTicksPerSecond * 5;

// The chunk the loaded area is centred on until players exist (P0-5).
constexpr world::ChunkPos kSpawnChunk{0, 0};

float toMilliseconds(Clock::duration duration)
{
    return std::chrono::duration<float, std::milli>(duration).count();
}

bool hasWorldConfig(const ServerConfig& config)
{
    return config.jobs && config.blocks && config.flatPreset;
}

} // namespace

IntegratedServer::IntegratedServer()
    : IntegratedServer(ServerConfig{})
{
}

IntegratedServer::IntegratedServer(ServerConfig config, ServerTestHooks hooks)
    : m_config(std::move(config))
    , m_hooks(std::move(hooks))
{
    const bool partial = m_config.jobs || m_config.blocks || m_config.flatPreset;
    if (partial && !hasWorldConfig(m_config)) {
        core::logWarn("server", "Incomplete world settings (job system, blocks and preset are all needed): "
                                "the server runs without a world");
    }
}

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
        m_stats.hasWorld = hasWorldConfig(m_config);
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

void IntegratedServer::requestStop()
{
    {
        std::lock_guard lock(m_mutex);
        m_stopRequested = true;
    }
    m_wake.notify_all();
}

void IntegratedServer::stop()
{
    if (!m_thread.joinable()) {
        return;
    }
    requestStop();
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
    // Nothing may escape the thread (std::terminate). By the time a handler runs, the world has been destroyed
    // on this thread during unwinding.
    std::string failure;
    try {
        runLoop();
        return;
    } catch (const std::exception& e) {
        failure = e.what();
    } catch (...) {
        failure = "unknown exception";
    }
    core::logError("server", "Server thread stopped by an error: {}", failure);
    std::lock_guard lock(m_mutex);
    m_stats.running = false;
    m_stats.error = failure.empty() ? "unknown error" : std::move(failure);
}

void IntegratedServer::runLoop()
{
    core::logInfo("server", "Server thread started ({} ticks per second)", core::kTicksPerSecond);

    // Owned by this thread only; other threads see copies in m_stats.
    std::unique_ptr<world::World> world;
    const std::int32_t loadRadius = std::max(0, m_config.loadRadius);
    if (hasWorldConfig(m_config)) {
        world = std::make_unique<world::World>(m_config.blocks, *m_config.jobs,
                                               world::makeFlatGenerator(m_config.flatPreset));
        const std::int64_t side = 2 * static_cast<std::int64_t>(loadRadius) + 1;
        core::logInfo("server", "Flat world: loading {} chunks around the origin (radius {})", side * side,
                      loadRadius);
    }

    core::TickScheduler scheduler(core::kTickInterval, core::kMaxCatchUpTicks);
    core::TimingHistory tickTimes(kTickHistory);
    std::uint64_t tickCount = 0;
    std::uint64_t skippedTicks = 0;
    std::uint32_t maxScheduledBatch = 0;
    double ticksPerSecond = 0.0;
    bool spawnAreaReady = false;

    const Clock::time_point startTime = now();
    scheduler.reset(startTime);
    // TPS counts the ticks that end within a window of at least one second after a reference tick.
    Clock::time_point rateWindowStart = startTime;
    std::uint32_t ticksInRateWindow = 0;

    std::unique_lock lock(m_mutex);
    while (!m_stopRequested) {
        lock.unlock();

        const core::TickScheduler::Advance advance = scheduler.advance(now());
        if (advance.ticksSkipped > 0) {
            skippedTicks += advance.ticksSkipped;
            const auto intervalMs = static_cast<std::uint64_t>(core::kTickInterval.count());
            const std::uint64_t behindMs = advance.ticksSkipped * intervalMs;
            core::logWarn("server", "Can't keep up! {} ms behind, skipping {} ticks", behindMs, advance.ticksSkipped);
        }
        if (advance.ticksToRun > maxScheduledBatch) {
            maxScheduledBatch = advance.ticksToRun;
            lock.lock();
            m_stats.maxScheduledBatch = maxScheduledBatch;
            lock.unlock();
        }

        for (std::uint32_t i = 0; i < advance.ticksToRun; ++i) {
            const Clock::time_point tickStart = now();
            tick(tickCount + 1, world.get());
            const Clock::time_point tickEnd = now();
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

            const world::WorldStats worldStats = world ? world->stats() : world::WorldStats{};
            if (world && !spawnAreaReady && worldStats.pendingChunks == 0) {
                spawnAreaReady = true;
                const std::chrono::duration<double, std::milli> elapsed = tickEnd - startTime;
                core::logInfo("server", "Spawn area ready after {:.0f} ms: {} chunks loaded, {} failed",
                              elapsed.count(), worldStats.loadedChunks, worldStats.failedChunks);
            }

            lock.lock();
            m_stats.tickCount = tickCount;
            m_stats.skippedTicks = skippedTicks;
            m_stats.ticksPerSecond = ticksPerSecond;
            m_stats.tickTime = tickTimes.summary();
            m_stats.loadedChunks = worldStats.loadedChunks;
            m_stats.pendingChunks = worldStats.pendingChunks;
            m_stats.failedChunks = worldStats.failedChunks;
            // Checked between the ticks of a catch-up batch too, so stop never waits for the backlog.
            const bool stopRequested = m_stopRequested;
            lock.unlock();
            if (stopRequested) {
                break;
            }
        }

        lock.lock();
        // Sleeps until the next deadline; requestStop() wakes it early. A relative wait, so an injected test
        // clock decides how long is left.
        m_wake.wait_for(lock, scheduler.nextTickTime() - now(), [this] { return m_stopRequested; });
    }
    lock.unlock();

    world.reset(); // On this thread, which owns it. Queued generation jobs finish without it.
    const std::chrono::duration<double> uptime = now() - startTime;
    core::logInfo("server", "Server thread stopped: {} ticks in {:.2f} s, last measured {:.2f} TPS, {} skipped",
                  tickCount, uptime.count(), ticksPerSecond, skippedTicks);
}

void IntegratedServer::tick(std::uint64_t tickNumber, world::World* world)
{
    AURORA_PROFILE_ZONE_N("Server tick");
    if (m_hooks.onTick) {
        m_hooks.onTick(tickNumber);
    }
    if (world) {
        world->ensureLoaded(kSpawnChunk, std::max(0, m_config.loadRadius));
        world->update();
    }
}

Clock::time_point IntegratedServer::now() const
{
    return m_hooks.clock ? m_hooks.clock() : Clock::now();
}

} // namespace aurora::server
