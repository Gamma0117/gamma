#pragma once

#include "client/cursor_controller.h"
#include "client/local_player.h"
#include "client/movement_sampler.h"
#include "core/tick_scheduler.h"
#include "data/player_movement.h"
#include "entity/player_messages.h"

#include <glm/vec3.hpp>

#include <cstdint>
#include <memory>
#include <optional>

namespace aurora::entity {
struct CollisionWorld;
}

namespace aurora::client {

// Where the local player's messages go, in the order they are made: the server's player mailbox in the game, a
// list in tests.
class PlayerMessageSink {
public:
    virtual ~PlayerMessageSink() = default;
    virtual void sendInput(const entity::PlayerInput& input) = 0;
    virtual void sendNeutralize(std::uint32_t through) = 0;
};

// One frame's input, as the window and the UI saw it after polling.
struct PlayerFrameInput {
    bool focusLost = false; // Since the last frame, even if focus came back in the same poll.
    bool minimised = false;
    bool focused = false;
    bool escapePressed = false;
    bool clickPressed = false; // Left mouse button.
    bool uiWantsMouse = false;
    bool uiWantsKeyboard = false;
    bool jumpPressed = false; // Space pressed during the poll: a tap can be shorter than a tick.
    bool enabled = true;      // False: the game never takes input (screenshot mode); Esc and clicks are ignored.
};

// The local player's part of the main loop: who owns the mouse, when movement input counts and stops, the 20 Hz
// client tick clock and the prediction. The app and the tests drive the player through this one class, so the order
// below exists once. Main thread only.
//
// Every frame:
//  1. input(frame), after polling and after the UI started its frame (so a click on the F3 panel is known):
//     - a focus loss releases the mouse and blocks;
//     - a minimised window blocks and ends the player's frame: input() returns false and no ticks run;
//     - Esc releases the mouse, a click captures it (not when the UI wants the mouse);
//     - a release since the last frame blocks, even if a click of the same poll captured again;
//     - accepting = enabled, focused, captured, the UI not taking the keyboard and not flying freely; if it was on
//       and is off now, block;
//     - the sampler closes the frame (a jump press is kept for the next tick only if nothing disturbed the frame).
//  2. update(state, now, keys, yaw, pitch): the newest server state (the first one is the spawn and starts the
//     client tick clock at `now`), then every client tick due at `now`: sample, predict, send.
//  3. setFreeFlight(on), from the F3 panel after the world: switching on blocks in that same frame.
// block(): the pending jump is dropped, the unsettled inputs become neutral here and a Neutralize is sent (only when
// something new was sent since the last one). Every way input stops blocks as it happens.
class PlayerControl {
public:
    PlayerControl(std::shared_ptr<const data::PlayerMovementTuning> tuning, const entity::CollisionWorld& world,
                  PlayerMessageSink& sink);

    // Returns false for a minimised window: the frame has no player work left.
    bool input(const PlayerFrameInput& frame);
    void update(const std::optional<entity::PlayerState>& state, core::TickScheduler::TimePoint now,
                const MovementKeys& keys, float yaw, float pitch);
    void setFreeFlight(bool on);

    bool freeFlight() const { return m_freeFlight; }
    // Whether movement input counted at the end of the last input().
    bool accepting() const { return m_sampler.accepting(); }
    // How far `now` is between the last client tick and the next (0..1), for drawing.
    double tickProgress(core::TickScheduler::TimePoint now) const { return m_clock.progress(now); }
    // Where the eyes are drawn at `now`: the feet and the eye height between the last two client ticks. Only
    // meaningful after the spawn.
    glm::dvec3 eyePosition(core::TickScheduler::TimePoint now) const;

    const LocalPlayer& localPlayer() const { return m_player; }
    // The app feeds it the cursor movement and reads the look from it.
    CursorController& cursor() { return m_cursor; }
    const CursorController& cursor() const { return m_cursor; }

private:
    void block();

    const entity::CollisionWorld& m_world;
    PlayerMessageSink& m_sink;
    CursorController m_cursor;
    MovementSampler m_sampler;
    LocalPlayer m_player;
    core::TickScheduler m_clock;
    bool m_freeFlight = false;
};

} // namespace aurora::client
