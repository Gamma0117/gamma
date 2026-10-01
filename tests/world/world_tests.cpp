#include "core/job_system.h"
#include "core/log.h"
#include "data/flat_preset.h"
#include "world/chunk.h"
#include "world/flat_generator.h"
#include "world/world.h"

#include "../data/data_test_support.h"
#include "world_test_support.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace std::chrono_literals;
using namespace aurora::world;
using aurora::core::JobSystem;
using aurora::test::stateOf;

namespace {

// Shared between a test and its generator jobs. Jobs wait at the gate while it is closed, so a test can hold
// chunks in the pending state; calls are counted per position.
struct GeneratorProbe {
    std::mutex mutex;
    std::condition_variable changed;
    bool open = true;
    std::unordered_map<ChunkPos, int, ChunkPosHash> calls;
    int totalCalls = 0;
    int finished = 0;
    std::unordered_set<ChunkPos, ChunkPosHash> throwAt;
    std::unordered_set<ChunkPos, ChunkPosHash> nullAt;
    std::unordered_set<ChunkPos, ChunkPosHash> wrongPosAt;

    void setOpen(bool value)
    {
        {
            std::lock_guard lock(mutex);
            open = value;
        }
        changed.notify_all();
    }

    // Waits (at most 10 s, a hang guard) until `count` jobs have entered the generator.
    bool waitForCalls(int count)
    {
        std::unique_lock lock(mutex);
        return changed.wait_for(lock, 10s, [&] { return totalCalls >= count; });
    }

    int callsAt(ChunkPos pos)
    {
        std::lock_guard lock(mutex);
        const auto found = calls.find(pos);
        return found == calls.end() ? 0 : found->second;
    }
};

// A flat generator behind the probe. Each chunk gets a marker at local (0, 319, 0): the state numbered by how
// many times its position has been generated (1 for the first call, 2 for the second).
ChunkGenerator makeProbeGenerator(std::shared_ptr<GeneratorProbe> probe,
                                  std::shared_ptr<const aurora::data::FlatPreset> preset)
{
    return [probe = std::move(probe), preset = std::move(preset)](ChunkPos pos) -> std::unique_ptr<Chunk> {
        int callNumber = 0;
        {
            std::unique_lock lock(probe->mutex);
            callNumber = ++probe->calls[pos];
            ++probe->totalCalls;
            probe->changed.notify_all();
            probe->changed.wait(lock, [&] { return probe->open; });
            if (probe->throwAt.contains(pos)) {
                ++probe->finished;
                throw std::runtime_error("test generator failure");
            }
            if (probe->nullAt.contains(pos)) {
                ++probe->finished;
                return nullptr;
            }
        }
        const bool wrongPos = [&] {
            std::lock_guard lock(probe->mutex);
            return probe->wrongPosAt.contains(pos);
        }();
        std::unique_ptr<Chunk> chunk = generateFlatChunk(*preset, wrongPos ? ChunkPos{pos.x + 1, pos.z} : pos);
        chunk->setBlock(0, 319, 0, static_cast<BlockStateId>(callNumber));

        std::lock_guard lock(probe->mutex);
        ++probe->finished;
        probe->changed.notify_all();
        return chunk;
    };
}

struct Fixture {
    std::shared_ptr<const aurora::data::BlockRegistry> registry = aurora::test::makeTestRegistry();
    std::shared_ptr<const aurora::data::FlatPreset> preset = aurora::test::makeStandardFlatPreset(*registry);
    std::shared_ptr<GeneratorProbe> probe = std::make_shared<GeneratorProbe>();

    ChunkGenerator generator() const { return makeProbeGenerator(probe, preset); }
};

// Lets every queued job finish, then takes the results.
void finishJobs(JobSystem& jobs, World& world)
{
    jobs.waitIdle();
    world.update();
}

} // namespace

TEST_CASE("World generates every chunk within the radius on the workers", "[world][manager]")
{
    Fixture fixture;
    JobSystem jobs(2);
    World world(fixture.registry, jobs, makeFlatGenerator(fixture.preset));

    world.ensureLoaded(ChunkPos{0, 0}, 2);
    CHECK(world.stats().pendingChunks + world.stats().loadedChunks == 25);
    finishJobs(jobs, world);
    CHECK(world.stats().loadedChunks == 25);
    CHECK(world.stats().pendingChunks == 0);
    CHECK(world.stats().failedChunks == 0);

    CHECK(world.chunk(ChunkPos{2, -2}) != nullptr);
    CHECK(world.chunk(ChunkPos{-2, 2}) != nullptr);
    CHECK(world.chunk(ChunkPos{3, 0}) == nullptr);

    const BlockStateId grass = stateOf(*fixture.registry, "aurora:grass_block");
    CHECK(world.getBlock(BlockPos{0, 63, 0}) == grass);
    CHECK(world.getBlock(BlockPos{-17, 59, 40}) == stateOf(*fixture.registry, "aurora:stone")); // Chunk (-2, 2).
    // Real air in a loaded chunk is air; everything the world cannot answer is nullopt.
    CHECK(world.getBlock(BlockPos{0, 64, 0}) == aurora::data::kAirState);
    CHECK(world.getBlock(BlockPos{0, 320, 0}) == std::nullopt);
    CHECK(world.getBlock(BlockPos{0, -65, 0}) == std::nullopt);
    CHECK(world.getBlock(BlockPos{48, 63, 0}) == std::nullopt); // Chunk 3 is not loaded.
}

TEST_CASE("World radius 8 is 289 chunks", "[world][manager]")
{
    Fixture fixture;
    JobSystem jobs(2);
    World world(fixture.registry, jobs, makeFlatGenerator(fixture.preset));
    world.ensureLoaded(ChunkPos{0, 0}, 8);
    finishJobs(jobs, world);
    CHECK(world.stats().loadedChunks == 289);
    CHECK(world.chunk(ChunkPos{8, 8}) != nullptr);
    CHECK(world.chunk(ChunkPos{9, 0}) == nullptr);
}

TEST_CASE("World keeps chunks up to one past the radius", "[world][manager]")
{
    Fixture fixture;
    JobSystem jobs(2);
    World world(fixture.registry, jobs, makeFlatGenerator(fixture.preset));
    world.ensureLoaded(ChunkPos{0, 0}, 2);
    finishJobs(jobs, world);

    // Center one to the east: column x = -2 is 3 away, inside the keep range (3); column 3 is new.
    world.ensureLoaded(ChunkPos{1, 0}, 2);
    finishJobs(jobs, world);
    CHECK(world.chunk(ChunkPos{-2, 0}) != nullptr);
    CHECK(world.chunk(ChunkPos{3, 0}) != nullptr);
    CHECK(world.stats().loadedChunks == 30);

    // Two to the east: x = -2 is 4 away and goes.
    world.ensureLoaded(ChunkPos{2, 0}, 2);
    CHECK(world.chunk(ChunkPos{-2, 0}) == nullptr);
    CHECK(world.chunk(ChunkPos{-1, 0}) != nullptr);
    finishJobs(jobs, world);
    CHECK(world.stats().loadedChunks == 30);
}

TEST_CASE("World setBlock and getBlock", "[world][manager]")
{
    Fixture fixture;
    JobSystem jobs(1);
    World world(fixture.registry, jobs, makeFlatGenerator(fixture.preset));
    world.ensureLoaded(ChunkPos{0, 0}, 1);
    finishJobs(jobs, world);

    const BlockStateId log = stateOf(*fixture.registry, "aurora:oak_log[axis=z]");
    CHECK(world.setBlock(BlockPos{-5, 100, 7}, log));
    CHECK(world.getBlock(BlockPos{-5, 100, 7}) == log);
    CHECK(world.chunk(ChunkPos{-1, 0})->height(11, 7) == 100);
    CHECK(world.setBlock(BlockPos{-5, 100, 7}, aurora::data::kAirState));
    CHECK(world.chunk(ChunkPos{-1, 0})->height(11, 7) == 63);

    CHECK(world.setBlock(BlockPos{0, 319, 0}, log));
    CHECK(world.setBlock(BlockPos{0, -64, 0}, log));
    CHECK_FALSE(world.setBlock(BlockPos{0, 320, 0}, log));
    CHECK_FALSE(world.setBlock(BlockPos{0, -65, 0}, log));
    CHECK_FALSE(world.setBlock(BlockPos{32, 70, 0}, log)); // Chunk 2 is not loaded.
    CHECK(world.getBlock(BlockPos{32, 70, 0}) == std::nullopt);

    const auto stateCount = static_cast<BlockStateId>(fixture.registry->stateCount());
    CHECK(world.setBlock(BlockPos{1, 70, 1}, static_cast<BlockStateId>(stateCount - 1)));
    CHECK_FALSE(world.setBlock(BlockPos{1, 71, 1}, stateCount)); // Not in this world's registry.
    CHECK_FALSE(world.setBlock(BlockPos{1, 71, 1}, 65535));
    CHECK(world.getBlock(BlockPos{1, 71, 1}) == aurora::data::kAirState);
}

TEST_CASE("Asking again before the jobs finish queues nothing new", "[world][manager]")
{
    Fixture fixture;
    JobSystem jobs(2);
    World world(fixture.registry, jobs, fixture.generator());

    fixture.probe->setOpen(false);
    for (int i = 0; i < 5; ++i) {
        world.ensureLoaded(ChunkPos{0, 0}, 1);
        world.update();
    }
    CHECK(world.stats().pendingChunks == 9);
    CHECK(jobs.pendingJobs() == 9); // Queued plus the two waiting at the gate.

    fixture.probe->setOpen(true);
    finishJobs(jobs, world);
    CHECK(world.stats().loadedChunks == 9);
    CHECK(fixture.probe->totalCalls == 9);
    for (std::int32_t x = -1; x <= 1; ++x) {
        for (std::int32_t z = -1; z <= 1; ++z) {
            CHECK(fixture.probe->callsAt(ChunkPos{x, z}) == 1);
        }
    }
}

TEST_CASE("A late result for a chunk that left the range is dropped", "[world][manager]")
{
    Fixture fixture;
    JobSystem jobs(2);
    World world(fixture.registry, jobs, fixture.generator());
    fixture.probe->setOpen(false);

    world.ensureLoaded(ChunkPos{0, 0}, 0);
    REQUIRE(fixture.probe->waitForCalls(1)); // (0, 0) is inside the generator.
    world.ensureLoaded(ChunkPos{5, 0}, 0);   // (0, 0) is now 5 away: dropped while its job runs.
    CHECK(world.stats().pendingChunks == 1);

    fixture.probe->setOpen(true);
    finishJobs(jobs, world);
    CHECK(fixture.probe->finished == 2);
    CHECK(world.chunk(ChunkPos{0, 0}) == nullptr);
    CHECK(world.chunk(ChunkPos{5, 0}) != nullptr);
    CHECK(world.stats().loadedChunks == 1);
    CHECK(world.stats().pendingChunks == 0);
}

TEST_CASE("An old request cannot overwrite a newer one for the same chunk", "[world][manager]")
{
    Fixture fixture;
    JobSystem jobs(2);
    World world(fixture.registry, jobs, fixture.generator());
    fixture.probe->setOpen(false);

    world.ensureLoaded(ChunkPos{0, 0}, 0); // Request 1 for (0, 0).
    REQUIRE(fixture.probe->waitForCalls(1));
    world.ensureLoaded(ChunkPos{5, 0}, 0); // Drops it; requests (5, 0).
    REQUIRE(fixture.probe->waitForCalls(2));
    world.ensureLoaded(ChunkPos{0, 0}, 0); // Drops (5, 0); request 2 for (0, 0).

    fixture.probe->setOpen(true);
    finishJobs(jobs, world);
    CHECK(fixture.probe->callsAt(ChunkPos{0, 0}) == 2);
    CHECK(world.stats().loadedChunks == 1);
    // The marker says which call produced the loaded chunk: the second.
    CHECK(world.getBlock(BlockPos{0, 319, 0}) == BlockStateId{2});
}

TEST_CASE("Destroying the world with jobs in flight leaves the jobs safe", "[world][manager]")
{
    Fixture fixture;
    JobSystem jobs(2);
    {
        World world(fixture.registry, jobs, fixture.generator());
        fixture.probe->setOpen(false);
        world.ensureLoaded(ChunkPos{0, 0}, 1);
        REQUIRE(fixture.probe->waitForCalls(2)); // Two jobs inside the generator, seven queued.
    }
    // The world is gone; the jobs hold only their position and generator. ASan and TSan runs check the rest.
    fixture.probe->setOpen(true);
    jobs.waitIdle();
    CHECK(fixture.probe->finished == 9);
    CHECK(jobs.pendingJobs() == 0);
}

TEST_CASE("Jobs the job system refuses fail at once and are not retried", "[world][manager]")
{
    const aurora::test::QuietLog quiet; // One error per failed chunk, on purpose.
    Fixture fixture;
    JobSystem jobs(1);
    jobs.shutdown();
    World world(fixture.registry, jobs, fixture.generator());

    world.ensureLoaded(ChunkPos{0, 0}, 1);
    world.update();
    CHECK(world.stats().failedChunks == 9);
    CHECK(world.stats().pendingChunks == 0);
    world.ensureLoaded(ChunkPos{0, 0}, 1);
    CHECK(world.stats().failedChunks == 9); // Still the same nine entries.
    CHECK(fixture.probe->totalCalls == 0);
    CHECK(world.getBlock(BlockPos{0, 63, 0}) == std::nullopt);
}

TEST_CASE("A failed chunk is retried only after it leaves the range and comes back", "[world][manager]")
{
    const aurora::test::QuietLog quiet; // One error per failed chunk, on purpose.
    Fixture fixture;
    fixture.probe->throwAt = {ChunkPos{1, 0}};
    fixture.probe->nullAt = {ChunkPos{0, 1}};
    fixture.probe->wrongPosAt = {ChunkPos{-1, -1}};
    JobSystem jobs(2);
    World world(fixture.registry, jobs, fixture.generator());

    world.ensureLoaded(ChunkPos{0, 0}, 1);
    finishJobs(jobs, world);
    CHECK(world.stats().loadedChunks == 6);
    CHECK(world.stats().failedChunks == 3); // Threw, returned nullptr, returned another position.
    CHECK(world.stats().pendingChunks == 0);
    CHECK(world.chunk(ChunkPos{1, 0}) == nullptr);
    CHECK(world.chunk(ChunkPos{-1, -1}) == nullptr);

    // Staying in range: no new attempt.
    for (int i = 0; i < 3; ++i) {
        world.ensureLoaded(ChunkPos{0, 0}, 1);
        finishJobs(jobs, world);
    }
    CHECK(fixture.probe->callsAt(ChunkPos{1, 0}) == 1);
    CHECK(fixture.probe->callsAt(ChunkPos{0, 1}) == 1);
    CHECK(world.stats().failedChunks == 3);

    // Leave (every old entry is dropped), fix the generator and come back: one more attempt each.
    world.ensureLoaded(ChunkPos{10, 0}, 1);
    finishJobs(jobs, world);
    {
        std::lock_guard lock(fixture.probe->mutex);
        fixture.probe->throwAt.clear();
        fixture.probe->nullAt.clear();
        fixture.probe->wrongPosAt.clear();
    }
    world.ensureLoaded(ChunkPos{0, 0}, 1);
    finishJobs(jobs, world);
    CHECK(world.stats().loadedChunks == 9);
    CHECK(world.stats().failedChunks == 0);
    CHECK(fixture.probe->callsAt(ChunkPos{1, 0}) == 2);
    CHECK(fixture.probe->callsAt(ChunkPos{0, 1}) == 2);
}

TEST_CASE("A snapshot copies the chunk and does not follow later changes", "[world][snapshot]")
{
    Fixture fixture;
    JobSystem jobs(1);
    World world(fixture.registry, jobs, makeFlatGenerator(fixture.preset));
    world.ensureLoaded(ChunkPos{-2, 3}, 0);
    finishJobs(jobs, world);

    const std::vector<ChunkUpdate> updates = world.takeChunkUpdates();
    REQUIRE(updates.size() == 1);
    REQUIRE(updates[0].snapshot);
    const ChunkSnapshot& snapshot = *updates[0].snapshot;
    const Chunk& chunk = *world.chunk(ChunkPos{-2, 3});
    CHECK(snapshot.pos() == ChunkPos{-2, 3});
    for (std::int32_t index = 0; index < aurora::core::kSectionsPerChunk; ++index) {
        INFO("section " << index);
        CHECK((snapshot.section(index) == nullptr) == (chunk.section(index) == nullptr));
        CHECK((snapshot.section(index) == nullptr || snapshot.section(index) != chunk.section(index))); // A copy.
    }
    for (const std::int32_t y : {-64, 59, 60, 63, 64, 319}) {
        CHECK(snapshot.getBlock(3, y, 4) == chunk.getBlock(3, y, 4));
    }

    const BlockStateId log = stateOf(*fixture.registry, "aurora:oak_log");
    REQUIRE(world.setBlock(BlockPos{-30, 100, 50}, log));
    REQUIRE(world.setBlock(BlockPos{-30, 63, 50}, aurora::data::kAirState));
    CHECK(snapshot.getBlock(2, 100, 2) == aurora::data::kAirState);
    CHECK(snapshot.getBlock(2, 63, 2) == stateOf(*fixture.registry, "aurora:grass_block"));
    CHECK(world.takeChunkUpdates().empty()); // Block changes are not announced before P0-7.
}

TEST_CASE("Load updates carry generations and unloads repeat them", "[world][snapshot]")
{
    const aurora::test::QuietLog quiet; // One failure is logged on purpose.
    Fixture fixture;
    fixture.probe->throwAt = {ChunkPos{1, 1}};
    JobSystem jobs(2);
    World world(fixture.registry, jobs, fixture.generator());

    world.ensureLoaded(ChunkPos{0, 0}, 1);
    finishJobs(jobs, world);
    const std::vector<ChunkUpdate> loads = world.takeChunkUpdates();
    REQUIRE(loads.size() == 8); // The failed chunk is not announced.
    std::unordered_map<ChunkPos, std::uint64_t, ChunkPosHash> generations;
    for (const ChunkUpdate& update : loads) {
        CHECK(update.kind == ChunkUpdate::Kind::Loaded);
        CHECK(update.generation == update.snapshot->generation());
        CHECK(generations.emplace(update.pos, update.generation).second);
    }
    CHECK_FALSE(generations.contains(ChunkPos{1, 1}));

    // A request still pending when it leaves the range was never announced, so it is not unloaded either.
    fixture.probe->setOpen(false);
    {
        std::lock_guard lock(fixture.probe->mutex);
        fixture.probe->throwAt.clear();
    }
    world.ensureLoaded(ChunkPos{0, 3}, 1); // Rows z = 2..4 are new, (x, 3) and (x, 4) pending; z = -1 drops.
    world.ensureLoaded(ChunkPos{0, 20}, 1);
    fixture.probe->setOpen(true);
    finishJobs(jobs, world);
    std::vector<ChunkUpdate> unloads = world.takeChunkUpdates();
    std::erase_if(unloads, [](const ChunkUpdate& update) { return update.kind == ChunkUpdate::Kind::Loaded; });
    REQUIRE(unloads.size() == 8);
    for (const ChunkUpdate& update : unloads) {
        CHECK_FALSE(update.snapshot);
        REQUIRE(generations.contains(update.pos));
        CHECK(update.generation == generations.at(update.pos));
    }

    // Coming back loads every position again with new, larger numbers.
    world.ensureLoaded(ChunkPos{0, 0}, 1);
    finishJobs(jobs, world);
    std::vector<ChunkUpdate> reloads = world.takeChunkUpdates();
    std::erase_if(reloads, [](const ChunkUpdate& update) { return update.kind == ChunkUpdate::Kind::Unloaded; });
    CHECK(reloads.size() == 9);
    for (const ChunkUpdate& update : reloads) {
        if (generations.contains(update.pos)) {
            CHECK(update.generation > generations.at(update.pos));
        }
    }
}
