#pragma once

#include "server/server_stats.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace aurora::core {
class JobSystem;
}

namespace aurora::data {
class BlockRegistry;
struct FlatPreset;
} // namespace aurora::data

namespace aurora::world {
class World;
}

namespace aurora::server {

struct ServerConfig {
    // A world needs all three. Without them the server runs its tick loop with no world (loop tests).
    // The job system must outlive the server.
    core::JobSystem* jobs = nullptr;
    std::shared_ptr<const data::BlockRegistry> blocks;
    std::shared_ptr<const data::FlatPreset> flatPreset;
    // Chunks kept loaded around the origin, as a square radius: 8 is 17 x 17 = 289 chunks. From P0-5 on the
    // center follows the player.
    std::int32_t loadRadius = 8;
};

// Test seams. Both run on the server thread only.
struct ServerTestHooks {
    using Clock = std::chrono::steady_clock;

    // Called at the start of every tick with its number (1 for the first). May block. An exception from it stops
    // the server like any other error in the loop.
    std::function<void(std::uint64_t tickNumber)> onTick;
    // Replaces Clock::now() for the whole loop: scheduling, sleeping, tick timing and TPS. Must be monotonic and
    // safe to call while the test thread moves it. The loop still wakes at least every tick interval of real time
    // to read it again.
    std::function<Clock::time_point()> clock;
};

// The authoritative game simulation, run inside the game process on its own thread at a fixed
// core::kTicksPerSecond. The render loop runs independently at a variable frame rate; the client reaches the
// server through packets (P0-11).
//
// The World is created at the start of the server thread and destroyed before it ends, so the server thread is
// its owner: only it reads or writes world data. Other threads see stats() snapshots.
//
// Errors: an exception anywhere in the loop (world creation, a tick) is caught at the top of the thread, logged
// and put in ServerStats::error; the thread ends and stop() joins it as usual.
class IntegratedServer {
public:
    // A server with no world.
    IntegratedServer();
    explicit IntegratedServer(ServerConfig config, ServerTestHooks hooks = {});
    ~IntegratedServer();

    IntegratedServer(const IntegratedServer&) = delete;
    IntegratedServer& operator=(const IntegratedServer&) = delete;

    // Starts the server thread. Returns false if it is already running. Throws std::system_error if the thread
    // cannot be created (a fatal start-up error).
    bool start();
    // Asks the loop to stop and wakes it, without waiting. The running tick finishes; no further tick starts, not
    // even the rest of a catch-up batch. Thread-safe.
    void requestStop();
    // requestStop(), then joins the thread. Safe to call more than once.
    void stop();

    // Thread-safe snapshot.
    ServerStats stats() const;

private:
    void run();
    void runLoop();
    void tick(std::uint64_t tickNumber, world::World* world);
    ServerTestHooks::Clock::time_point now() const;

    const ServerConfig m_config;
    const ServerTestHooks m_hooks;
    std::thread m_thread;

    mutable std::mutex m_mutex; // Guards everything below.
    std::condition_variable m_wake;
    bool m_stopRequested = false;
    ServerStats m_stats;
};

} // namespace aurora::server
