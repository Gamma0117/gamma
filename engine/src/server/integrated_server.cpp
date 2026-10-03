#include "server/integrated_server.h"

#include "core/constants.h"
#include "core/log.h"
#include "core/profiler.h"
#include "core/thread.h"
#include "core/tick_scheduler.h"
#include "core/timing_history.h"
#include "data/player_movement.h"
#include "data/player_interaction.h"
#include "entity/collision_shapes.h"
#include "server/block_interaction.h"
#include "server/world_collision_view.h"
#include "world/flat_generator.h"
#include "world/world.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <iterator>
#include <string>
#include <utility>

namespace aurora::server {

namespace {

using Clock = ServerTestHooks::Clock;

// Tick-time statistics cover the last 5 seconds.
constexpr std::size_t kTickHistory = core::kTicksPerSecond * 5;

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
        m_stats.hasPlayer = m_stats.hasWorld && m_config.playerMovement;
        m_stats.hasInteraction = m_stats.hasPlayer && m_config.playerInteraction;
        m_frame = ServerFrame{};
        m_playerMessages.clear();
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

void IntegratedServer::setViewCenterOverride(std::optional<world::ChunkPos> center)
{
    std::lock_guard lock(m_mutex);
    m_viewCenterOverride = center;
}

ServerFrame IntegratedServer::takeFrame()
{
    std::lock_guard lock(m_mutex);
    return std::exchange(m_frame, {});
}

void IntegratedServer::sendPlayerInput(const entity::PlayerInput& input)
{
    std::lock_guard lock(m_mutex);
    m_playerMessages.push_back({.kind = PlayerMessage::Kind::Input, .input = input});
}

void IntegratedServer::neutralizePlayerInputs(std::uint32_t through)
{
    std::lock_guard lock(m_mutex);
    m_playerMessages.push_back({.kind = PlayerMessage::Kind::Neutralize, .through = through});
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

    // Owned by this thread only; other threads see copies in m_stats and the mailboxes.
    std::unique_ptr<world::World> world;
    std::unique_ptr<ServerPlayer> player;
    std::unique_ptr<BlockInteraction> interaction;
    std::shared_ptr<const entity::CollisionShapes> shapes = m_config.collisionShapes;
    const std::int32_t loadRadius = std::max(0, m_config.loadRadius);
    if (hasWorldConfig(m_config)) {
        world = std::make_unique<world::World>(m_config.blocks, *m_config.jobs,
                                               world::makeFlatGenerator(m_config.flatPreset));
        const std::int64_t side = 2 * static_cast<std::int64_t>(loadRadius) + 1;
        core::logInfo("server", "Flat world: loading {} chunks around the load center (radius {})", side * side,
                      loadRadius);
        if (m_config.playerMovement) {
            player = std::make_unique<ServerPlayer>(m_config.playerMovement);
            if (!shapes) {
                shapes = std::make_shared<const entity::CollisionShapes>(
                    entity::CollisionShapes::fromRegistry(*m_config.blocks));
            }
            if (m_config.playerInteraction) {
                interaction = std::make_unique<BlockInteraction>(m_config.playerInteraction, m_config.blocks, shapes);
            }
        }
    }
    const Simulation simulation{world.get(), player.get(), shapes.get(), interaction.get()};

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
            tick(tickCount + 1, simulation);
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

            if (world && !spawnAreaReady) {
                const world::WorldStats worldStats = world->stats();
                if (worldStats.pendingChunks == 0) {
                    spawnAreaReady = true;
                    const std::chrono::duration<double, std::milli> elapsed = tickEnd - startTime;
                    core::logInfo("server", "Spawn area ready after {:.0f} ms: {} chunks loaded, {} failed",
                                  elapsed.count(), worldStats.loadedChunks, worldStats.failedChunks);
                }
            }

            // The loop's own timing; the chunk and player stats are published with the tick (see tick()).
            lock.lock();
            m_stats.tickCount = tickCount;
            m_stats.skippedTicks = skippedTicks;
            m_stats.ticksPerSecond = ticksPerSecond;
            m_stats.tickTime = tickTimes.summary();
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

    interaction.reset();
    player.reset();
    world.reset(); // On this thread, which owns it. Queued generation jobs finish without it.
    const std::chrono::duration<double> uptime = now() - startTime;
    core::logInfo("server", "Server thread stopped: {} ticks in {:.2f} s, last measured {:.2f} TPS, {} skipped",
                  tickCount, uptime.count(), ticksPerSecond, skippedTicks);
}

void IntegratedServer::tick(std::uint64_t tickNumber, const Simulation& simulation)
{
    AURORA_PROFILE_ZONE_N("Server tick");
    if (m_hooks.onTick) {
        m_hooks.onTick(tickNumber);
    }
    if (!simulation.world) {
        return;
    }
    world::World& world = *simulation.world;
    ServerPlayer* player = simulation.player;

    std::vector<PlayerMessage> messages;
    std::optional<world::ChunkPos> override;
    {
        std::lock_guard lock(m_mutex);
        messages = std::exchange(m_playerMessages, {});
        override = m_viewCenterOverride;
    }
    if (player) {
        for (const PlayerMessage& message : messages) {
            player->receive(message);
        }
    }

    world.ensureLoaded(loadCenter(player, override), std::max(0, m_config.loadRadius));
    world.update();

    if (player) {
        if (!player->spawned()) {
            trySpawn(world, *player);
        }
        const WorldCollisionView view(world);
        player->tick({view, *simulation.shapes});
        if (simulation.interaction && player->spawned()) {
            simulation.interaction->tick(world, *player, tickNumber, now());
        }
    }

    // Publish what this tick made, all at once: a client never sees the player's new state with the world of an
    // older tick, or the other way round.
    world.publishChanges();
    std::vector<world::ChunkUpdate> updates = world.takeChunkUpdates();
    const Clock::time_point publishedAt = now();
    for (world::ChunkUpdate& update : updates) {
        update.serverTick = tickNumber;
        update.publishedAt = publishedAt;
    }
    std::optional<entity::PlayerState> state;
    std::vector<entity::BlockBrokenEvent> broken;
    if (player && player->spawned()) {
        state = player->state(tickNumber);
        if (simulation.interaction) {
            simulation.interaction->describe(*state);
            broken = simulation.interaction->takeBrokenEvents();
        }
    }
    const world::WorldStats worldStats = world.stats();

    std::lock_guard lock(m_mutex);
    m_frame.chunkUpdates.insert(m_frame.chunkUpdates.end(), std::make_move_iterator(updates.begin()),
                                std::make_move_iterator(updates.end()));
    m_frame.broken.insert(m_frame.broken.end(), broken.begin(), broken.end());
    if (state) {
        m_frame.playerState = state;
        m_stats.player = player->stats();
    }
    if (simulation.interaction) {
        m_stats.interaction = simulation.interaction->stats();
    }
    m_stats.loadedChunks = worldStats.loadedChunks;
    m_stats.pendingChunks = worldStats.pendingChunks;
    m_stats.failedChunks = worldStats.failedChunks;
}

world::ChunkPos IntegratedServer::loadCenter(const ServerPlayer* player, std::optional<world::ChunkPos> override) const
{
    const world::ChunkPos spawnChunk = world::chunkPosOf({m_config.spawnColumn.x, 0, m_config.spawnColumn.z});
    if (player && !player->spawned()) {
        return spawnChunk; // The spawn area first, whatever the client asks for.
    }
    if (override) {
        return *override;
    }
    if (player) {
        const glm::dvec3& position = player->motion().position;
        return world::chunkPosOf({static_cast<std::int32_t>(std::floor(position.x)), 0,
                                  static_cast<std::int32_t>(std::floor(position.z))});
    }
    return spawnChunk;
}

void IntegratedServer::trySpawn(const world::World& world, ServerPlayer& player) const
{
    if (const std::optional<glm::dvec3> feet = findSpawn(world, m_config.spawnColumn, player.tuning())) {
        player.spawn(*feet);
        core::logInfo("server", "Player spawned at ({:.2f}, {:.2f}, {:.2f})", feet->x, feet->y, feet->z);
    }
}

Clock::time_point IntegratedServer::now() const
{
    return m_hooks.clock ? m_hooks.clock() : Clock::now();
}

} // namespace aurora::server
