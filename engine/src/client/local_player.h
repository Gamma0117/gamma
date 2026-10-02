#pragma once

#include "data/player_movement.h"
#include "entity/player_messages.h"
#include "entity/player_movement.h"

#include <glm/vec3.hpp>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>

namespace aurora::entity {
struct CollisionWorld;
}

namespace aurora::client {

struct LocalPlayerStats {
    bool spawned = false;
    std::uint32_t lastSent = 0;
    std::uint32_t lastInput = 0; // The settled run of the newest server state.
    std::uint64_t serverTick = 0;
    std::size_t history = 0;     // Inputs sent and not settled yet.
    bool paused = false;         // The history is full: no new inputs until the server settles some.
    bool resyncing = false;
    bool frozen = false;         // The newest server state.
    std::uint64_t corrections = 0;
    std::uint64_t inputPauses = 0;
    std::uint64_t resyncs = 0;
};

// The client's own player, predicted ahead of the server. Main thread only. One per server session, like the
// ClientWorld: sequence numbers and server ticks count from that session's start.
//
// Every client tick turns one intent into an input with the next sequence number (from 1 after the spawn), steps
// the same physics as the server on the client's copy of the world and keeps the input until the server settles
// it. A server state settles every input up to its lastInput; prediction then starts again from that state and
// replays the inputs after it, so the client always ends up where the server's world puts it.
//
// - States are used in serverTick order; an older or repeated one is ignored. The first state is the spawn.
// - Normal: after dropping the settled inputs, either none is left and lastInput == the last sequence sent (all
//   settled), or the first left is lastInput + 1. A replay that moves the current position by more than
//   kCorrectionThreshold counts as a correction.
// - The history is never cut. With kMaxHistory inputs unsettled (the server has stopped answering) no new input is
//   made and nothing is predicted until a state settles some; entering that pause counts once in inputPauses.
// - Anything else is a gap, which the server's contract rules out. Defence: log, wait without new inputs while
//   showing the server's states, until lastInput reaches the last sequence sent at the gap, then go on with the
//   next sequence (counted in resyncs).
// - neutralize(): input stopped. Every unsettled input becomes neutral here as on the server (same look), the
//   prediction is replayed (not counted as a correction), and the range is returned for the server once.
//
// Drawing: the position between the previous and the current tick's prediction (renderPosition(alpha)). Replays,
// resyncs and the spawn set both from their own results, so no old prediction is ever blended in; a tick that
// predicts nothing sets previous = current.
class LocalPlayer {
public:
    static constexpr std::size_t kMaxHistory = 40;
    static constexpr double kCorrectionThreshold = 1e-4;

    explicit LocalPlayer(std::shared_ptr<const data::PlayerMovementTuning> tuning);

    bool spawned() const { return m_spawned; }
    void receive(const entity::PlayerState& state, const entity::CollisionWorld& world);
    // One client tick. Returns the input to send, or nothing when no input is made (not spawned, paused,
    // resyncing). The intent is sanitised the way the server will.
    std::optional<entity::PlayerInput> tick(const entity::MovementIntent& intent, const entity::CollisionWorld& world);
    // Returns the sequence to send as Neutralize(through), or nothing if every sent input was neutralised already.
    std::optional<std::uint32_t> neutralize(const entity::CollisionWorld& world);

    const entity::PlayerMotion& current() const { return m_current; }
    const entity::PlayerMotion& previous() const { return m_previous; }
    glm::dvec3 renderPosition(double alpha) const;
    // Eye height above the feet, between the previous and current tick's (sneaking lowers it).
    double eyeHeight(double alpha) const;
    LocalPlayerStats stats() const;

private:
    struct Sent {
        std::uint32_t sequence = 0;
        entity::MovementIntent intent;
        entity::PlayerMotion after; // Predicted state after this input.
    };

    void replay(const entity::CollisionWorld& world, bool countCorrection);
    void hold(); // previous = current.
    double eyeFor(bool sneaking) const;

    std::shared_ptr<const data::PlayerMovementTuning> m_tuning;
    bool m_spawned = false;
    std::optional<entity::PlayerState> m_base;
    std::deque<Sent> m_history;
    std::uint32_t m_lastSent = 0;
    std::uint32_t m_lastNeutralized = 0;
    float m_lastValidYaw = 0.0f;
    float m_lastValidPitch = 0.0f;

    entity::PlayerMotion m_current;
    entity::PlayerMotion m_previous;
    bool m_currentSneak = false;
    bool m_previousSneak = false;

    bool m_paused = false;
    bool m_resyncing = false;
    std::uint32_t m_resyncBoundary = 0;
    std::uint64_t m_corrections = 0;
    std::uint64_t m_inputPauses = 0;
    std::uint64_t m_resyncs = 0;
};

class MovementSampler;

// Input stopped (focus lost, mouse released, minimised, UI keyboard or free flight on): drops the pending jump and
// makes the unsettled inputs neutral. Returns the range to send to the server as Neutralize, if there is a new one.
// Call it as it happens, every time; repeated calls send nothing new.
std::optional<std::uint32_t> blockPlayerInput(MovementSampler& sampler, LocalPlayer& player,
                                              const entity::CollisionWorld& world);

} // namespace aurora::client
