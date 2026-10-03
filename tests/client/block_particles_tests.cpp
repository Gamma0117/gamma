#include "client/block_particles.h"
#include "client/client_world.h"

#include "client_test_support.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <vector>

using namespace aurora;
using namespace std::chrono_literals;
using client::BlockParticles;
using Clock = BlockParticles::Clock;

namespace {

data::PlayerInteractionTuning tuning()
{
    data::PlayerInteractionTuning tuning;
    tuning.particleCount = 12;
    tuning.particleLifetime = 0.6;
    tuning.particleGravity = 32.0;
    tuning.particleSpeed = 2.0;
    tuning.particleSize = 0.06;
    tuning.maxParticles = 64;
    return tuning;
}

const Clock::time_point kStart = Clock::time_point{} + 1h;

entity::BlockBrokenEvent brokenAt(world::BlockPos pos, std::uint64_t generation, Clock::time_point when,
                                  std::uint64_t tick = 7, data::BlockStateId state = 2)
{
    return {.position = pos, .previousState = state, .generation = generation, .serverTick = tick, .occurredAt = when};
}

struct Fixture {
    client::ClientWorld world{1};
    std::vector<data::Rgb> colours{{0, 0, 0}, {255, 0, 255}, {10, 20, 30}};

    Fixture() { test::loadSquare(world, {0, 0}, 2); } // Generations 1..25; (0, 0) is 13.

    std::uint64_t generationAt(world::ChunkPos pos) const { return world.snapshot(pos)->generation(); }
};

} // namespace

TEST_CASE("A broken block makes its fragments once and the same ones every time", "[client][particles]")
{
    Fixture fixture;
    const auto event = brokenAt({3, 64, 5}, fixture.generationAt({0, 0}), kStart);
    BlockParticles first(tuning(), fixture.colours);
    BlockParticles second(tuning(), fixture.colours);
    first.add(event, fixture.world, kStart);
    second.add(event, fixture.world, kStart);
    REQUIRE(first.count() == 12);
    const auto a = first.instances(kStart + 100ms);
    const auto b = second.instances(kStart + 100ms);
    REQUIRE(a.size() == 12);
    for (std::size_t i = 0; i < a.size(); ++i) {
        CHECK(a[i].position == b[i].position);
        CHECK(a[i].colour == data::Rgb{10, 20, 30}); // The broken state's colour.
        CHECK(a[i].size == 0.06f);
    }
    // At the moment of breaking they are inside the block.
    for (const auto& particle : first.instances(kStart)) {
        CHECK(particle.position.x >= 3.0);
        CHECK(particle.position.x <= 4.0);
        CHECK(particle.position.y >= 64.0);
        CHECK(particle.position.y <= 65.0);
    }
    // Another tick at the same place makes other fragments.
    BlockParticles other(tuning(), fixture.colours);
    other.add(brokenAt({3, 64, 5}, fixture.generationAt({0, 0}), kStart, 8), fixture.world, kStart);
    CHECK(other.instances(kStart + 100ms)[0].position != a[0].position);
}

TEST_CASE("Fragments live from the breaking and not from their arrival", "[client][particles]")
{
    Fixture fixture;
    const std::uint64_t generation = fixture.generationAt({0, 0});
    BlockParticles fresh(tuning(), fixture.colours);
    BlockParticles late(tuning(), fixture.colours);
    const auto event = brokenAt({1, 64, 1}, generation, kStart);
    fresh.add(event, fixture.world, kStart);
    late.add(event, fixture.world, kStart + 300ms); // Arrives 0.3 s late: half way through its life.
    REQUIRE(late.count() == 12);
    const auto now = kStart + 400ms;
    const auto a = fresh.instances(now);
    const auto b = late.instances(now);
    for (std::size_t i = 0; i < a.size(); ++i) {
        CHECK(a[i].position == b[i].position); // The same place at the same time.
    }
    late.update(kStart + 599ms);
    CHECK(late.count() == 12);
    late.update(kStart + 600ms);
    CHECK(late.count() == 0);

    // After a minimised minute, or when it arrives exactly at the end of its life: nothing.
    BlockParticles restored(tuning(), fixture.colours);
    restored.add(event, fixture.world, kStart + 60s);
    restored.add(event, fixture.world, kStart + 600ms);
    CHECK(restored.count() == 0);
    CHECK(restored.stats().tooOld == 2);

    // From the future of this clock: a mismatch, reported and dropped.
    BlockParticles confused(tuning(), fixture.colours);
    confused.add(brokenAt({1, 64, 1}, generation, kStart + 1s), fixture.world, kStart);
    CHECK(confused.count() == 0);
    CHECK(confused.stats().negativeAge == 1);
}

TEST_CASE("Fragments need the chunk in the event's load", "[client][particles]")
{
    Fixture fixture;
    BlockParticles particles(tuning(), fixture.colours);
    const std::uint64_t old = fixture.generationAt({0, 0});
    particles.add(brokenAt({40, 64, 40}, 1, kStart), fixture.world, kStart); // Not held.
    fixture.world.apply(test::unloaded({0, 0}, old));
    fixture.world.apply(test::loaded({0, 0}, 500));
    particles.add(brokenAt({1, 64, 1}, old, kStart), fixture.world, kStart); // The same place, an older load.
    CHECK(particles.count() == 0);
    CHECK(particles.stats().otherLoad == 2);
    particles.add(brokenAt({1, 64, 1}, 500, kStart), fixture.world, kStart);
    CHECK(particles.count() == 12);
}

TEST_CASE("Fragments stay within the limit with the oldest out first", "[client][particles]")
{
    Fixture fixture;
    BlockParticles particles(tuning(), fixture.colours);
    for (std::uint64_t tick = 0; tick < 10; ++tick) {
        particles.add(brokenAt({2, 64, 2}, fixture.generationAt({0, 0}), kStart + std::chrono::milliseconds(tick),
                               tick),
                      fixture.world, kStart + 10ms);
    }
    CHECK(particles.count() == 64);
    CHECK(particles.stats().evicted == 10 * 12 - 64);
    CHECK(particles.stats().events == 10);
}

TEST_CASE("Fragments far from the origin stay at their block", "[client][particles]")
{
    for (const std::int32_t base : {30'000'000, -30'000'000}) {
        client::ClientWorld world(1);
        const world::ChunkPos chunk = world::chunkPosOf({base, 0, base});
        test::loadSquare(world, chunk, 1);
        BlockParticles particles(tuning(), {});
        particles.add(brokenAt({base, 64, base}, world.snapshot(chunk)->generation(), kStart), world, kStart);
        REQUIRE(particles.count() == 12);
        for (const auto& particle : particles.instances(kStart + 50ms)) {
            CHECK(std::abs(particle.position.x - (base + 0.5)) < 1.0);
            CHECK(std::abs(particle.position.z - (base + 0.5)) < 1.0);
            CHECK(particle.colour == data::kMissingParticleColour); // No colour table entry.
        }
    }
}
