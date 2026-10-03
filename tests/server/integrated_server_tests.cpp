#include "core/job_system.h"
#include "core/log.h"
#include "data/flat_preset.h"
#include "server/integrated_server.h"

#include "../data/data_test_support.h"
#include "../entity/entity_test_support.h"
#include "../world/world_test_support.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
using aurora::server::IntegratedServer;
using aurora::server::ServerConfig;
using aurora::server::ServerStats;
using aurora::server::ServerTestHooks;

namespace {

using Clock = std::chrono::steady_clock;

// Polls until `done(stats)` holds. The 10 s limit only guards against a hang on a loaded machine.
bool waitForStats(const IntegratedServer& server, const std::function<bool(const ServerStats&)>& done)
{
    const Clock::time_point deadline = Clock::now() + 10s;
    while (!done(server.stats()) && Clock::now() < deadline) {
        std::this_thread::sleep_for(1ms);
    }
    return done(server.stats());
}

constexpr std::uint64_t kEveryTick = ~std::uint64_t{0};

// Holds each tick in the onTick hook until the test releases it, and tells the test which tick has entered.
class TickGate {
public:
    void onTick(std::uint64_t tickNumber)
    {
        std::unique_lock lock(m_mutex);
        m_entered = tickNumber;
        m_changed.notify_all();
        m_changed.wait(lock, [&] { return m_released >= tickNumber; });
    }

    // Waits (10 s hang guard) until tick `tickNumber` is inside the hook.
    bool waitForTick(std::uint64_t tickNumber)
    {
        std::unique_lock lock(m_mutex);
        return m_changed.wait_for(lock, 10s, [&] { return m_entered >= tickNumber; });
    }

    void release(std::uint64_t tickNumber)
    {
        {
            std::lock_guard lock(m_mutex);
            m_released = tickNumber;
        }
        m_changed.notify_all();
    }

    std::uint64_t entered()
    {
        std::lock_guard lock(m_mutex);
        return m_entered;
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_changed;
    std::uint64_t m_entered = 0;
    std::uint64_t m_released = 0;
};

// A steady clock that only moves when the test says so.
class FakeClock {
public:
    Clock::time_point now() const { return m_base + Clock::duration(m_offset.load()); }
    void advance(Clock::duration by) { m_offset += by.count(); }

private:
    const Clock::time_point m_base = Clock::now();
    std::atomic<Clock::rep> m_offset{0};
};

// Polls until the server has run `count` ticks. The 10 s limit only guards against a hang on a loaded machine.
bool waitForTicks(const IntegratedServer& server, std::uint64_t count)
{
    const Clock::time_point deadline = Clock::now() + 10s;
    while (server.stats().tickCount < count && Clock::now() < deadline) {
        std::this_thread::sleep_for(1ms);
    }
    return server.stats().tickCount >= count;
}

} // namespace

TEST_CASE("Integrated server ticks at a fixed rate on its own thread", "[server]")
{
    IntegratedServer server;
    CHECK_FALSE(server.stats().running);

    const Clock::time_point startTime = Clock::now();
    REQUIRE(server.start());
    CHECK(server.stats().running);

    REQUIRE(waitForTicks(server, 3));
    // Ticks are due 0, 50 and 100 ms after the loop starts, so a fixed-rate loop cannot reach three ticks
    // sooner than 100 ms (a free-running loop would). Load can only make this later.
    CHECK(Clock::now() - startTime >= 100ms);

    {
        const aurora::core::LogLevel previous = aurora::core::Log::minLevel();
        aurora::core::Log::setMinLevel(aurora::core::LogLevel::Off);
        CHECK_FALSE(server.start()); // Already running.
        aurora::core::Log::setMinLevel(previous);
    }

    // stop() wakes the sleeping loop instead of waiting out the interval; 2 s is only a hang guard.
    const Clock::time_point stopStart = Clock::now();
    server.stop();
    CHECK(Clock::now() - stopStart < 2s);

    const std::uint64_t ticksAtStop = server.stats().tickCount;
    CHECK_FALSE(server.stats().running);
    std::this_thread::sleep_for(150ms);
    CHECK(server.stats().tickCount == ticksAtStop);

    server.stop(); // Second stop is a no-op.
}

TEST_CASE("Integrated server can be restarted after stopping", "[server]")
{
    IntegratedServer server;
    server.stop(); // Stop before start is a no-op.

    REQUIRE(server.start());
    REQUIRE(waitForTicks(server, 1));
    server.stop();

    REQUIRE(server.start());
    CHECK(server.stats().running);
    REQUIRE(waitForTicks(server, 2));
    server.stop();
    CHECK_FALSE(server.stats().running);
}

TEST_CASE("A stop request ends a catch-up batch after the running tick", "[server]")
{
    TickGate gate;
    FakeClock clock;
    ServerTestHooks hooks;
    hooks.onTick = [&gate](std::uint64_t tickNumber) { gate.onTick(tickNumber); };
    hooks.clock = [&clock] { return clock.now(); };
    IntegratedServer server(ServerConfig{}, hooks);

    // Whatever happens below, never leave the server thread blocked in the hook.
    struct ReleaseAndStop {
        TickGate& gate;
        IntegratedServer& server;
        ~ReleaseAndStop()
        {
            server.requestStop();
            gate.release(kEveryTick);
            server.stop();
        }
    } releaseAndStop{gate, server};

    REQUIRE(server.start());
    REQUIRE(gate.waitForTick(1));
    // Tick 1 is due at 0 ms. While it runs, 200 ms pass: ticks at 50, 100, 150 and 200 ms are due, a batch of 4.
    clock.advance(200ms);
    gate.release(1);
    REQUIRE(gate.waitForTick(2)); // Tick 2 is the first of that batch.
    CHECK(server.stats().maxScheduledBatch >= 3);

    server.requestStop();
    // Release every later tick as well: a loop that ignored the request would run them and fail the count below
    // instead of hanging in the hook.
    gate.release(kEveryTick);
    server.stop();

    const ServerStats stats = server.stats();
    CHECK(stats.tickCount == 2); // The rest of the batch never ran.
    CHECK(stats.maxScheduledBatch >= 3);
    CHECK(gate.entered() == 2);
    CHECK_FALSE(stats.running);
    CHECK(stats.error.empty());
}

TEST_CASE("An exception in a tick stops the server thread and is reported", "[server]")
{
    const aurora::test::QuietLog quiet; // The error is logged on purpose.

    // With a world, so the unwinding also destroys the world on the server thread (asserted in Debug builds).
    const auto registry = aurora::test::makeTestRegistry();
    aurora::core::JobSystem jobs(2);
    ServerTestHooks hooks;
    hooks.onTick = [](std::uint64_t tickNumber) {
        if (tickNumber == 3) {
            throw std::runtime_error("tick hook failure");
        }
    };
    IntegratedServer server(
        ServerConfig{.jobs = &jobs, .blocks = registry, .flatPreset = aurora::test::makeStandardFlatPreset(*registry)},
        hooks);
    REQUIRE(server.start());
    REQUIRE(waitForStats(server, [](const ServerStats& stats) { return !stats.running; }));

    const ServerStats stats = server.stats();
    CHECK(stats.error == "tick hook failure");
    CHECK(stats.tickCount == 2); // The tick that threw is not counted.

    const Clock::time_point stopStart = Clock::now();
    server.stop(); // Joins the finished thread.
    CHECK(Clock::now() - stopStart < 2s);
    CHECK(server.stats().error == "tick hook failure");
}

TEST_CASE("The server generates the area around the view center on the job system", "[server]")
{
    const auto registry = aurora::test::makeTestRegistry();
    aurora::core::JobSystem jobs(2);
    IntegratedServer server(
        ServerConfig{.jobs = &jobs, .blocks = registry, .flatPreset = aurora::test::makeStandardFlatPreset(*registry)});
    CHECK(server.stats().loadedChunks == 0);

    REQUIRE(server.start());
    CHECK(server.stats().hasWorld);
    // The default radius is 9: 19 x 19 chunks.
    REQUIRE(waitForStats(server, [](const ServerStats& stats) {
        return stats.loadedChunks + stats.failedChunks == 361 && stats.pendingChunks == 0;
    }));
    const ServerStats stats = server.stats();
    CHECK(stats.loadedChunks == 361);
    CHECK(stats.failedChunks == 0);
    server.stop();
    CHECK(server.stats().loadedChunks == 361); // The last snapshot stays readable.
}

TEST_CASE("Chunk updates follow the view center in order", "[server]")
{
    using aurora::world::ChunkPos;
    using aurora::world::ChunkUpdate;
    const auto registry = aurora::test::makeTestRegistry();
    aurora::core::JobSystem jobs(2);
    IntegratedServer server(ServerConfig{.jobs = &jobs,
                                         .blocks = registry,
                                         .flatPreset = aurora::test::makeStandardFlatPreset(*registry),
                                         .loadRadius = 1});
    REQUIRE(server.start());

    // Collects updates until `done` holds for everything collected so far (10 s hang guard).
    std::vector<ChunkUpdate> updates;
    const auto collectUntil = [&](const std::function<bool()>& done) {
        const Clock::time_point deadline = Clock::now() + 10s;
        while (!done() && Clock::now() < deadline) {
            std::vector<ChunkUpdate> more = server.takeFrame().chunkUpdates;
            updates.insert(updates.end(), std::make_move_iterator(more.begin()), std::make_move_iterator(more.end()));
            std::this_thread::sleep_for(1ms);
        }
        return done();
    };
    const auto count = [&](ChunkUpdate::Kind kind) {
        return std::count_if(updates.begin(), updates.end(), [&](const ChunkUpdate& u) { return u.kind == kind; });
    };

    REQUIRE(collectUntil([&] { return count(ChunkUpdate::Kind::Loaded) == 9; }));
    for (const ChunkUpdate& update : updates) {
        REQUIRE(update.snapshot);
        CHECK(update.snapshot->pos() == update.pos);
        CHECK(update.snapshot->generation() == update.generation);
        CHECK(update.snapshot->getBlock(0, 63, 0) == aurora::test::stateOf(*registry, "aurora:grass_block"));
    }
    const std::uint64_t originGeneration =
        std::find_if(updates.begin(), updates.end(), [](const ChunkUpdate& u) { return u.pos == ChunkPos{0, 0}; })
            ->generation;

    // Far away: the nine old chunks are announced as unloaded (keep radius 2 is exceeded), nine new ones loaded.
    server.setViewCenterOverride(ChunkPos{10, 0});
    REQUIRE(collectUntil([&] { return count(ChunkUpdate::Kind::Loaded) == 18; }));
    CHECK(count(ChunkUpdate::Kind::Unloaded) == 9);
    // Indices, not iterators: collecting more updates may reallocate the vector.
    const auto indexOf = [&](ChunkUpdate::Kind kind, bool last) {
        std::optional<std::size_t> found;
        for (std::size_t i = 0; i < updates.size(); ++i) {
            if (updates[i].kind == kind && updates[i].pos == ChunkPos{0, 0} && (last || !found)) {
                found = i;
            }
        }
        return found;
    };
    const std::optional<std::size_t> unloadOrigin = indexOf(ChunkUpdate::Kind::Unloaded, false);
    REQUIRE(unloadOrigin);
    CHECK(updates[*unloadOrigin].generation == originGeneration);
    CHECK_FALSE(updates[*unloadOrigin].snapshot);

    // Back again: the origin loads anew with a larger generation, after its unload.
    server.setViewCenterOverride(ChunkPos{0, 0});
    REQUIRE(collectUntil([&] { return count(ChunkUpdate::Kind::Loaded) == 27; }));
    const std::optional<std::size_t> reload = indexOf(ChunkUpdate::Kind::Loaded, true);
    REQUIRE(reload);
    CHECK(updates[*reload].generation > originGeneration);
    CHECK(*reload > *unloadOrigin);
    server.stop();
}

TEST_CASE("A server whose jobs are refused still runs and stops", "[server]")
{
    const aurora::test::QuietLog quiet; // The error is logged on purpose.

    const auto registry = aurora::test::makeTestRegistry();
    aurora::core::JobSystem jobs(1);
    jobs.shutdown();
    IntegratedServer server(ServerConfig{.jobs = &jobs,
                                         .blocks = registry,
                                         .flatPreset = aurora::test::makeStandardFlatPreset(*registry),
                                         .loadRadius = 2});
    REQUIRE(server.start());
    REQUIRE(waitForStats(server, [](const ServerStats& stats) { return stats.tickCount >= 3; }));

    const ServerStats stats = server.stats();
    CHECK(stats.failedChunks == 25);
    CHECK(stats.pendingChunks == 0);
    CHECK(stats.loadedChunks == 0);
    CHECK(stats.running);
    server.stop();
    CHECK(server.stats().error.empty());
}

namespace {

std::shared_ptr<const aurora::data::PlayerMovementTuning> testMovement()
{
    return std::make_shared<const aurora::data::PlayerMovementTuning>(aurora::test::standardTuning());
}

// Polls takeFrame() until a player state arrives (10 s hang guard). The chunk updates are dropped.
std::optional<aurora::entity::PlayerState> waitForPlayerState(IntegratedServer& server)
{
    const Clock::time_point deadline = Clock::now() + 10s;
    while (Clock::now() < deadline) {
        if (std::optional<aurora::entity::PlayerState> state = server.takeFrame().playerState) {
            return state;
        }
        std::this_thread::sleep_for(1ms);
    }
    return std::nullopt;
}

} // namespace

TEST_CASE("The local player spawns on the surface and freezes while its chunk is away", "[server][player]")
{
    using aurora::world::ChunkPos;
    const auto registry = aurora::test::makeTestRegistry();
    aurora::core::JobSystem jobs(2);
    IntegratedServer server(ServerConfig{.jobs = &jobs,
                                         .blocks = registry,
                                         .flatPreset = aurora::test::makeStandardFlatPreset(*registry),
                                         .playerMovement = testMovement(),
                                         .loadRadius = 1});
    // Before the spawn the spawn area loads whatever the client asks for.
    server.setViewCenterOverride(ChunkPos{50, 50});
    REQUIRE(server.start());
    CHECK(server.stats().hasPlayer);

    const std::optional<aurora::entity::PlayerState> state = waitForPlayerState(server);
    REQUIRE(state);
    CHECK(state->motion.position == glm::dvec3(0.5, 64.0, 0.5));
    CHECK(state->lastInput == 0);
    CHECK(server.stats().player.spawned);

    // After the spawn the override decides: the player's chunk is unloaded and it freezes (rule B)...
    REQUIRE(waitForStats(server, [](const ServerStats& stats) { return stats.player.frozen; }));
    // ...and moves again once its chunk is back.
    server.setViewCenterOverride(std::nullopt);
    REQUIRE(waitForStats(server, [](const ServerStats& stats) { return !stats.player.frozen; }));
    const std::optional<aurora::entity::PlayerState> after = waitForPlayerState(server);
    REQUIRE(after);
    CHECK(after->motion.position == glm::dvec3(0.5, 64.0, 0.5));
    CHECK(after->serverTick > state->serverTick);
    server.stop();
}

TEST_CASE("The load center follows the player", "[server][player]")
{
    using aurora::world::ChunkPos;
    using aurora::world::ChunkUpdate;
    const auto registry = aurora::test::makeTestRegistry();
    aurora::core::JobSystem jobs(2);
    IntegratedServer server(ServerConfig{.jobs = &jobs,
                                         .blocks = registry,
                                         .flatPreset = aurora::test::makeStandardFlatPreset(*registry),
                                         .playerMovement = testMovement(),
                                         .loadRadius = 1,
                                         .spawnColumn = {15, 0}});
    REQUIRE(server.start());
    REQUIRE(waitForPlayerState(server));

    // Walk east over the border into chunk (1, 0): chunk (2, 0) comes into range.
    bool loadedEast = false;
    const Clock::time_point deadline = Clock::now() + 10s;
    for (std::uint32_t sequence = 1; !loadedEast && Clock::now() < deadline; ++sequence) {
        server.sendPlayerInput({sequence, {.forward = 1, .yaw = 90.0f}});
        for (const ChunkUpdate& update : server.takeFrame().chunkUpdates) {
            loadedEast = loadedEast || (update.kind == ChunkUpdate::Kind::Loaded && update.pos == ChunkPos{2, 0});
        }
        std::this_thread::sleep_for(50ms);
    }
    CHECK(loadedEast);
    server.stop();
}
