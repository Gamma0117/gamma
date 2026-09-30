#include "core/tick_scheduler.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>

using namespace std::chrono_literals;
using aurora::core::TickScheduler;

namespace {

constexpr TickScheduler::Duration kInterval = 50ms;
constexpr std::uint32_t kMaxCatchUp = 40; // 2 s at 20 ticks per second

// Any fixed origin works: the scheduler only compares and subtracts time points.
const TickScheduler::TimePoint kStart = TickScheduler::TimePoint{} + 1000s;

} // namespace

TEST_CASE("First tick is due at reset and then once per interval", "[core][tick]")
{
    TickScheduler scheduler(kInterval, kMaxCatchUp);
    scheduler.reset(kStart);
    CHECK(scheduler.nextTickTime() == kStart);

    const TickScheduler::Advance first = scheduler.advance(kStart);
    CHECK(first.ticksToRun == 1);
    CHECK(first.ticksSkipped == 0);
    CHECK(scheduler.nextTickTime() == kStart + 50ms);

    CHECK(scheduler.advance(kStart + 49ms).ticksToRun == 0);
    CHECK(scheduler.nextTickTime() == kStart + 50ms);

    CHECK(scheduler.advance(kStart + 50ms).ticksToRun == 1);
    CHECK(scheduler.nextTickTime() == kStart + 100ms);
}

TEST_CASE("A late wake-up runs the missed ticks on the absolute schedule", "[core][tick]")
{
    TickScheduler scheduler(kInterval, kMaxCatchUp);
    scheduler.reset(kStart);

    // Deadlines at 0, 50, 100 and 150 ms have all passed.
    const TickScheduler::Advance advance = scheduler.advance(kStart + 175ms);
    CHECK(advance.ticksToRun == 4);
    CHECK(advance.ticksSkipped == 0);
    // Next deadline stays on the grid (200 ms), not wake-up time + interval (225 ms).
    CHECK(scheduler.nextTickTime() == kStart + 200ms);
}

TEST_CASE("Wake-up jitter never drifts the schedule", "[core][tick]")
{
    TickScheduler scheduler(kInterval, kMaxCatchUp);
    scheduler.reset(kStart);

    for (int n = 0; n < 1000; ++n) {
        const TickScheduler::Advance advance = scheduler.advance(kStart + kInterval * n + 7ms);
        REQUIRE(advance.ticksToRun == 1);
        REQUIRE(scheduler.nextTickTime() == kStart + kInterval * (n + 1));
    }
}

TEST_CASE("A backlog up to the catch-up limit is caught up", "[core][tick]")
{
    TickScheduler scheduler(kInterval, kMaxCatchUp);
    scheduler.reset(kStart);

    // 1999 ms late: deadlines 0 .. 1950 ms are due, exactly the limit of 40 ticks.
    const TickScheduler::Advance advance = scheduler.advance(kStart + 1999ms);
    CHECK(advance.ticksToRun == 40);
    CHECK(advance.ticksSkipped == 0);
    CHECK(scheduler.nextTickTime() == kStart + 2000ms);
}

TEST_CASE("A backlog over the catch-up limit is dropped and the grid kept", "[core][tick]")
{
    TickScheduler scheduler(kInterval, kMaxCatchUp);
    scheduler.reset(kStart);

    // 2000 ms late: deadlines 0 .. 2000 ms are due, 41 ticks, one over the limit.
    const TickScheduler::Advance advance = scheduler.advance(kStart + 2000ms);
    CHECK(advance.ticksToRun == 1);
    CHECK(advance.ticksSkipped == 40);
    CHECK(scheduler.nextTickTime() == kStart + 2050ms);

    // Back on the original 50 ms grid afterwards.
    CHECK(scheduler.advance(kStart + 2049ms).ticksToRun == 0);
    const TickScheduler::Advance next = scheduler.advance(kStart + 2050ms);
    CHECK(next.ticksToRun == 1);
    CHECK(next.ticksSkipped == 0);
    CHECK(scheduler.nextTickTime() == kStart + 2100ms);
}

TEST_CASE("A stall in the middle of a run is measured from the missed deadline", "[core][tick]")
{
    TickScheduler scheduler(kInterval, kMaxCatchUp);
    scheduler.reset(kStart);
    REQUIRE(scheduler.advance(kStart).ticksToRun == 1);
    REQUIRE(scheduler.advance(kStart + 50ms).ticksToRun == 1);

    // Next deadline is 100 ms. Waking at 100 + 1950 ms: 40 deadlines due, caught up.
    const TickScheduler::Advance caughtUp = scheduler.advance(kStart + 2050ms);
    CHECK(caughtUp.ticksToRun == 40);
    CHECK(caughtUp.ticksSkipped == 0);
    CHECK(scheduler.nextTickTime() == kStart + 2100ms);

    // Next deadline is 2100 ms. Waking at 2100 + 2000 ms: 41 due, dropped.
    const TickScheduler::Advance dropped = scheduler.advance(kStart + 4100ms);
    CHECK(dropped.ticksToRun == 1);
    CHECK(dropped.ticksSkipped == 40);
    CHECK(scheduler.nextTickTime() == kStart + 4150ms);
}

TEST_CASE("reset starts a new schedule", "[core][tick]")
{
    TickScheduler scheduler(kInterval, kMaxCatchUp);
    scheduler.reset(kStart);
    REQUIRE(scheduler.advance(kStart + 120ms).ticksToRun == 3);

    const TickScheduler::TimePoint restart = kStart + 10s + 3ms;
    scheduler.reset(restart);
    CHECK(scheduler.nextTickTime() == restart);
    CHECK(scheduler.advance(restart).ticksToRun == 1);
    CHECK(scheduler.nextTickTime() == restart + kInterval);
}
