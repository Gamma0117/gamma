#pragma once

#include "core/timing_history.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace aurora::server {

// Snapshot of the server loop for debug display. Plain data, copied out under the server's lock.
struct ServerStats {
    bool running = false;
    std::uint64_t tickCount = 0;
    std::uint64_t skippedTicks = 0;
    // Most ticks the scheduler handed out at once: 1 while keeping up, more while catching up. A stop request
    // still ends such a batch after the running tick.
    std::uint32_t maxScheduledBatch = 0;
    // Measured over the last full second; 0 until one second has passed.
    double ticksPerSecond = 0.0;
    // Work time per tick (excluding the sleep between ticks) over the recent ticks.
    core::TimingSummary tickTime;

    // The chunk table after the last tick. All zero for a server without a world.
    bool hasWorld = false;
    std::size_t loadedChunks = 0;
    std::size_t pendingChunks = 0;
    std::size_t failedChunks = 0;

    // Why the server thread stopped on its own (an exception in the loop). Empty otherwise.
    std::string error;
};

} // namespace aurora::server
