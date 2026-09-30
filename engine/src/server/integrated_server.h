#pragma once

#include "server/server_stats.h"

#include <condition_variable>
#include <mutex>
#include <thread>

namespace aurora::server {

// The authoritative game simulation, run inside the game process on its own thread at a fixed
// core::kTicksPerSecond. Only this thread will write world data (from P0-4 on); the client reaches it
// through packets (P0-11). The render loop runs independently at a variable frame rate.
class IntegratedServer {
public:
    IntegratedServer() = default;
    ~IntegratedServer();

    IntegratedServer(const IntegratedServer&) = delete;
    IntegratedServer& operator=(const IntegratedServer&) = delete;

    // Starts the server thread. Returns false if it is already running.
    bool start();
    // Wakes the loop, lets the current tick finish and joins the thread. Safe to call more than once.
    void stop();

    // Thread-safe snapshot.
    ServerStats stats() const;

private:
    void run();
    void tick();

    std::thread m_thread;

    mutable std::mutex m_mutex; // Guards everything below.
    std::condition_variable m_wake;
    bool m_stopRequested = false;
    ServerStats m_stats;
};

} // namespace aurora::server
