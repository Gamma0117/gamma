#pragma once

#include "entity/player_movement.h"

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

// Turns the keys into one intent per client tick, and decides when input counts at all. Plain state; the app feeds
// it with window input once per frame, in this order: block() whenever input stops (any time in the frame), then
// endFrame(), then sample() for each tick that runs.
//
// - Input counts while "accepting": focused, mouse captured, the UI not taking the keyboard, not flying freely and
//   not minimised. The app works that out; endFrame() is told the result at the end of the frame's input handling.
// - block() is called every time input stops (focus lost, mouse released, minimised, UI keyboard or free flight
//   switched on), as it happens, even if a later event of the same frame allows input again. It drops a pending
//   jump press and marks the frame.
// - A jump press (a tap between two ticks would be lost otherwise) is kept for the next tick only if the frame
//   started accepting, nothing blocked during it and it ended accepting. So a press from before a block, or from a
//   frame that was blocked in between (Esc then a click in the same poll), never jumps later.
// - Held keys are read at the tick, only while accepting; otherwise the intent is neutral (same look).
class MovementSampler {
public:
    bool accepting() const { return m_accepting; }

    void block();
    void endFrame(bool accepting, bool jumpPressed);
    entity::MovementIntent sample(const MovementKeys& held, float yaw, float pitch);

private:
    bool m_accepting = false; // At the end of the last frame.
    bool m_blockedThisFrame = false;
    bool m_jumpLatched = false;
};

} // namespace aurora::client
