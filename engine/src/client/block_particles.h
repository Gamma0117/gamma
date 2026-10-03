#pragma once

#include "data/player_interaction.h"
#include "entity/player_messages.h"

#include <glm/vec3.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

namespace aurora::client {

class ClientWorld;

struct BlockParticleStats {
    std::uint64_t events = 0;          // Broken blocks that made fragments.
    std::uint64_t tooOld = 0;          // Events past the fragments' lifetime when they arrived (made none).
    std::uint64_t otherLoad = 0;       // Events whose chunk is not held in the same load (made none).
    std::uint64_t negativeAge = 0;     // Events from the future of the given clock (a clock mismatch; made none).
    std::uint64_t evicted = 0;         // Fragments dropped early to stay within the limit, oldest first.
};

// The fragments of broken blocks: plain falling squares in the block's colour, no collision. Main thread only.
//
// - Every server event makes `particle_count` fragments once (events come once each, in order). Their lifetime
//   counts from when the server broke the block (event.occurredAt, the P0 process clock), not from when the event
//   arrived: an event that is already older than the lifetime (after a minimised window, say) makes nothing, and a
//   younger one starts where its fragments would be by now.
// - None for a chunk the client does not hold in the event's load (generation): a reloaded chunk shows no old
//   fragments.
// - Start positions and velocities come from the block position and the server tick (a fixed generator), so the
//   same event always makes the same fragments. Motion is closed-form: p0 + v t + g t^2 / 2.
// - At most `max_particles` at once; adding beyond drops the oldest.
class BlockParticles {
public:
    using Clock = std::chrono::steady_clock;

    struct Instance {
        glm::dvec3 position{0.0};
        data::Rgb colour{};
        float size = 0.0f;
    };

    BlockParticles(const data::PlayerInteractionTuning& tuning, std::vector<data::Rgb> colours);

    void add(const entity::BlockBrokenEvent& event, const ClientWorld& world, Clock::time_point now);
    // Drops the fragments whose lifetime is over at `now`.
    void update(Clock::time_point now);
    // Every live fragment where it is at `now`.
    std::vector<Instance> instances(Clock::time_point now) const;

    std::size_t count() const { return m_particles.size(); }
    BlockParticleStats stats() const { return m_stats; }

private:
    struct Particle {
        glm::dvec3 start{0.0}; // At `born`.
        glm::dvec3 velocity{0.0};
        Clock::time_point born{};
        data::Rgb colour{};
    };

    std::uint32_t m_count;
    std::chrono::duration<double> m_lifetime;
    double m_gravity;
    double m_speed;
    float m_size;
    std::size_t m_max;
    std::vector<data::Rgb> m_colours;
    std::deque<Particle> m_particles; // Oldest first.
    BlockParticleStats m_stats;
};

} // namespace aurora::client
