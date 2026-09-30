#pragma once

#include "core/timing_history.h"

#include <cstdint>

namespace aurora::server {

// Snapshot of the server loop for debug display. Plain data, copied out under the server's lock.
struct ServerStats {
    bool running = false;
    std::uint64_t tickCount = 0;
    std::uint64_t skippedTicks = 0;
    // Measured over the last full second; 0 until one second has passed.
    double ticksPerSecond = 0.0;
    // Work time per tick (excluding the sleep between ticks) over the recent ticks.
    core::TimingSummary tickTime;
};

} // namespace aurora::server
