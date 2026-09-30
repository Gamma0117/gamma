#include "core/log.h"
#include "server/integrated_server.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <thread>

using namespace std::chrono_literals;
using aurora::server::IntegratedServer;

namespace {

using Clock = std::chrono::steady_clock;

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
