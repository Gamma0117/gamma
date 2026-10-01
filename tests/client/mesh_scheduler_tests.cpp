#include "client/client_world.h"
#include "client/mesh_scheduler.h"
#include "core/job_system.h"

#include "../data/data_test_support.h"
#include "client_test_support.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace std::chrono_literals;
using namespace aurora::client;
using aurora::core::JobSystem;
using aurora::test::loaded;
using aurora::test::loadSquare;
using aurora::test::unloaded;
using aurora::world::ChunkPos;
using aurora::world::ChunkPosHash;

namespace {

// Shared by a test and its meshing jobs: a gate to hold jobs, call counts and failure injection.
struct MeshProbe {
    std::mutex mutex;
    std::condition_variable changed;
    bool open = true;
    bool emptyMeshes = false;
    std::unordered_set<ChunkPos, ChunkPosHash> throwAt;
    std::unordered_map<ChunkPos, int, ChunkPosHash> calls;
    std::vector<ChunkPos> order; // Positions in the order jobs started.
    int started = 0;
    int finished = 0;

    void setOpen(bool value)
    {
        {
            std::lock_guard lock(mutex);
            open = value;
        }
        changed.notify_all();
    }

    bool waitForStarted(int count)
    {
        std::unique_lock lock(mutex);
        return changed.wait_for(lock, 10s, [&] { return started >= count; });
    }

    int callsAt(ChunkPos pos)
    {
        std::lock_guard lock(mutex);
        return calls.contains(pos) ? calls.at(pos) : 0;
    }
};

// One quad (4 vertices, 6 indices) per section, or nothing.
MeshFunction makeProbeMesher(std::shared_ptr<MeshProbe> probe)
{
    return [probe = std::move(probe)](const MeshInput& input) {
        const ChunkPos pos = input.center().pos();
        std::unique_lock lock(probe->mutex);
        ++probe->calls[pos];
        probe->order.push_back(pos);
        ++probe->started;
        probe->changed.notify_all();
        probe->changed.wait(lock, [&] { return probe->open; });
        ++probe->finished;
        probe->changed.notify_all();
        if (probe->throwAt.contains(pos)) {
            throw std::runtime_error("test mesher failure");
        }
        MeshData mesh;
        if (!probe->emptyMeshes) {
            mesh.vertexWords.assign(8, 0);
            mesh.indices = {0, 1, 2, 0, 2, 3};
        }
        return mesh;
    };
}

struct Fixture {
    std::shared_ptr<MeshProbe> probe = std::make_shared<MeshProbe>();
    JobSystem jobs{2};
    ClientWorld world{1};

    Fixture() { loadSquare(world, {0, 0}, 2); } // 9 eligible chunks, one section each.

    // Finishes every job and collects until nothing changes (at most a few rounds).
    void settle(MeshScheduler& scheduler)
    {
        for (int round = 0; round < 20 && !scheduler.isSettled(); ++round) {
            scheduler.update(world, {0, 0}, 7);
            jobs.waitIdle();
            scheduler.update(world, {0, 0}, 7);
            scheduler.takeReady();
        }
    }
};

} // namespace

TEST_CASE("Every eligible section is meshed once and handed over", "[client][mesh]")
{
    Fixture fixture;
    MeshScheduler scheduler(fixture.jobs, makeProbeMesher(fixture.probe), 4);
    std::vector<ReadyMesh> ready;
    for (int round = 0; round < 20 && ready.size() < 9; ++round) {
        scheduler.update(fixture.world, {0, 0}, 7);
        fixture.jobs.waitIdle();
        scheduler.update(fixture.world, {0, 0}, 7);
        for (ReadyMesh& mesh : scheduler.takeReady()) {
            CHECK(fixture.world.isCurrent(mesh.key));
            CHECK(mesh.key.section.section == 7); // Only the non-air section.
            ready.push_back(std::move(mesh));
        }
    }
    CHECK(ready.size() == 9);
    CHECK(scheduler.isSettled());
    CHECK(scheduler.stats().meshed == 9);
    CHECK(fixture.probe->started == 9);

    scheduler.update(fixture.world, {0, 0}, 7); // Nothing changed: nothing new.
    fixture.jobs.waitIdle();
    CHECK(fixture.probe->started == 9);
}

TEST_CASE("Jobs are limited and the nearest sections go first", "[client][mesh]")
{
    Fixture fixture;
    fixture.probe->setOpen(false);
    MeshScheduler scheduler(fixture.jobs, makeProbeMesher(fixture.probe), 2);
    scheduler.update(fixture.world, {0, 0}, 7);
    CHECK(scheduler.stats().inFlight == 2);
    CHECK(scheduler.stats().waiting == 7);
    REQUIRE(fixture.probe->waitForStarted(2));
    CHECK(fixture.probe->callsAt({0, 0}) == 1); // The camera's own chunk is among the first two.

    scheduler.update(fixture.world, {0, 0}, 7); // Still blocked: no more jobs.
    CHECK(scheduler.stats().inFlight == 2);
    fixture.probe->setOpen(true);
    fixture.settle(scheduler);
    CHECK(fixture.probe->started == 9);
}

TEST_CASE("Stale results are dropped and never handed over", "[client][mesh]")
{
    Fixture fixture;
    fixture.probe->setOpen(false);
    MeshScheduler scheduler(fixture.jobs, makeProbeMesher(fixture.probe), 9);
    scheduler.update(fixture.world, {0, 0}, 7);
    REQUIRE(fixture.probe->waitForStarted(2));

    SECTION("A neighbour leaves")
    {
        // (2, 2) leaving makes (1, 1) ineligible; its job is already queued.
        fixture.world.apply(unloaded({2, 2}, fixture.world.snapshot({2, 2})->generation()));
    }
    SECTION("The chunk is unloaded and loaded again")
    {
        fixture.world.apply(unloaded({1, 1}, fixture.world.snapshot({1, 1})->generation()));
        fixture.world.apply(loaded({1, 1}, 900));
    }
    SECTION("The center moves away and back")
    {
        fixture.world.setCenter({5, 5});
        fixture.world.setCenter({0, 0});
    }
    scheduler.update(fixture.world, {0, 0}, 7);
    fixture.probe->setOpen(true);
    fixture.jobs.waitIdle();
    scheduler.update(fixture.world, {0, 0}, 7);
    for (const ReadyMesh& mesh : scheduler.takeReady()) {
        INFO("chunk " << mesh.key.section.pos.x << ", " << mesh.key.section.pos.z);
        CHECK(fixture.world.isCurrent(mesh.key)); // Nothing made for the old inputs gets through.
    }
    fixture.settle(scheduler);
    CHECK(scheduler.stats().inFlight == 0);
    CHECK(scheduler.stats().failed == 0);
}

TEST_CASE("Stale jobs keep their slots until they finish", "[client][mesh]")
{
    Fixture fixture;
    fixture.probe->setOpen(false);
    MeshScheduler scheduler(fixture.jobs, makeProbeMesher(fixture.probe), 2);
    scheduler.update(fixture.world, {0, 0}, 7);
    REQUIRE(fixture.probe->waitForStarted(2));

    for (int move = 0; move < 5; ++move) {
        // Every move makes everything stale and then waiting again with new stamps.
        fixture.world.setCenter({move % 2 == 0 ? 10 : 0, 0});
        scheduler.update(fixture.world, {0, 0}, 7);
        CHECK(scheduler.stats().inFlight <= 2);
        // What really ran out of the scheduler's sight: jobs queued or running in the job system.
        CHECK(fixture.jobs.pendingJobs() <= 2);
    }
    fixture.world.setCenter({0, 0});
    scheduler.update(fixture.world, {0, 0}, 7);
    CHECK(scheduler.stats().inFlight == 2);
    CHECK(fixture.jobs.pendingJobs() == 2); // No job was added on top of the two blocked ones.
    fixture.probe->setOpen(true);
    fixture.settle(scheduler);
    CHECK(scheduler.isSettled());
    CHECK(scheduler.stats().meshed == 9);
}

TEST_CASE("A ready mesh that loses eligibility before it is taken is dropped", "[client][mesh]")
{
    Fixture fixture;
    MeshScheduler scheduler(fixture.jobs, makeProbeMesher(fixture.probe), 9);
    scheduler.update(fixture.world, {0, 0}, 7);
    fixture.jobs.waitIdle();
    scheduler.update(fixture.world, {0, 0}, 7);
    REQUIRE(scheduler.stats().ready == 9);

    fixture.world.setCenter({4, 0}); // Every mesh's chunk is out of range now.
    scheduler.update(fixture.world, {4, 0}, 7);
    CHECK(scheduler.takeReady().empty());
    CHECK(scheduler.stats().ready == 0);
}

TEST_CASE("A failed mesh is not retried until its inputs change", "[client][mesh]")
{
    const aurora::test::QuietLog quiet; // The failure is logged on purpose.
    Fixture fixture;
    fixture.probe->throwAt = {ChunkPos{0, 0}};
    MeshScheduler scheduler(fixture.jobs, makeProbeMesher(fixture.probe), 9);
    for (int round = 0; round < 5; ++round) {
        scheduler.update(fixture.world, {0, 0}, 7);
        fixture.jobs.waitIdle();
        scheduler.takeReady();
    }
    CHECK(fixture.probe->callsAt({0, 0}) == 1);
    CHECK(scheduler.stats().failed == 1);
    CHECK(scheduler.stats().meshed == 8);
    CHECK_FALSE(scheduler.isSettled());

    {
        std::lock_guard lock(fixture.probe->mutex);
        fixture.probe->throwAt.clear();
    }
    fixture.world.apply(loaded({0, 0}, 700)); // New inputs: one more attempt.
    fixture.settle(scheduler);
    CHECK(fixture.probe->callsAt({0, 0}) == 2);
    CHECK(scheduler.stats().failed == 0);
    CHECK(scheduler.isSettled());
}

TEST_CASE("Jobs the job system refuses fail without waiting and are not retried", "[client][mesh]")
{
    const aurora::test::QuietLog quiet;
    Fixture fixture;
    fixture.jobs.shutdown();
    MeshScheduler scheduler(fixture.jobs, makeProbeMesher(fixture.probe), 4);
    for (int round = 0; round < 3; ++round) {
        scheduler.update(fixture.world, {0, 0}, 7);
    }
    CHECK(scheduler.stats().failed == 9);
    CHECK(scheduler.stats().inFlight == 0);
    CHECK(fixture.probe->started == 0);
}

TEST_CASE("Empty meshes are finished without anything to upload", "[client][mesh]")
{
    Fixture fixture;
    fixture.probe->emptyMeshes = true;
    MeshScheduler scheduler(fixture.jobs, makeProbeMesher(fixture.probe), 9);
    scheduler.update(fixture.world, {0, 0}, 7);
    fixture.jobs.waitIdle();
    scheduler.update(fixture.world, {0, 0}, 7);
    CHECK(scheduler.takeReady().empty());
    CHECK(scheduler.stats().empty == 9);
    CHECK(scheduler.isSettled());
}

TEST_CASE("Destroying the scheduler and the world with jobs in flight is safe", "[client][mesh]")
{
    auto probe = std::make_shared<MeshProbe>();
    JobSystem jobs(2);
    {
        ClientWorld world(1);
        loadSquare(world, {0, 0}, 2);
        MeshScheduler scheduler(jobs, makeProbeMesher(probe), 9);
        probe->setOpen(false);
        scheduler.update(world, {0, 0}, 7);
        REQUIRE(probe->waitForStarted(2));
    }
    // Both are gone; the jobs hold their own snapshots. ASan and TSan runs check the rest.
    probe->setOpen(true);
    jobs.waitIdle();
    CHECK(probe->finished == 9);
}
