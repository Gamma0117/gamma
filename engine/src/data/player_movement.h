#pragma once

#include "data/block_loader.h"
#include "data/load_issue.h"

#include <filesystem>
#include <memory>
#include <span>
#include <vector>

namespace aurora::data {

// How the player moves (data/<ns>/player/movement.json). Lengths in blocks, speeds in blocks per second,
// accelerations as the share of the gap to the target speed that one tick closes.
struct PlayerMovementTuning {
    double width = 0.0; // Collision box: a square footprint...
    double height = 0.0; // ...this tall, standing on the position.
    double eyeHeight = 0.0;
    double sneakEyeHeight = 0.0;
    double stepHeight = 0.0; // Highest ledge climbed without jumping.
    double gravity = 0.0;    // Blocks per second squared.
    double terminalVelocity = 0.0;
    double jumpVelocity = 0.0;
    double walkSpeed = 0.0;
    double sprintSpeed = 0.0;
    double sneakSpeed = 0.0;
    double groundAcceleration = 0.0;
    double airAcceleration = 0.0;
};

// Lower bound of the player's width and height: far above the collision epsilon, so the cells a box covers are
// never an empty range.
inline constexpr double kMinPlayerSize = 0.1;
inline constexpr double kMaxPlayerSize = 4.0;

struct PlayerMovementLoadResult {
    // Set only when the file had no errors.
    std::shared_ptr<const PlayerMovementTuning> tuning;
    std::vector<LoadIssue> issues;
    std::filesystem::path file; // The file that was read; empty if none was found.
};

// Reads data/aurora/player/movement.json from the last pack that has one (a mod replaces the base game's file as a
// whole). Every field is required and must be a finite number within its range:
//   width, height                [kMinPlayerSize, kMaxPlayerSize]
//   eye_height                   (0, height]
//   sneak_eye_height             (0, eye_height]
//   step_height                  [0, height)
//   gravity                      (0, 200]  (the ground is found by falling onto it, so 0 never stands)
//   terminal_velocity            (0, 200]
//   jump_velocity, walk_speed, sprint_speed, sneak_speed   [0, 50]
//   ground_acceleration, air_acceleration                  (0, 1]
// Unknown fields are warnings. Every problem is collected as a LoadIssue.
PlayerMovementLoadResult loadPlayerMovement(std::span<const DataPack> packs);

// Logs every issue and a summary line.
void logPlayerMovementLoadResult(const PlayerMovementLoadResult& result);

} // namespace aurora::data
