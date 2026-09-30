#include "core/timing_history.h"

#include <catch2/catch_test_macros.hpp>

using aurora::core::TimingHistory;
using aurora::core::TimingSummary;

TEST_CASE("An empty timing history reports zeros", "[core][timing]")
{
    const TimingHistory history(4);
    CHECK(history.empty());
    CHECK(history.size() == 0);
    CHECK(history.capacity() == 4);
    CHECK(history.offset() == 0);

    const TimingSummary summary = history.summary();
    CHECK(summary.latestMs == 0.0f);
    CHECK(summary.averageMs == 0.0f);
    CHECK(summary.minMs == 0.0f);
    CHECK(summary.maxMs == 0.0f);
}

TEST_CASE("Timing summary covers the stored samples", "[core][timing]")
{
    TimingHistory history(4);
    history.add(2.0f);
    history.add(4.0f);
    history.add(9.0f);

    CHECK(history.size() == 3);
    CHECK(history.at(0) == 2.0f);
    CHECK(history.at(2) == 9.0f);

    const TimingSummary summary = history.summary();
    CHECK(summary.latestMs == 9.0f);
    CHECK(summary.averageMs == 5.0f);
    CHECK(summary.minMs == 2.0f);
    CHECK(summary.maxMs == 9.0f);
}

TEST_CASE("A full timing history overwrites its oldest samples", "[core][timing]")
{
    TimingHistory history(4);
    for (int i = 1; i <= 6; ++i) {
        history.add(static_cast<float>(i));
    }

    REQUIRE(history.size() == 4);
    CHECK(history.at(0) == 3.0f);
    CHECK(history.at(1) == 4.0f);
    CHECK(history.at(2) == 5.0f);
    CHECK(history.at(3) == 6.0f);

    const TimingSummary summary = history.summary();
    CHECK(summary.latestMs == 6.0f);
    CHECK(summary.averageMs == 4.5f);
    CHECK(summary.minMs == 3.0f);
    CHECK(summary.maxMs == 6.0f);

    // The raw view handed to ImGui::PlotLines starts at offset() and wraps.
    for (std::size_t i = 0; i < history.size(); ++i) {
        CHECK(history.data()[(history.offset() + i) % history.size()] == history.at(i));
    }

    history.clear();
    CHECK(history.empty());
    history.add(1.5f);
    CHECK(history.offset() == 0);
    CHECK(history.at(0) == 1.5f);
}
