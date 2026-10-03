#pragma once

#include "data/block_registry.h"
#include "data/player_movement.h"
#include "entity/player_movement.h"
#include "world/coordinates.h"

#include <glm/vec3.hpp>

#include <cstdint>
#include <vector>

namespace aurora::entity {

class BlockCollisionView;

// Where a look along (yaw, pitch) in degrees points: yaw 0 is north (-z), 90 east (+x); pitch +90 straight up.
// The camera, the client's block selection and the server's block actions all use this.
glm::dvec3 lookDirection(double yaw, double pitch);

// The eyes of a player with `motion`: the feet plus the eye height of its pose (sneaking or not).
glm::dvec3 eyePosition(const PlayerMotion& motion, const data::PlayerMovementTuning& tuning);

// Which states a look can pick (index = state id): every state that is not air and not drawn invisible. Each is
// picked as a full cube, as it is drawn (partial shapes come with P0-7b).
std::vector<bool> selectableStates(const data::BlockRegistry& registry);

enum class RaycastStatus : std::uint8_t {
    Hit,      // A selectable block within reach.
    Miss,     // Nothing within reach.
    Unloaded, // The ray reached a column that is not loaded before any hit; nothing beyond it is picked.
    Invalid,  // Not a usable ray: a non-finite or zero direction, a non-finite origin or one outside the block
              // coordinate range, a reach outside [data::kMinReach, data::kMaxReach], or an origin inside a
              // selectable block (whose own faces cannot be aimed at).
};

struct RaycastResult {
    RaycastStatus status = RaycastStatus::Miss;
    // Hit only:
    world::BlockPos cell;
    data::BlockStateId state = data::kAirState;
    data::BlockFace face = data::BlockFace::Up; // The face of `cell` the ray entered through.
    glm::ivec3 normal{0};                       // That face's outward normal: the cell in front of it is cell+normal.
    double distance = 0.0;                      // From the origin to where the ray enters the cell, in blocks.
    glm::dvec3 point{0.0};                      // That entry point.
};

// Ray tolerance: crossings closer than this (in blocks along the ray) count as simultaneous.
inline constexpr double kRayEpsilon = 1e-9;

// Walks the block grid from `origin` along `direction` (any length; it is normalised here, so reach and distance
// are in blocks) and returns the first selectable cell whose entry point is at most `reach` away.
// - Cells are found with floor (negative coordinates included). Starting exactly on a grid plane while moving
//   towards negative coordinates, the first cell is the one on the moving side.
// - A zero component (+0 or -0) never crosses its axis. When the ray crosses several planes at once (through an
//   edge or a corner, within kRayEpsilon) it steps on all those axes together, so cells it only grazes for zero
//   length are never hit; the entry face is then the first of X, Y, Z among them.
// - Air and invisible states are passed through, as are cells outside the world height. Every other state is a
//   full cube. A column that is not loaded stops the ray (Unloaded).
RaycastResult raycastBlocks(const BlockCollisionView& view, const std::vector<bool>& selectable,
                            const glm::dvec3& origin, const glm::dvec3& direction, double reach);

} // namespace aurora::entity
