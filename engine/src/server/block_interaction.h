#pragma once

#include "data/block_registry.h"
#include "entity/player_messages.h"
#include "server/server_stats.h"
#include "world/coordinates.h"

#include <glm/vec3.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace aurora::data {
struct PlayerInteraction;
}

namespace aurora::entity {
class CollisionShapes;
}

namespace aurora::world {
class World;
}

namespace aurora::server {

class ServerPlayer;

// The local player's block actions on the server: mining and placing, decided by the server from its own player.
// Server thread only, like the world. tick() runs once per server tick, right after ServerPlayer::tick of that
// tick, and acts only on the input that tick applied.
//
// Each tick:
//  1. Cancelling, also on a frozen tick or one without input: a Neutralize whose range covers the last input that
//     added mining progress clears the progress (the queue may have been empty, so no neutral input carried it);
//     an applied input without attack clears it too, whether or not it uses. This comes before the tick's own
//     input is acted on, so a Neutralize and a new attack arriving together start the new attack from 1.
//  2. No applied input (starving or filling) or a frozen player: nothing more; progress that was not cancelled
//     waits.
//  3. The target: the first selectable block along the input's look from the eyes of the server's player after
//     this tick's movement, within reach (entity::raycastBlocks). It may differ from what the client highlighted.
//  4. use: one place attempt (see PlaceResult), never mining in the same tick. A placed block clears the progress;
//     a failed attempt that came with attack leaves it as it is.
//     attack (without use): a breakable block (mining ticks > 0) in the editable height. The same cell, state and
//     chunk load as before adds one tick of progress; anything else starts over at 1. At the needed ticks the
//     block becomes air and a BlockBrokenEvent is made. Anything else clears the progress.
//
// Editable height: above the bedrock layer and below the build limit (core::kBedrockY < y < core::kBuildLimitY).
class BlockInteraction {
public:
    using Clock = std::chrono::steady_clock;

    struct DigState {
        std::optional<world::BlockPos> target;
        data::BlockStateId state = data::kAirState;
        std::uint64_t generation = 0;
        std::uint32_t progress = 0;
        std::uint32_t required = 0;
        std::uint32_t lastInput = 0; // The input that added the last tick of progress.
    };

    BlockInteraction(std::shared_ptr<const data::PlayerInteraction> interaction,
                     std::shared_ptr<const data::BlockRegistry> registry,
                     std::shared_ptr<const entity::CollisionShapes> shapes);
    ~BlockInteraction();

    // `serverTick` and `now` label the blocks broken in this tick.
    void tick(world::World& world, const ServerPlayer& player, std::uint64_t serverTick, Clock::time_point now);

    const DigState& digState() const { return m_dig; }
    std::uint32_t lastPlaceInput() const { return m_lastPlaceInput; }
    entity::PlaceResult lastPlaceResult() const { return m_lastPlaceResult; }
    // Writes the mining and last-place fields of `state`.
    void describe(entity::PlayerState& state) const;
    // The blocks broken since the last call, oldest first.
    std::vector<entity::BlockBrokenEvent> takeBrokenEvents();
    BlockInteractionStats stats() const { return m_stats; }

private:
    void clearDig();
    entity::PlaceResult place(world::World& world, const ServerPlayer& player, std::uint8_t slot,
                              const world::BlockPos& hitCell, const glm::ivec3& normal);

    std::shared_ptr<const data::PlayerInteraction> m_interaction;
    std::shared_ptr<const data::BlockRegistry> m_registry;
    std::shared_ptr<const entity::CollisionShapes> m_shapes;
    std::vector<bool> m_selectable;
    // Per palette slot, the state to place against a face along x, y and z (the block's axis follows the face;
    // blocks without an axis property use their default state for all three).
    std::vector<std::array<data::BlockStateId, 3>> m_placeStates;

    DigState m_dig;
    std::uint32_t m_lastPlaceInput = 0;
    entity::PlaceResult m_lastPlaceResult = entity::PlaceResult::None;
    std::vector<entity::BlockBrokenEvent> m_broken;
    BlockInteractionStats m_stats;
};

} // namespace aurora::server
