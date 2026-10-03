// Block changes from a real server World through the client's mesh pipeline with the real mesher: ClientWorld,
// MeshScheduler, UploadQueue and ChunkRenderer's table rules (isDrawable, chooseMeshUpdate), without GL. The GPU
// table must always end equal to meshing every drawn section from scratch.

#include "client/client_world.h"
#include "client/mesh_scheduler.h"
#include "core/constants.h"
#include "core/job_system.h"
#include "render/chunk_mesher.h"
#include "render/mesh_resources.h"
#include "render/upload_queue.h"

#include "../world/test_world.h"
#include "gpu_table_support.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace aurora;
using namespace std::chrono_literals;
using client::MeshData;
using client::MeshKey;
using client::SectionKey;
using world::BlockPos;
using world::ChunkPos;

namespace {

// Wraps the real mesher: counts calls per section, can hold jobs at a gate and fail one section once.
struct MesherProbe {
    std::mutex mutex;
    std::condition_variable changed;
    bool open = true;
    std::optional<SectionKey> failOnce;
    std::unordered_map<SectionKey, int, client::SectionKeyHash> calls;
    int started = 0;

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

    void failOnceAt(const SectionKey& key)
    {
        std::lock_guard lock(mutex);
        failOnce = key;
    }

    int startedCount()
    {
        std::lock_guard lock(mutex);
        return started;
    }

    int callsAt(const SectionKey& key)
    {
        std::lock_guard lock(mutex);
        return calls.contains(key) ? calls.at(key) : 0;
    }
};

client::MeshFunction probed(std::shared_ptr<MesherProbe> probe, client::MeshFunction real)
{
    return [probe = std::move(probe), real = std::move(real)](const client::MeshInput& input) {
        const SectionKey key{input.center().pos(), input.section};
        {
            std::unique_lock lock(probe->mutex);
            ++probe->calls[key];
            ++probe->started;
            probe->changed.notify_all();
            probe->changed.wait(lock, [&] { return probe->open; });
            if (probe->failOnce && *probe->failOnce == key) {
                probe->failOnce.reset();
                throw std::runtime_error("test mesher failure");
            }
        }
        return real(input);
    };
}

std::shared_ptr<const render::MeshResources> resourcesFor(const data::BlockRegistry& registry)
{
    return render::buildMeshResources(registry, *render::assignTextureLayers({}));
}

struct Pipeline {
    static constexpr std::size_t kMaxInFlight = 4;

    test::TestWorld server;
    std::shared_ptr<MesherProbe> probe = std::make_shared<MesherProbe>();
    std::shared_ptr<const render::MeshResources> resources = resourcesFor(*server.registry);
    core::JobSystem jobs{2};
    client::ClientWorld world{1}; // The 3 x 3 chunks around (0, 0) are drawn.
    client::MeshScheduler scheduler{jobs, probed(probe, render::makeChunkMesher(resources)), kMaxInFlight};
    render::UploadQueue queue;
    test::GpuTable gpu;

    Pipeline() { receive(server.load({0, 0}, 2)); }
    // A failed check must not leave jobs waiting at the gate: the job system joins them when it goes.
    ~Pipeline() { probe->setOpen(true); }

    void receive(const std::vector<world::ChunkUpdate>& updates)
    {
        for (const world::ChunkUpdate& update : updates) {
            world.apply(update);
        }
    }

    // One client frame: the scheduler, the renderer's take of ready meshes, the GPU table.
    void frame()
    {
        scheduler.update(world, {0, 0}, 7);
        CHECK(scheduler.stats().inFlight <= kMaxInFlight);
        queue.push(scheduler.takeReady());
        gpu.update(world, queue);
    }

    void settle()
    {
        for (int round = 0; round < 50; ++round) {
            frame();
            jobs.waitIdle();
            frame();
            if (scheduler.isSettled() && queue.size() == 0) {
                return;
            }
        }
        FAIL("the pipeline did not settle");
    }

    // The server's changes so far, published and received.
    void publish() { receive(server.publish()); }

    // Every drawn section's GPU mesh equals meshing it now from the client's snapshots; no other section holds one.
    void checkMatchesFullMesh() { test::checkMatchesFullMesh(world, gpu, *resources); }
};

// The cells of a section at its corners, edges, faces and middle: x, y, z each at the low end, middle or high end.
std::vector<BlockPos> cellsOfSection(std::int32_t chunkX, std::int32_t chunkZ, std::int32_t section)
{
    std::vector<BlockPos> cells;
    const std::int32_t baseY = world::sectionBottomY(section);
    for (const std::int32_t dy : {0, 7, 15}) {
        for (const std::int32_t dz : {0, 8, 15}) {
            for (const std::int32_t dx : {0, 7, 15}) {
                cells.push_back({world::chunkOrigin(chunkX) + dx, baseY + dy, world::chunkOrigin(chunkZ) + dz});
            }
        }
    }
    return cells;
}

} // namespace

TEST_CASE("Remeshing the changed sections and their neighbours matches meshing everything again",
          "[render][pipeline][changes]")
{
    Pipeline pipeline;
    pipeline.settle();
    pipeline.checkMatchesFullMesh();
    const data::BlockStateId stone = pipeline.server.state("aurora:stone");

    // Section 7 of (0, 0) holds the surface (y 48..63, grass at 63): dig each of its 27 cells out, one per tick.
    // Section 8 of (-1, -1) is open air above the surface (y 64..79): build each of its 27 cells, across the
    // negative chunk borders, one per tick. Then undo everything in reverse.
    std::vector<std::pair<BlockPos, data::BlockStateId>> edits;
    for (const BlockPos& cell : cellsOfSection(0, 0, 7)) {
        edits.push_back({cell, data::kAirState});
    }
    for (const BlockPos& cell : cellsOfSection(-1, -1, 8)) {
        edits.push_back({cell, stone});
    }
    std::vector<std::pair<BlockPos, data::BlockStateId>> undo;
    for (const auto& [pos, state] : edits) {
        undo.insert(undo.begin(), {pos, *pipeline.server.world.getBlock(pos)});
    }
    edits.insert(edits.end(), undo.begin(), undo.end());

    for (const auto& [pos, state] : edits) {
        INFO("edit at (" << pos.x << ", " << pos.y << ", " << pos.z << ")");
        REQUIRE(pipeline.server.world.setBlock(pos, state));
        pipeline.publish();
        pipeline.settle();
        pipeline.checkMatchesFullMesh();
    }

    // Several changes in one tick, in different chunks and sections.
    REQUIRE(pipeline.server.world.setBlock({-1, 63, 0}, data::kAirState));
    REQUIRE(pipeline.server.world.setBlock({0, 64, -1}, stone));
    REQUIRE(pipeline.server.world.setBlock({15, 47, 15}, data::kAirState));
    pipeline.publish();
    pipeline.settle();
    pipeline.checkMatchesFullMesh();
}

TEST_CASE("Removing the last block of a section removes its GPU mesh without a meshing job",
          "[render][pipeline][changes]")
{
    Pipeline pipeline;
    pipeline.settle();
    const data::BlockStateId stone = pipeline.server.state("aurora:stone");
    const SectionKey high{{0, 0}, world::sectionIndex(100)}; // All air until the block below goes in.
    CHECK_FALSE(pipeline.gpu.meshes.contains(high));

    for (int round = 0; round < 2; ++round) {
        INFO("round " << round);
        REQUIRE(pipeline.server.world.setBlock({5, 100, 5}, stone));
        pipeline.publish();
        pipeline.settle();
        REQUIRE(pipeline.gpu.meshes.contains(high));
        pipeline.checkMatchesFullMesh();

        const int callsBefore = pipeline.probe->callsAt(high);
        const int removesBefore = pipeline.gpu.removes;
        REQUIRE(pipeline.server.world.setBlock({5, 100, 5}, data::kAirState));
        pipeline.publish();
        REQUIRE(pipeline.world.snapshot({0, 0})->section(high.section) == nullptr);
        pipeline.settle();
        CHECK_FALSE(pipeline.gpu.meshes.contains(high));
        CHECK(pipeline.gpu.removes > removesBefore);
        CHECK(pipeline.probe->callsAt(high) == callsBefore); // The empty result came without a job.
        pipeline.checkMatchesFullMesh();
    }
}

TEST_CASE("Changes while meshes are in flight or ready or queued never upload stale meshes",
          "[render][pipeline][changes]")
{
    Pipeline pipeline;
    pipeline.settle();
    const data::BlockStateId stone = pipeline.server.state("aurora:stone");
    const data::BlockStateId log = pipeline.server.state("aurora:oak_log");
    const BlockPos cell{3, 64, 3}; // Section 8 of (0, 0).

    SECTION("Two more changes while the first job is held in flight")
    {
        pipeline.probe->setOpen(false);
        REQUIRE(pipeline.server.world.setBlock(cell, stone));
        pipeline.publish();
        const int before = pipeline.probe->startedCount();
        pipeline.frame();
        REQUIRE(pipeline.probe->waitForStarted(before + 1));
        REQUIRE(pipeline.server.world.setBlock(cell, log));
        pipeline.publish();
        pipeline.frame();
        REQUIRE(pipeline.server.world.setBlock({4, 64, 3}, stone));
        pipeline.publish();
        pipeline.frame();
        CHECK(pipeline.scheduler.stats().inFlight <= Pipeline::kMaxInFlight);
        pipeline.probe->setOpen(true);
        pipeline.settle();
        pipeline.checkMatchesFullMesh();
    }
    SECTION("A change after the result is ready but before the renderer takes it")
    {
        REQUIRE(pipeline.server.world.setBlock(cell, stone));
        pipeline.publish();
        pipeline.scheduler.update(pipeline.world, {0, 0}, 7);
        pipeline.jobs.waitIdle();
        pipeline.scheduler.update(pipeline.world, {0, 0}, 7); // Collected: ready, not taken.
        REQUIRE(pipeline.scheduler.stats().ready > 0);
        REQUIRE(pipeline.server.world.setBlock(cell, log));
        pipeline.publish();
        pipeline.settle();
        pipeline.checkMatchesFullMesh();
        CHECK(pipeline.scheduler.stats().stale > 0); // The ready result was dropped when its section changed.
    }
    SECTION("A change after the renderer queued the mesh but before it uploads")
    {
        REQUIRE(pipeline.server.world.setBlock(cell, stone));
        pipeline.publish();
        pipeline.scheduler.update(pipeline.world, {0, 0}, 7);
        pipeline.jobs.waitIdle();
        pipeline.scheduler.update(pipeline.world, {0, 0}, 7);
        pipeline.queue.push(pipeline.scheduler.takeReady()); // Queued, not uploaded.
        REQUIRE(pipeline.queue.size() > 0);
        REQUIRE(pipeline.server.world.setBlock(cell, data::kAirState));
        pipeline.publish();
        pipeline.settle(); // GpuTable::update checks that nothing stale is uploaded.
        pipeline.checkMatchesFullMesh();
        CHECK(pipeline.queue.dropped() > 0); // The queued mesh went stale and was dropped, not uploaded.
    }
    SECTION("The chunk unloads and reloads while its change is in flight")
    {
        pipeline.probe->setOpen(false);
        REQUIRE(pipeline.server.world.setBlock(cell, stone));
        pipeline.publish();
        pipeline.frame();
        pipeline.receive(pipeline.server.load({30, 0}, 0)); // Everything around (0, 0) unloads.
        pipeline.frame();
        CHECK(pipeline.gpu.meshes.empty());
        pipeline.probe->setOpen(true);
        pipeline.receive(pipeline.server.load({0, 0}, 2));
        pipeline.settle();
        pipeline.checkMatchesFullMesh();
        CHECK(pipeline.world.snapshot({0, 0})->getBlock(3, 64, 3) == data::kAirState); // The edit was not kept.
    }
    SECTION("A failed job is not settled and the next change meshes the section again")
    {
        const test::QuietLog quiet; // The failure is logged on purpose.
        const SectionKey key{{0, 0}, world::sectionIndex(cell.y)};
        pipeline.probe->failOnceAt(key);
        REQUIRE(pipeline.server.world.setBlock(cell, stone));
        pipeline.publish();
        for (int round = 0; round < 5; ++round) {
            pipeline.frame();
            pipeline.jobs.waitIdle();
        }
        CHECK(pipeline.scheduler.stats().failed == 1);
        CHECK_FALSE(pipeline.scheduler.isSettled());
        REQUIRE(pipeline.server.world.setBlock(cell, log));
        pipeline.publish();
        pipeline.settle();
        CHECK(pipeline.scheduler.stats().failed == 0);
        pipeline.checkMatchesFullMesh();
    }
}
