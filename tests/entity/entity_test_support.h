#pragma once

#include "data/player_movement.h"
#include "entity/collision.h"
#include "entity/collision_shapes.h"
#include "entity/player_movement.h"
#include "world/coordinates.h"

#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <tuple>
#include <utility>

namespace aurora::test {

// The shipped player/movement.json values (a test checks that the file still has them).
inline data::PlayerMovementTuning standardTuning()
{
    data::PlayerMovementTuning tuning;
    tuning.width = 0.6;
    tuning.height = 1.8;
    tuning.eyeHeight = 1.62;
    tuning.sneakEyeHeight = 1.27;
    tuning.stepHeight = 0.6;
    tuning.gravity = 32.0;
    tuning.terminalVelocity = 78.0;
    tuning.jumpVelocity = 9.8;
    tuning.walkSpeed = 4.3;
    tuning.sprintSpeed = 5.6;
    tuning.sneakSpeed = 1.3;
    tuning.groundAcceleration = 0.5;
    tuning.airAcceleration = 0.05;
    return tuning;
}

// States of the test shape table.
enum TestShape : data::BlockStateId {
    kAir = 0,
    kSolid = 1,
    kHalf = 2,  // Bottom half: y 0..0.5.
    kStair = 3, // Bottom half plus the upper half of the far side (x 0.5..1): climbed towards +x.
    kRise06 = 4,
    kRise06625 = 5, // 0.6 + 1/16.
    kShapeCount = 6,
};

inline const entity::CollisionShapes& testShapes()
{
    static const entity::CollisionShapes shapes = [] {
        entity::CollisionShapes table(kShapeCount);
        const entity::Aabb solid[] = {entity::kFullCube};
        const entity::Aabb half[] = {{{0.0, 0.0, 0.0}, {1.0, 0.5, 1.0}}};
        const entity::Aabb stair[] = {{{0.0, 0.0, 0.0}, {1.0, 0.5, 1.0}}, {{0.5, 0.5, 0.0}, {1.0, 1.0, 1.0}}};
        const entity::Aabb rise06[] = {{{0.0, 0.0, 0.0}, {1.0, 0.6, 1.0}}};
        const entity::Aabb rise06625[] = {{{0.0, 0.0, 0.0}, {1.0, 0.6625, 1.0}}};
        (void)table.setShape(kSolid, solid);
        (void)table.setShape(kHalf, half);
        (void)table.setShape(kStair, stair);
        (void)table.setShape(kRise06, rise06);
        (void)table.setShape(kRise06625, rise06625);
        return table;
    }();
    return shapes;
}

// Blocks by position, air elsewhere, solid below `floorY`. Every column is loaded except the listed ones.
class TestBlocks : public entity::BlockCollisionView {
public:
    explicit TestBlocks(std::int32_t floorY = 64)
        : m_floorY(floorY)
    {
    }

    void set(std::int32_t x, std::int32_t y, std::int32_t z, data::BlockStateId state) { m_blocks[{x, y, z}] = state; }
    void unload(world::ChunkPos pos) { m_unloaded.insert({pos.x, pos.z}); }

    bool isLoaded(world::ChunkPos pos) const override { return !m_unloaded.contains({pos.x, pos.z}); }
    data::BlockStateId blockAt(const world::BlockPos& pos) const override
    {
        if (const auto found = m_blocks.find({pos.x, pos.y, pos.z}); found != m_blocks.end()) {
            return found->second;
        }
        return pos.y < m_floorY ? kSolid : kAir;
    }

    entity::CollisionWorld world() const { return {*this, testShapes()}; }

private:
    std::int32_t m_floorY;
    std::map<std::tuple<std::int32_t, std::int32_t, std::int32_t>, data::BlockStateId> m_blocks;
    std::set<std::pair<std::int32_t, std::int32_t>> m_unloaded;
};

// A player standing still at `feet`.
inline entity::PlayerMotion standingAt(double x, double y, double z)
{
    return {.position = {x, y, z}, .velocity = {0.0, 0.0, 0.0}, .onGround = true};
}

inline entity::MovementIntent walk(std::int8_t forward, std::int8_t strafe, float yaw)
{
    return {.forward = forward, .strafe = strafe, .yaw = yaw};
}

// Steps `ticks` times with the same intent; `each` sees the motion after every tick.
inline void run(entity::PlayerMotion& motion, const entity::MovementIntent& intent, int ticks,
                const entity::CollisionWorld& world, const data::PlayerMovementTuning& tuning = standardTuning(),
                const std::function<void(const entity::PlayerMotion&)>& each = {})
{
    for (int i = 0; i < ticks; ++i) {
        entity::stepPlayer(motion, intent, tuning, world);
        if (each) {
            each(motion);
        }
    }
}

} // namespace aurora::test
