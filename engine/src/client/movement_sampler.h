#pragma once

#include "entity/player_movement.h"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace aurora::client {

// The movement keys held right now.
struct MovementKeys {
    bool forward = false;
    bool back = false;
    bool left = false;
    bool right = false;
    bool jump = false;
    bool sneak = false;
    bool sprint = false;
};

// One frame's presses that must not be lost between ticks, and the left button's held state, as the window saw
// them after polling.
struct FramePresses {
    bool jump = false;                // Space pressed.
    bool attack = false;              // Left button pressed (the same press may also capture the mouse).
    bool attackDown = false;          // Left button held now: the only source of the held state.
    bool use = false;                 // Right button pressed.
    std::optional<std::uint8_t> slot{}; // A number key pressed: its slot 0..8 (the lowest when several were).
};

// Turns the keys into one intent per client tick, and decides when input counts at all. Plain state; the app feeds
// it with window input once per frame, in this order: block() whenever input stops (any time in the frame), then
// endFrame(), then sample() for each tick that runs.
//
// - Input counts while "accepting": focused, mouse captured, the UI not taking the keyboard, not flying freely and
//   not minimised. The app works that out; endFrame() is told the result at the end of the frame's input handling.
// - block() is called every time input stops (focus lost, mouse released, minimised, UI keyboard or free flight
//   switched on), as it happens, even if a later event of the same frame allows input again. It drops everything
//   pending (jump, attack, use) and marks the frame.
// - A press counts only if the frame started accepting, nothing blocked during it and it ended accepting. So a
//   press from before a block, or from a frame that was blocked in between (Esc then a click in the same poll),
//   never acts later; neither does the click that captures the mouse (that frame did not start accepting).
//   - Jump and use: the press is kept for the next tick only (a tap can be shorter than a tick). Several presses
//     before a tick are one.
//   - Attack: the press arms the button and is kept as a tap for the next tick. Armed, the held button attacks
//     every tick; the first frame that sees it up (attackDown false), a block or not accepting disarms it, even if
//     no tick runs in that frame. The tap still reaches the next tick, so a click shorter than a tick hits once.
//   - Slot: the pressed number key selects its slot if the palette has it. Use takes the slot (and the look) of
//     the tick, not of the click.
// - Held movement keys are read at the tick, only while accepting; otherwise the intent is neutral (same look).
class MovementSampler {
public:
    explicit MovementSampler(std::size_t paletteSize = 0);

    bool accepting() const { return m_accepting; }
    // Armed and accepting: the left button is held for the game. Drives the crack effect, so a release hides the
    // cracks in the frame it is seen.
    bool attackActive() const { return m_accepting && m_armed; }
    std::uint8_t slot() const { return m_slot; }

    void block();
    void endFrame(bool accepting, const FramePresses& presses);
    entity::MovementIntent sample(const MovementKeys& held, float yaw, float pitch);

private:
    std::size_t m_paletteSize;
    bool m_accepting = false; // At the end of the last frame.
    bool m_blockedThisFrame = false;
    bool m_jumpLatched = false;
    bool m_armed = false;
    bool m_attackTapped = false;
    bool m_useLatched = false;
    std::uint8_t m_slot = 0;
};

} // namespace aurora::client
