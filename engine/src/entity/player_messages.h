#pragma once

#include "data/block_registry.h"
#include "entity/player_movement.h"
#include "world/coordinates.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string_view>

namespace aurora::entity {

// Client -> server: the intent of one client tick. Plain values, so the packets of P0-11 carry the same fields.
//
// `sequence` numbers the inputs the client actually made, from 1 after the spawn, one more each time; ticks the
// client skipped or paused use no number.
struct PlayerInput {
    std::uint32_t sequence = 0;
    MovementIntent intent;
};

// What became of a place attempt (an applied input with `use`).
enum class PlaceResult : std::uint8_t {
    None,         // No attempt yet.
    Applied,      // The block was placed.
    NoTarget,     // The look hit no block within reach (or the ray was not usable).
    InvalidSlot,  // The slot is not in the palette.
    Height,       // The cell in front of the face is outside the editable height (above the bedrock layer, below
                  // the build limit).
    Occupied,     // That cell is not air.
    Unloaded,     // That cell's column is not loaded.
    BlocksPlayer, // The block would overlap the player.
};

std::string_view placeResultName(PlaceResult result);

// Server -> client: a block the player broke, once each, in order (the effects' source). P0: `occurredAt` is the
// monotonic time of the server tick that broke it, in this process; a network client will need its own clock
// (P0-11).
struct BlockBrokenEvent {
    world::BlockPos position;
    data::BlockStateId previousState = data::kAirState;
    std::uint64_t generation = 0; // The load of the chunk it was broken in.
    std::uint64_t serverTick = 0;
    std::chrono::steady_clock::time_point occurredAt{};
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

    // The block being mined after this tick: where, which load of its chunk, ticks done and ticks needed. No
    // target (and zeros) when not mining.
    std::optional<world::BlockPos> digTarget{};
    std::uint64_t digGeneration = 0;
    std::uint32_t digProgress = 0;
    std::uint32_t digRequired = 0;
    // The last place attempt: the input that carried it and what happened. Only the last one (for F3): the client
    // never waits for it, and `lastInput` does not say what became of each input.
    std::uint32_t lastPlaceInput = 0;
    PlaceResult lastPlaceResult = PlaceResult::None;
};

} // namespace aurora::entity
