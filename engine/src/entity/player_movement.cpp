#include "entity/player_movement.h"

#include "core/constants.h"
#include "core/profiler.h"
#include "entity/collision.h"

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace aurora::entity {

namespace {

constexpr double kTickSeconds = core::kSecondsPerTick;

// Unit horizontal direction of the movement keys for the intent's yaw, or zero.
glm::dvec3 wishDirection(const MovementIntent& intent)
{
    const double yaw = glm::radians(static_cast<double>(intent.yaw));
    const glm::dvec3 forward(std::sin(yaw), 0.0, -std::cos(yaw));
    const glm::dvec3 right(std::cos(yaw), 0.0, std::sin(yaw));
    const glm::dvec3 direction =
        forward * static_cast<double>(intent.forward) + right * static_cast<double>(intent.strafe);
    const double lengthSquared = glm::dot(direction, direction);
    return lengthSquared > 0.0 ? direction / std::sqrt(lengthSquared) : direction;
}

float finiteOr(float value, float fallback)
{
    return std::isfinite(value) ? value : fallback;
}

} // namespace

MovementIntent sanitizeIntent(const MovementIntent& intent, float lastValidYaw, float lastValidPitch)
{
    if (!std::isfinite(intent.yaw) || !std::isfinite(intent.pitch)) {
        return MovementIntent{.yaw = finiteOr(lastValidYaw, 0.0f), .pitch = finiteOr(lastValidPitch, 0.0f)};
    }
    MovementIntent result = intent;
    result.forward = std::clamp<std::int8_t>(intent.forward, -1, 1);
    result.strafe = std::clamp<std::int8_t>(intent.strafe, -1, 1);
    float yaw = std::fmod(intent.yaw, 360.0f);
    if (yaw < 0.0f) {
        yaw += 360.0f;
    }
    result.yaw = yaw >= 360.0f ? 0.0f : yaw; // -tiny + 360 can round to 360.
    result.pitch = std::clamp(intent.pitch, -90.0f, 90.0f);
    return result;
}

Aabb playerBox(const glm::dvec3& feet, const data::PlayerMovementTuning& tuning)
{
    const double half = tuning.width * 0.5;
    return {{feet.x - half, feet.y, feet.z - half}, {feet.x + half, feet.y + tuning.height, feet.z + half}};
}

bool isSprinting(const MovementIntent& intent)
{
    return intent.sprint && intent.forward > 0 && !intent.sneak;
}

StepResult stepPlayer(PlayerMotion& motion, const MovementIntent& intent, const data::PlayerMovementTuning& tuning,
                      const CollisionWorld& world)
{
    AURORA_PROFILE_ZONE_N("Player step");
    motion.sneaking = intent.sneak;
    motion.sprinting = isSprinting(intent);
    const Aabb box = playerBox(motion.position, tuning);
    if (touchesUnloadedColumn(world.view, box)) {
        motion.velocity = glm::dvec3(0.0);
        return {.frozen = true};
    }

    // Horizontal velocity towards the wished one; jump; gravity.
    const glm::dvec3 direction = wishDirection(intent);
    const double speed = intent.sneak       ? tuning.sneakSpeed
                         : isSprinting(intent) ? tuning.sprintSpeed
                                               : tuning.walkSpeed;
    const double acceleration = motion.onGround ? tuning.groundAcceleration : tuning.airAcceleration;
    glm::dvec3 velocity = motion.velocity;
    velocity.x += (direction.x * speed - velocity.x) * acceleration;
    velocity.z += (direction.z * speed - velocity.z) * acceleration;
    if (motion.onGround && intent.jump) {
        velocity.y = tuning.jumpVelocity;
    }
    velocity.y = std::max(velocity.y - tuning.gravity * kTickSeconds, -tuning.terminalVelocity);

    // Everything the normal path or a step path can reach.
    const glm::dvec3 wanted = velocity * kTickSeconds;
    Aabb region = unite(box, box.moved(wanted));
    region.max.y += tuning.stepHeight;
    std::vector<Aabb> obstacles;
    collectBoxes(world, region, obstacles);

    const SweepResult normal = sweep(box, wanted, obstacles);
    glm::dvec3 moved = normal.moved;
    bool blockedX = normal.blocked[kAxisX];
    bool blockedZ = normal.blocked[kAxisZ];
    bool blockedDown = wanted.y < 0.0 && normal.blocked[kAxisY];
    bool blockedUp = wanted.y > 0.0 && normal.blocked[kAxisY];

    const bool canStep = tuning.stepHeight > 0.0 && (blockedX || blockedZ) && wanted.y <= 0.0 &&
                         (motion.onGround || blockedDown);
    if (canStep) {
        Aabb stepped = box;
        const double up = clipAxis(stepped, kAxisY, tuning.stepHeight, obstacles);
        stepped = stepped.moved({0.0, up, 0.0});
        const double acrossX = clipAxis(stepped, kAxisX, wanted.x, obstacles);
        stepped = stepped.moved({acrossX, 0.0, 0.0});
        const double acrossZ = clipAxis(stepped, kAxisZ, wanted.z, obstacles);
        stepped = stepped.moved({0.0, 0.0, acrossZ});
        const double downWanted = wanted.y - up;
        const double down = clipAxis(stepped, kAxisY, downWanted, obstacles);

        const double stepDistance = acrossX * acrossX + acrossZ * acrossZ;
        const double normalDistance = moved.x * moved.x + moved.z * moved.z;
        if (stepDistance > normalDistance + kCollisionEpsilon * kCollisionEpsilon) {
            moved = {acrossX, up + down, acrossZ};
            blockedX = acrossX != wanted.x;
            blockedZ = acrossZ != wanted.z;
            blockedDown = downWanted < 0.0 && down != downWanted;
            blockedUp = false;
        }
    }

    motion.position += moved;
    if (blockedX) {
        velocity.x = 0.0;
    }
    if (blockedZ) {
        velocity.z = 0.0;
    }
    if (blockedDown || blockedUp) {
        velocity.y = 0.0;
    }
    motion.velocity = velocity;
    motion.onGround = blockedDown;
    return {};
}

} // namespace aurora::entity
