#include "client/block_particles.h"

#include "client/client_world.h"
#include "core/log.h"
#include "core/profiler.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace aurora::client {

namespace {

// splitmix64: the same numbers on every platform and standard library.
class Random {
public:
    explicit Random(std::uint64_t seed)
        : m_state(seed)
    {
    }

    // Uniform in [0, 1).
    double next()
    {
        std::uint64_t z = (m_state += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        z ^= z >> 31;
        return static_cast<double>(z >> 11) * 0x1.0p-53;
    }

private:
    std::uint64_t m_state;
};

std::uint64_t seedOf(const entity::BlockBrokenEvent& event)
{
    const auto bits = [](std::int32_t value) {
        return static_cast<std::uint64_t>(static_cast<std::uint32_t>(value));
    };
    return (bits(event.position.x) * 0x100000001B3ull) ^ (bits(event.position.y) << 21) ^
           (bits(event.position.z) << 42) ^ (event.serverTick * 0x9E3779B97F4A7C15ull);
}

} // namespace

BlockParticles::BlockParticles(const data::PlayerInteractionTuning& tuning, std::vector<data::Rgb> colours)
    : m_count(tuning.particleCount)
    , m_lifetime(tuning.particleLifetime)
    , m_gravity(tuning.particleGravity)
    , m_speed(tuning.particleSpeed)
    , m_size(static_cast<float>(tuning.particleSize))
    , m_max(tuning.maxParticles)
    , m_colours(std::move(colours))
{
}

void BlockParticles::add(const entity::BlockBrokenEvent& event, const ClientWorld& world, Clock::time_point now)
{
    const std::shared_ptr<const world::ChunkSnapshot> chunk = world.snapshot(world::chunkPosOf(event.position));
    if (!chunk || chunk->generation() != event.generation) {
        ++m_stats.otherLoad;
        return;
    }
    const std::chrono::duration<double> age = now - event.occurredAt;
    if (age.count() < 0.0) {
        ++m_stats.negativeAge;
        core::logWarn("client", "A broken block at ({}, {}, {}) is {:.3f} s in the future; no fragments",
                      event.position.x, event.position.y, event.position.z, -age.count());
        return;
    }
    if (age >= m_lifetime) {
        ++m_stats.tooOld;
        return;
    }
    ++m_stats.events;
    const data::Rgb colour =
        event.previousState < m_colours.size() ? m_colours[event.previousState] : data::kMissingParticleColour;
    const glm::dvec3 corner(event.position.x, event.position.y, event.position.z);
    Random random(seedOf(event));
    for (std::uint32_t i = 0; i < m_count; ++i) {
        Particle particle;
        particle.start = corner + glm::dvec3(0.2 + 0.6 * random.next(), 0.2 + 0.6 * random.next(),
                                             0.2 + 0.6 * random.next());
        // Outwards and a little up, up to the configured speed.
        const double angle = 2.0 * std::numbers::pi * random.next();
        const double speed = m_speed * (0.4 + 0.6 * random.next());
        particle.velocity = glm::dvec3(std::cos(angle) * speed, (0.5 + random.next()) * speed * 0.8,
                                       std::sin(angle) * speed);
        particle.born = event.occurredAt;
        particle.colour = colour;
        m_particles.push_back(particle);
    }
    while (m_particles.size() > m_max) {
        m_particles.pop_front();
        ++m_stats.evicted;
    }
}

void BlockParticles::update(Clock::time_point now)
{
    std::erase_if(m_particles, [&](const Particle& particle) { return now - particle.born >= m_lifetime; });
}

std::vector<BlockParticles::Instance> BlockParticles::instances(Clock::time_point now) const
{
    AURORA_PROFILE_ZONE_N("Block particles");
    std::vector<Instance> result;
    result.reserve(m_particles.size());
    for (const Particle& particle : m_particles) {
        const double t = std::max(0.0, std::chrono::duration<double>(now - particle.born).count());
        if (t >= m_lifetime.count()) {
            continue;
        }
        const glm::dvec3 position =
            particle.start + particle.velocity * t + glm::dvec3(0.0, -0.5 * m_gravity * t * t, 0.0);
        result.push_back({position, particle.colour, m_size});
    }
    return result;
}

} // namespace aurora::client
