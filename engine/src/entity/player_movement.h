#pragma once

#include "data/player_movement.h"
#include "entity/aabb.h"

#include <glm/vec3.hpp>

#include <cstdint>

namespace aurora::entity {

struct CollisionWorld;

// What the player wants during one tick. Angles in degrees, as client::Camera: yaw 0 looks north (-Z), 90 east;
// pitch positive upwards. The look is the client's to decide; only the movement is checked by the server.
//
// The block actions ride on the same intent, so whatever makes an intent neutral (a Neutralize, a starving or
// filling tick, a look that is not a number) drops them too. stepPlayer ignores them.
struct MovementIntent {
    std::int8_t forward = 0; // +1 forward (W), -1 back (S).
    std::int8_t strafe = 0;  // +1 right (D), -1 left (A).
    bool jump = false;
    bool sneak = false;
    bool sprint = false;
    float yaw = 0.0f;
    float pitch = 0.0f;
    // Block actions of this tick (P0-7).
    bool attack = false;   // Mine what the look hits: the left button is held (after a press in an accepting
                           // frame) or was tapped since the last tick.
    bool use = false;      // Place on the face the look hits: a right-button press since the last tick.
    std::uint8_t slot = 0; // Palette slot for `use`, 0..8; the server checks it against its palette.

    // No movement, no jump, sneak or sprint, no block action; the same look.
    MovementIntent neutral() const { return {.yaw = yaw, .pitch = pitch}; }
    bool operator==(const MovementIntent&) const = default;
};

// What the server accepts from an intent: axes cut to -1..1, the yaw brought into [0, 360) and the pitch into
// [-90, 90]. A yaw or pitch that is not finite makes the whole intent neutral and puts `lastValidYaw` and
// `lastValidPitch` (themselves finite) in place of both angles.
MovementIntent sanitizeIntent(const MovementIntent& intent, float lastValidYaw, float lastValidPitch);

// The player's simulated state. Position is the centre of the feet (bottom of the collision box).
struct PlayerMotion {
    glm::dvec3 position{0.0};
    glm::dvec3 velocity{0.0}; // Blocks per second.
    bool onGround = false;
    // The pose of the last step, from the intent it used (sneaking lowers the eyes). Part of the state so the
    // server's state carries the pose of the intent it really applied, never one it dropped or neutralised.
    bool sneaking = false;
    bool sprinting = false; // isSprinting() of that intent.

    bool operator==(const PlayerMotion&) const = default;
};

// The collision box of a player standing at `feet`.
Aabb playerBox(const glm::dvec3& feet, const data::PlayerMovementTuning& tuning);

// Whether `intent` sprints: sprint held, moving forward and not sneaking (sneaking wins).
bool isSprinting(const MovementIntent& intent);

// One tick (core::kSecondsPerTick) of player movement, the same on the server (authoritative) and the client
// (prediction):
//  0. The pose (sneaking, sprinting) becomes the intent's, also on a frozen tick.
//  1. Rule B: if the box overlaps a column that is not loaded, nothing moves and the velocity becomes zero; the
//     result says frozen.
//  2. Wish direction from forward/strafe, turned by the yaw; diagonals are normalised.
//  3. Target speed: sneak, sprint (see isSprinting) or walk speed. The horizontal velocity closes the gap to the
//     target by the ground or air acceleration share.
//  4. On the ground with jump held: vertical velocity = jump velocity. Then gravity, limited to the terminal fall
//     speed.
//  5. Sweep the box by velocity * dt (Y, X, Z) against every box on the way.
//  6. Step up: only if step height > 0, the horizontal movement was blocked, this tick's vertical movement is not
//     upwards (so a jump is never turned into a step) and the player was on the ground or landed this tick. Path:
//     up by the step height (as far as the ceiling allows), across (X, Z), down by what went up plus the wanted
//     fall. It is taken if it moves further horizontally.
//  7. From the path taken: blocked horizontal axes stop that velocity component; blocked downwards means on the
//     ground with no vertical velocity; blocked upwards stops the vertical velocity.
struct StepResult {
    bool frozen = false;
};
StepResult stepPlayer(PlayerMotion& motion, const MovementIntent& intent, const data::PlayerMovementTuning& tuning,
                      const CollisionWorld& world);

} // namespace aurora::entity
