#pragma once

#include <chrono>
#include <cstdint>

namespace aurora::core {

// Fixed-rate tick timing on absolute deadlines: tick n is due at start + n * interval, so sleep jitter and
// tick cost never shift the schedule. Pure bookkeeping on steady_clock readings supplied by the caller,
// which does the sleeping (until nextTickTime()).
//
// A loop that falls behind runs the missed ticks back to back to catch up, at most maxCatchUpTicks at once.
// If more are due, the backlog is dropped: one tick runs and the schedule jumps to the first deadline after
// `now`, staying on the original grid.
class TickScheduler {
public:
    using Clock = std::chrono::steady_clock;
    using Duration = Clock::duration;
    using TimePoint = Clock::time_point;

    struct Advance {
        std::uint32_t ticksToRun = 0;   // Run this many ticks now, back to back.
        std::uint64_t ticksSkipped = 0; // Dropped because the loop fell too far behind.
    };

    // `interval` must be positive and `maxCatchUpTicks` at least 1.
    TickScheduler(Duration interval, std::uint32_t maxCatchUpTicks);

    // Starts the schedule: the first tick is due at `now`.
    void reset(TimePoint now);

    // Returns the ticks due at `now` and moves the schedule past them.
    Advance advance(TimePoint now);

    // Deadline of the next tick that has not been handed out yet.
    TimePoint nextTickTime() const { return m_nextTick; }
    Duration interval() const { return m_interval; }

private:
    Duration m_interval;
    std::uint32_t m_maxCatchUpTicks;
    TimePoint m_nextTick{};
};

} // namespace aurora::core
