// Section update latency (E-01): from a Changed's publish time until the GPU table first takes a mesh or empty
// result for that section, in the same load, made for the change's stamp or a later one.

#include "client/client_world.h"
#include "client/mesh_scheduler.h"
#include "client/section_latency.h"
#include "core/job_system.h"
#include "render/chunk_mesher.h"
#include "render/mesh_resources.h"
#include "render/upload_queue.h"

#include "../data/data_test_support.h"
#include "../render/gpu_table_support.h"
#include "../world/test_world.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <optional>
#include <stdexcept>
#include <vector>

using namespace aurora;
using namespace std::chrono_literals;
using client::SectionLatencyTracker;
using Clock = SectionLatencyTracker::Clock;
using world::BlockPos;

namespace {

const Clock::time_point kStart = Clock::time_point{} + 1h;

struct Fixture {
    test::TestWorld server;
    core::JobSystem jobs{2};
    std::shared_ptr<const render::MeshResources> resources =
        render::buildMeshResources(*server.registry, *render::assignTextureLayers({}));
    std::optional<client::SectionKey> failOnce;
    client::ClientWorld world{1};
    client::MeshScheduler scheduler{
        jobs,
        [this, mesher = render::makeChunkMesher(resources)](const client::MeshInput& input) {
            // failOnce is set and cleared only while no job runs.
            if (failOnce && failOnce->pos == input.center().pos() && failOnce->section == input.section) {
                throw std::runtime_error("test mesher failure");
            }
            return mesher(input);
        },
        16};
    render::UploadQueue queue;
    test::GpuTable gpu;
    SectionLatencyTracker tracker{8};
    Clock::time_point now = kStart;

    Fixture()
    {
        receive(server.load({0, 0}, 2));
        settle();
    }

    // The server publishes its changes at `now`; the client applies them.
    void publish()
    {
        std::vector<world::ChunkUpdate> updates = server.publish();
        for (world::ChunkUpdate& update : updates) {
            update.publishedAt = now;
        }
        receive(updates);
    }
    void receive(const std::vector<world::ChunkUpdate>& updates)
    {
        for (const world::ChunkUpdate& update : updates) {
            if (world.apply(update)) { // As the app does: only what the world took starts samples.
                tracker.onApplied(update, world);
            }
        }
    }

    // One client frame at `now`, in the app's order.
    void frame()
    {
        scheduler.update(world, {0, 0}, 7);
        for (const client::MeshKey& key : scheduler.takeFailures()) {
            tracker.onMeshFailed(key);
        }
        queue.push(scheduler.takeReady());
        gpu.taken.clear();
        gpu.update(world, queue);
        for (const client::MeshKey& key : gpu.taken) {
            tracker.onGpuTaken(key, now);
        }
        tracker.update(world);
    }

    // Frames until everything is meshed and taken, 10 ms apart.
    void settle()
    {
        for (int round = 0; round < 50; ++round) {
            jobs.waitIdle();
            frame();
            now += 10ms;
            if (scheduler.isSettled() && queue.size() == 0) {
                jobs.waitIdle();
                frame();
                return;
            }
        }
        FAIL("the pipeline did not settle");
    }
};

} // namespace

TEST_CASE("One change completes when its section's new mesh is taken", "[client][latency]")
{
    Fixture fixture;
    CHECK(fixture.tracker.stats().pending == 0);
    const auto published = fixture.now;
    REQUIRE(fixture.server.world.setBlock({3, 70, 3}, fixture.server.state("aurora:stone")));
    fixture.publish();
    CHECK(fixture.tracker.stats().pending == 1);
    fixture.settle();
    const client::SectionLatencyStats stats = fixture.tracker.stats();
    CHECK(stats.completed == 1);
    CHECK(stats.pending == 0);
    const std::vector<SectionLatencyTracker::Completion> completions = fixture.tracker.takeCompletions();
    REQUIRE(completions.size() == 1);
    CHECK(completions[0].milliseconds > 0.0);
    CHECK(completions[0].milliseconds <= std::chrono::duration<double, std::milli>(fixture.now - published).count());
    CHECK(completions[0].section == client::SectionKey{{0, 0}, world::sectionIndex(70)});
    CHECK(fixture.tracker.takeCompletions().empty()); // Taken once.
}

TEST_CASE("Changes merged into one newer mesh complete together", "[client][latency]")
{
    Fixture fixture;
    const data::BlockStateId stone = fixture.server.state("aurora:stone");
    const data::BlockStateId grass = fixture.server.state("aurora:grass_block");

    SECTION("Two changes of one section before the client meshes")
    {
        REQUIRE(fixture.server.world.setBlock({3, 70, 3}, stone));
        fixture.publish();
        fixture.now += 30ms;
        REQUIRE(fixture.server.world.setBlock({4, 70, 3}, stone));
        fixture.publish();
        CHECK(fixture.tracker.stats().pending == 2);
        fixture.settle();
        const std::vector<SectionLatencyTracker::Completion> completions = fixture.tracker.takeCompletions();
        REQUIRE(completions.size() == 2);
        // Both at the same take: the first waited 30 ms longer.
        CHECK(completions[0].takenAt == completions[1].takenAt);
        CHECK(completions[0].milliseconds - completions[1].milliseconds == Catch::Approx(30.0));
    }
    SECTION("A -> B -> A: the middle state is never meshed, both changes complete")
    {
        REQUIRE(fixture.server.world.setBlock({3, 63, 3}, stone)); // Grass -> stone.
        fixture.publish();
        fixture.now += 10ms;
        REQUIRE(fixture.server.world.setBlock({3, 63, 3}, grass)); // And back.
        fixture.publish();
        fixture.settle();
        CHECK(fixture.tracker.stats().completed == 2);
        CHECK(fixture.world.snapshot({0, 0})->getBlock(3, 63, 3) == grass);
    }
}

TEST_CASE("Only a mesh at least as new as the change completes it", "[client][latency]")
{
    Fixture fixture;
    REQUIRE(fixture.server.world.setBlock({3, 70, 3}, fixture.server.state("aurora:stone")));
    fixture.publish();
    const client::SectionKey section{{0, 0}, world::sectionIndex(70)};
    const std::uint64_t stamp = *fixture.world.stamp(section);
    const std::uint64_t generation = fixture.world.snapshot({0, 0})->generation();
    fixture.tracker.onGpuTaken({section, generation, stamp - 1}, fixture.now); // Older inputs.
    fixture.tracker.onGpuTaken({{{0, 0}, section.section + 1}, generation, stamp}, fixture.now); // Another section.
    fixture.tracker.onGpuTaken({section, generation + 1, stamp}, fixture.now); // Another load.
    CHECK(fixture.tracker.stats().completed == 0);
    fixture.tracker.onGpuTaken({section, generation, stamp + 3}, fixture.now); // Newer: includes it.
    CHECK(fixture.tracker.stats().completed == 1);
}

TEST_CASE("A Changed the world ignored as a duplicate starts no sample", "[client][latency]")
{
    Fixture fixture;
    REQUIRE(fixture.server.world.setBlock({3, 70, 3}, fixture.server.state("aurora:stone")));
    std::vector<world::ChunkUpdate> updates = fixture.server.publish();
    for (world::ChunkUpdate& update : updates) {
        update.publishedAt = fixture.now;
    }

    SECTION("The same Changed twice before the GPU took the new mesh")
    {
        fixture.receive(updates);
        fixture.receive(updates); // The very same update object again: the world keeps what it has.
        CHECK(fixture.tracker.stats().pending == 1);
        fixture.settle();
        CHECK(fixture.tracker.stats().completed == 1);
        CHECK(fixture.tracker.stats().pending == 0);
    }
    SECTION("The same Changed again after the GPU took the new mesh")
    {
        fixture.receive(updates);
        fixture.settle();
        REQUIRE(fixture.tracker.stats().completed == 1);
        fixture.receive(updates); // Nothing changes in the world, so no mesh would ever complete a new sample.
        CHECK(fixture.tracker.stats().pending == 0);
        fixture.settle();
        CHECK(fixture.tracker.stats().completed == 1);
        CHECK(fixture.tracker.stats().pending == 0);
    }
}

TEST_CASE("Removing the last block completes with the empty result", "[client][latency]")
{
    Fixture fixture;
    const data::BlockStateId stone = fixture.server.state("aurora:stone");
    REQUIRE(fixture.server.world.setBlock({5, 100, 5}, stone));
    fixture.publish();
    fixture.settle();
    REQUIRE(fixture.tracker.stats().completed == 1);
    REQUIRE(fixture.server.world.setBlock({5, 100, 5}, data::kAirState));
    fixture.publish();
    fixture.settle();
    CHECK(fixture.tracker.stats().completed == 2);
    CHECK(fixture.gpu.removes > 0);
}

TEST_CASE("Unloads cancel and failures fail samples and the record overflows", "[client][latency]")
{
    const test::QuietLog quiet; // The failure is logged.
    Fixture fixture;
    const data::BlockStateId stone = fixture.server.state("aurora:stone");

    SECTION("The chunk unloads before its mesh is taken")
    {
        REQUIRE(fixture.server.world.setBlock({3, 70, 3}, stone));
        fixture.publish();
        fixture.receive(fixture.server.load({20, 0}, 0));
        fixture.frame();
        const client::SectionLatencyStats stats = fixture.tracker.stats();
        CHECK(stats.canceled == 1);
        CHECK(stats.pending == 0);
        CHECK(stats.completed == 0);
    }
    SECTION("Meshing the new inputs fails")
    {
        fixture.failOnce = client::SectionKey{{0, 0}, world::sectionIndex(70)};
        REQUIRE(fixture.server.world.setBlock({3, 70, 3}, stone));
        fixture.publish();
        for (int i = 0; i < 5; ++i) {
            fixture.frame();
            fixture.jobs.waitIdle();
        }
        fixture.failOnce.reset();
        const client::SectionLatencyStats stats = fixture.tracker.stats();
        CHECK(stats.failed == 1);
        CHECK(stats.completed == 0);
    }
    SECTION("More waiting than the record holds")
    {
        const data::BlockStateId log = fixture.server.state("aurora:oak_log");
        for (std::int32_t i = 0; i < 10; ++i) { // Ten sections in one tick; the record holds 8.
            REQUIRE(fixture.server.world.setBlock({1, -60 + 16 * i, 1}, log));
        }
        fixture.publish();
        const client::SectionLatencyStats stats = fixture.tracker.stats();
        CHECK(stats.pending == 8);
        CHECK(stats.overflowed == 2);
    }
}

TEST_CASE("Latency percentiles use the nearest rank", "[client][latency]")
{
    // Feed known latencies through a section of a held, eligible chunk.
    client::ClientWorld world(1);
    test::TestWorld server;
    for (const world::ChunkUpdate& update : server.load({0, 0}, 2)) {
        world.apply(update);
    }
    SectionLatencyTracker tracker(64, 4);
    SectionLatencyTracker narrow(64, 4, 10); // A window of the newest 10.
    for (int i = 1; i <= 20; ++i) {
        REQUIRE(server.world.setBlock({2, 70, 2}, i % 2 == 0 ? data::kAirState : server.state("aurora:stone")));
        std::vector<world::ChunkUpdate> updates = server.publish();
        updates[0].publishedAt = kStart;
        REQUIRE(world.apply(updates[0]));
        tracker.onApplied(updates[0], world);
        narrow.onApplied(updates[0], world);
        const client::SectionKey section{{0, 0}, world::sectionIndex(70)};
        const client::MeshKey key{section, world.snapshot({0, 0})->generation(), *world.stamp(section)};
        tracker.onGpuTaken(key, kStart + std::chrono::milliseconds(i * 10));
        narrow.onGpuTaken(key, kStart + std::chrono::milliseconds(i * 10));
    }
    const client::SectionLatencyStats stats = tracker.stats();
    CHECK(stats.completed == 20);
    CHECK(stats.windowCount == 20); // All of them fit the default window.
    CHECK(stats.windowEvicted == 0);
    CHECK(stats.windowP50Ms == Catch::Approx(100.0)); // 10, 20, ..., 200 ms: rank 10.
    CHECK(stats.windowP95Ms == Catch::Approx(190.0)); // Rank 19.
    CHECK(stats.windowMaxMs == Catch::Approx(200.0));
    CHECK(stats.runMaxMs == Catch::Approx(200.0));
    CHECK(stats.recentCount == 4);
    CHECK(stats.recentMaxMs == Catch::Approx(200.0)); // The last four: 170..200.
    CHECK(stats.recentMeanMs == Catch::Approx(185.0));

    // Only the newest 10 (110..200 ms) are in the narrow window: its percentiles are theirs, not the run's.
    const client::SectionLatencyStats window = narrow.stats();
    CHECK(window.completed == 20);
    CHECK(window.windowCount == 10);
    CHECK(window.windowEvicted == 10);
    CHECK(window.windowP50Ms == Catch::Approx(150.0)); // Rank 5 of 110..200.
    CHECK(window.windowP95Ms == Catch::Approx(200.0)); // Rank 10.
    CHECK(window.runMaxMs == Catch::Approx(200.0));
}

TEST_CASE("The latency record stays bounded however many changes complete", "[client][latency]")
{
    // Far more completions than any of the bounds: 8 pending, a window of 16 (4 recent), 32 kept for taking.
    client::ClientWorld world(1);
    test::TestWorld server;
    for (const world::ChunkUpdate& update : server.load({0, 0}, 2)) {
        world.apply(update);
    }
    SectionLatencyTracker tracker(8, 4, 16, 32);
    const client::SectionKey section{{0, 0}, world::sectionIndex(70)};
    constexpr int kChanges = 3000;
    double largest = 0.0;
    for (int i = 1; i <= kChanges; ++i) {
        REQUIRE(server.world.setBlock({2, 70, 2}, i % 2 == 0 ? data::kAirState : server.state("aurora:stone")));
        std::vector<world::ChunkUpdate> updates = server.publish();
        updates[0].publishedAt = kStart;
        REQUIRE(world.apply(updates[0]));
        tracker.onApplied(updates[0], world);
        // Latencies 1..97 ms in a fixed pattern, with the largest (99 ms) once in the middle.
        const int milliseconds = i == kChanges / 2 ? 99 : 1 + (i * 37) % 97;
        largest = std::max(largest, static_cast<double>(milliseconds));
        tracker.onGpuTaken({section, world.snapshot({0, 0})->generation(), *world.stamp(section)},
                           kStart + std::chrono::milliseconds(milliseconds));
        CHECK(tracker.stats().pending == 0);
    }
    const client::SectionLatencyStats stats = tracker.stats();
    CHECK(stats.completed == kChanges);
    CHECK(stats.windowCount == 16);
    CHECK(stats.windowEvicted == kChanges - 16);
    CHECK(stats.recentCount == 4);
    CHECK(stats.runMaxMs == Catch::Approx(largest)); // Kept as they came, long after it left the window.
    CHECK(stats.windowMaxMs < stats.runMaxMs);
    CHECK(stats.overflowed == 0); // Pending samples were never dropped; that count is separate.
    // Nobody took the completions: only the newest 32 are kept, and the rest are counted as lost to the record.
    CHECK(stats.unreportedDropped == kChanges - 32);
    const std::vector<SectionLatencyTracker::Completion> kept = tracker.takeCompletions();
    REQUIRE(kept.size() == 32);
    CHECK(kept.back().milliseconds == Catch::Approx(1 + (kChanges * 37) % 97));
    CHECK(tracker.takeCompletions().empty());
}
