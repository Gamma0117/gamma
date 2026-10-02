#include "server/server_player.h"

#include "core/log.h"
#include "core/profiler.h"
#include "entity/collision.h"
#include "world/chunk.h"
#include "world/world.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace aurora::server {

ServerPlayer::ServerPlayer(std::shared_ptr<const data::PlayerMovementTuning> tuning)
    : m_tuning(std::move(tuning))
{
}

void ServerPlayer::spawn(const glm::dvec3& feet)
{
    if (m_spawned) {
        return;
    }
    m_spawned = true;
    m_motion = entity::PlayerMotion{.position = feet};
    m_filling = true;
    m_waited = 0;
}

void ServerPlayer::receive(const PlayerMessage& message)
{
    if (!m_spawned) {
        ++m_staleInputs; // The client sends nothing before it saw the spawn; whatever came is ignored.
        return;
    }
    if (message.kind == PlayerMessage::Kind::Neutralize) {
        m_neutralizedThrough = std::max(m_neutralizedThrough, message.through);
        for (entity::PlayerInput& input : m_pending) {
            if (input.sequence <= m_neutralizedThrough) {
                input.intent = input.intent.neutral();
            }
        }
        return;
    }

    const entity::PlayerInput& input = message.input;
    if (input.sequence <= m_highestReceived) {
        ++m_staleInputs;
        return;
    }
    m_highestReceived = input.sequence;

    const bool validAngles = std::isfinite(input.intent.yaw) && std::isfinite(input.intent.pitch);
    entity::MovementIntent intent = entity::sanitizeIntent(input.intent, m_lastValidYaw, m_lastValidPitch);
    if (validAngles) {
        m_lastValidYaw = intent.yaw;
        m_lastValidPitch = intent.pitch;
    } else if (!m_warnedInvalidAngles) {
        m_warnedInvalidAngles = true;
        core::logWarn("server", "Player input {} has a look that is not a finite number; it is treated as no "
                                "input (later ones are not logged)",
                      input.sequence);
    }
    if (input.sequence <= m_neutralizedThrough) {
        intent = intent.neutral();
    }
    m_pending.push_back({input.sequence, intent});
}

void ServerPlayer::tick(const entity::CollisionWorld& world)
{
    if (!m_spawned) {
        return;
    }
    AURORA_PROFILE_ZONE_N("Player movement");
    while (m_pending.size() > kMaxPendingInputs) {
        m_pending.pop_front();
        ++m_droppedInputs;
    }

    entity::MovementIntent intent = m_lastIntent.neutral();
    m_lastTickInput.reset();
    if (m_pending.empty()) {
        ++m_starvedTicks;
        m_filling = true;
        m_waited = 0;
    } else if (m_filling && m_pending.size() < kFillTarget && m_waited < kFillWaitTicks) {
        ++m_primingTicks;
        ++m_waited;
    } else {
        m_filling = false;
        intent = m_pending.front().intent;
        m_lastTickInput = m_pending.front().sequence;
        m_pending.pop_front();
        m_lastIntent = intent;
    }
    m_lastTickIntent = intent;

    m_frozen = entity::stepPlayer(m_motion, intent, *m_tuning, world).frozen;
}

std::uint32_t ServerPlayer::lastInput() const
{
    return m_pending.empty() ? m_highestReceived : m_pending.front().sequence - 1;
}

entity::PlayerState ServerPlayer::state(std::uint64_t serverTick) const
{
    return {.serverTick = serverTick, .lastInput = lastInput(), .motion = m_motion, .frozen = m_frozen};
}

ServerPlayerStats ServerPlayer::stats() const
{
    return {.spawned = m_spawned,
            .frozen = m_frozen,
            .lastInput = lastInput(),
            .pendingInputs = m_pending.size(),
            .starvedTicks = m_starvedTicks,
            .primingTicks = m_primingTicks,
            .droppedInputs = m_droppedInputs,
            .staleInputs = m_staleInputs};
}

std::optional<glm::dvec3> findSpawn(const world::World& world, SpawnColumn column,
                                    const data::PlayerMovementTuning& tuning)
{
    const world::ChunkPos center = world::chunkPosOf({column.x, 0, column.z});
    for (std::int32_t dz = -1; dz <= 1; ++dz) {
        for (std::int32_t dx = -1; dx <= 1; ++dx) {
            if (world.chunk({center.x + dx, center.z + dz}) == nullptr) {
                return std::nullopt;
            }
        }
    }
    const double x = column.x + 0.5;
    const double z = column.z + 0.5;
    const entity::Aabb box = entity::playerBox({x, 0.0, z}, tuning);
    const entity::CellRange xs = entity::overlappedCells(box.min.x, box.max.x);
    const entity::CellRange zs = entity::overlappedCells(box.min.z, box.max.z);
    std::int32_t top = world::Chunk::kNoHeight;
    for (std::int32_t cellZ = zs.first; cellZ <= zs.last; ++cellZ) {
        for (std::int32_t cellX = xs.first; cellX <= xs.last; ++cellX) {
            if (const world::Chunk* chunk = world.chunk(world::chunkPosOf({cellX, 0, cellZ}))) {
                top = std::max(top, chunk->height(world::localCoord(cellX), world::localCoord(cellZ)));
            }
        }
    }
    return glm::dvec3(x, top + 1.0, z);
}

} // namespace aurora::server
