#pragma once

#include "entity/player_movement.h"

#include <cstdint>

namespace aurora::entity {

// Client -> server: the intent of one client tick. Plain values, so the packets of P0-11 carry the same fields.
//
// `sequence` numbers the inputs the client actually made, from 1 after the spawn, one more each time; ticks the
// client skipped or paused use no number.
struct PlayerInput {
    std::uint32_t sequence = 0;
    MovementIntent intent;
};

// Server -> client after every server tick: the authoritative state for prediction to start from. The motion
// carries the pose of the intent the tick applied (neutral on a starving or filling tick, neutral for an input
// the client neutralised), so a replay starts from the pose the server really has.
//
// `lastInput` ends the settled run: every input with a sequence at or below it has been applied, dropped or will
// never be accepted, and every input still waiting on the server has a larger sequence. It never goes down.
struct PlayerState {
    std::uint64_t serverTick = 0;
    std::uint32_t lastInput = 0;
    PlayerMotion motion;
    bool frozen = false; // The last tick stood still because a column under the player was not loaded (rule B).
};

} // namespace aurora::entity
