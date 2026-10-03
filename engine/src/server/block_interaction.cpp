#include "server/block_interaction.h"

#include "core/constants.h"
#include "core/log.h"
#include "core/profiler.h"
#include "data/player_interaction.h"
#include "entity/aabb.h"
#include "entity/block_raycast.h"
#include "entity/collision_shapes.h"
#include "server/server_player.h"
#include "server/world_collision_view.h"
#include "world/world.h"

#include <cassert>
#include <cstdint>
#include <format>
#include <limits>
#include <string>
#include <utility>

namespace aurora::server {

namespace {

bool isEditableHeight(std::int64_t y)
{
    return y > core::kBedrockY && y < core::kBuildLimitY;
}

// The state of `block` with its axis property at `axis` ("x", "y" or "z"), or its default state when it has no such
// property or value.
data::BlockStateId stateWithAxis(const data::BlockRegistry& registry, const data::BlockDefinition& block,
                                 const char* axis)
{
    for (const data::BlockProperty& property : block.properties) {
        if (property.name != "axis") {
            continue;
        }
        const data::BlockRegistry::ParseResult parsed =
            registry.parseState(std::format("{}[axis={}]", block.id.str(), axis));
        return parsed.state.value_or(block.defaultState);
    }
    return block.defaultState;
}

} // namespace

BlockInteraction::BlockInteraction(std::shared_ptr<const data::PlayerInteraction> interaction,
                                   std::shared_ptr<const data::BlockRegistry> registry,
                                   std::shared_ptr<const entity::CollisionShapes> shapes)
    : m_interaction(std::move(interaction))
    , m_registry(std::move(registry))
    , m_shapes(std::move(shapes))
    , m_selectable(entity::selectableStates(*m_registry))
{
    assert(m_interaction && m_registry && m_shapes);
    assert(m_interaction->miningTicks.size() == m_registry->stateCount());
    for (const data::BlockStateId state : m_interaction->paletteStates) {
        const data::BlockDefinition& block = m_registry->blockOf(state);
        m_placeStates.push_back({stateWithAxis(*m_registry, block, "x"), stateWithAxis(*m_registry, block, "y"),
                                 stateWithAxis(*m_registry, block, "z")});
    }
}

BlockInteraction::~BlockInteraction() = default;

void BlockInteraction::tick(world::World& world, const ServerPlayer& player, std::uint64_t serverTick,
                            Clock::time_point now)
{
    AURORA_PROFILE_ZONE_N("Block interaction");
    // 1. Cancelling, before anything this tick's input does, frozen or not.
    if (m_dig.target && m_dig.lastInput <= player.neutralizedThrough()) {
        clearDig();
    }
    const std::optional<std::uint32_t> input = player.lastTickInput();
    const entity::MovementIntent& intent = player.lastTickIntent();
    if (input && !intent.attack) {
        clearDig();
    }
    // 2. Nothing to act on.
    if (!input || player.state(serverTick).frozen || (!intent.attack && !intent.use)) {
        return;
    }

    // 3. The target, from the server's own player after this tick's movement.
    const WorldCollisionView view(world);
    const entity::RaycastResult hit = entity::raycastBlocks(
        view, m_selectable, entity::eyePosition(player.motion(), player.tuning()),
        entity::lookDirection(static_cast<double>(intent.yaw), static_cast<double>(intent.pitch)),
        m_interaction->tuning.reach);

    // 4. Place, or mine.
    if (intent.use) {
        ++m_stats.placeAttempts;
        m_lastPlaceInput = *input;
        m_lastPlaceResult = hit.status == entity::RaycastStatus::Hit
                                ? place(world, player, intent.slot, hit.cell, hit.normal)
                                : entity::PlaceResult::NoTarget;
        core::logDebug("server", "Place by input {} with palette key {}: {}", *input, intent.slot + 1,
                       entity::placeResultName(m_lastPlaceResult));
        if (m_lastPlaceResult == entity::PlaceResult::Applied) {
            ++m_stats.blocksPlaced;
            clearDig();
        }
        return;
    }

    const bool hitBlock = hit.status == entity::RaycastStatus::Hit && hit.state < m_interaction->miningTicks.size();
    const std::uint32_t required = hitBlock ? m_interaction->miningTicks[hit.state] : 0;
    if (required == 0 || !isEditableHeight(hit.cell.y)) {
        clearDig();
        return;
    }
    const std::uint64_t generation = world.generation(world::chunkPosOf(hit.cell)).value_or(0);
    const bool same = m_dig.target && *m_dig.target == hit.cell && m_dig.state == hit.state &&
                      m_dig.generation == generation;
    if (!same) {
        m_dig = DigState{.target = hit.cell, .state = hit.state, .generation = generation, .required = required};
        core::logDebug("server", "Mining {} at ({}, {}, {}) from input {}", m_registry->stateToString(hit.state),
                       hit.cell.x, hit.cell.y, hit.cell.z, *input);
    }
    ++m_dig.progress;
    m_dig.lastInput = *input;
    if (m_dig.progress < m_dig.required) {
        return;
    }
    if (world.setBlock(hit.cell, data::kAirState)) {
        m_broken.push_back({.position = hit.cell,
                            .previousState = hit.state,
                            .generation = generation,
                            .serverTick = serverTick,
                            .occurredAt = now});
        ++m_stats.blocksBroken;
        core::logDebug("server", "Broke {} at ({}, {}, {}) on tick {} after {} ticks of attack",
                       m_registry->stateToString(hit.state), hit.cell.x, hit.cell.y, hit.cell.z, serverTick,
                       m_dig.required);
    }
    clearDig();
}

entity::PlaceResult BlockInteraction::place(world::World& world, const ServerPlayer& player, std::uint8_t slot,
                                            const world::BlockPos& hitCell, const glm::ivec3& normal)
{
    if (slot >= m_placeStates.size()) {
        return entity::PlaceResult::InvalidSlot;
    }
    // The cell in front of the face, in 64 bits until it is known to be a block coordinate.
    const std::int64_t x = static_cast<std::int64_t>(hitCell.x) + normal.x;
    const std::int64_t y = static_cast<std::int64_t>(hitCell.y) + normal.y;
    const std::int64_t z = static_cast<std::int64_t>(hitCell.z) + normal.z;
    if (!isEditableHeight(y)) {
        return entity::PlaceResult::Height;
    }
    constexpr std::int64_t kMin = std::numeric_limits<std::int32_t>::min();
    constexpr std::int64_t kMax = std::numeric_limits<std::int32_t>::max();
    if (x < kMin || x > kMax || z < kMin || z > kMax) {
        return entity::PlaceResult::Unloaded; // No chunk holds it.
    }
    const world::BlockPos target{static_cast<std::int32_t>(x), static_cast<std::int32_t>(y),
                                 static_cast<std::int32_t>(z)};
    const std::optional<data::BlockStateId> existing = world.getBlock(target);
    if (!existing) {
        return entity::PlaceResult::Unloaded;
    }
    if (*existing != data::kAirState) {
        return entity::PlaceResult::Occupied;
    }

    // Against a face along x, y or z, a block with an axis lies along that axis.
    const int axis = normal.x != 0 ? 0 : (normal.y != 0 ? 1 : 2);
    const data::BlockStateId state = m_placeStates[slot][static_cast<std::size_t>(axis)];
    const entity::Aabb playerBox = entity::playerBox(player.motion().position, player.tuning());
    const glm::dvec3 offset(target.x, target.y, target.z);
    for (const entity::Aabb& box : m_shapes->boxes(state)) {
        if (entity::overlaps(box.moved(offset), playerBox)) {
            return entity::PlaceResult::BlocksPlayer; // Touching faces are fine; only a real overlap blocks.
        }
    }
    return world.setBlock(target, state) ? entity::PlaceResult::Applied : entity::PlaceResult::Unloaded;
}

void BlockInteraction::describe(entity::PlayerState& state) const
{
    state.digTarget = m_dig.target;
    state.digGeneration = m_dig.target ? m_dig.generation : 0;
    state.digProgress = m_dig.target ? m_dig.progress : 0;
    state.digRequired = m_dig.target ? m_dig.required : 0;
    state.lastPlaceInput = m_lastPlaceInput;
    state.lastPlaceResult = m_lastPlaceResult;
}

std::vector<entity::BlockBrokenEvent> BlockInteraction::takeBrokenEvents()
{
    return std::exchange(m_broken, {});
}

void BlockInteraction::clearDig()
{
    m_dig = DigState{};
}

} // namespace aurora::server
