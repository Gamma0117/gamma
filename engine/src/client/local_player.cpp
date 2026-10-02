#include "client/local_player.h"

#include "client/movement_sampler.h"
#include "core/log.h"
#include "core/profiler.h"
#include "entity/collision.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <cmath>
#include <utility>

namespace aurora::client {

LocalPlayer::LocalPlayer(std::shared_ptr<const data::PlayerMovementTuning> tuning)
    : m_tuning(std::move(tuning))
{
}

void LocalPlayer::receive(const entity::PlayerState& state, const entity::CollisionWorld& world)
{
    if (m_base && state.serverTick <= m_base->serverTick) {
        return;
    }
    m_base = state;
    if (!m_spawned) {
        m_spawned = true;
        m_current = state.motion;
        m_previous = state.motion;
        return;
    }

    while (!m_history.empty() && m_history.front().sequence <= state.lastInput) {
        m_history.pop_front();
    }
    if (m_resyncing) {
        m_current = state.motion;
        m_previous = state.motion;
        if (state.lastInput >= m_resyncBoundary) {
            m_resyncing = false;
            core::logInfo("client", "Player prediction resumed at input {}", m_lastSent + 1);
        }
        return;
    }

    const bool settled = m_history.empty() ? state.lastInput == m_lastSent
                                           : m_history.front().sequence == state.lastInput + 1;
    if (!settled) {
        ++m_resyncs;
        m_resyncing = true;
        m_resyncBoundary = m_lastSent;
        m_history.clear();
        m_current = state.motion;
        m_previous = state.motion;
        core::logError("client", "Player inputs out of step with the server (settled {}, last sent {}); waiting for "
                                 "it to settle {} before predicting again",
                       state.lastInput, m_lastSent, m_resyncBoundary);
        if (state.lastInput >= m_resyncBoundary) {
            m_resyncing = false;
        }
        return;
    }
    replay(world, true);
}

std::optional<entity::PlayerInput> LocalPlayer::tick(const entity::MovementIntent& intent,
                                                     const entity::CollisionWorld& world)
{
    if (!m_spawned || m_resyncing) {
        hold();
        return std::nullopt;
    }
    if (m_history.size() >= kMaxHistory) {
        if (!m_paused) {
            m_paused = true;
            ++m_inputPauses;
        }
        hold();
        return std::nullopt;
    }
    m_paused = false;
    AURORA_PROFILE_ZONE_N("Player prediction");

    const entity::MovementIntent sanitized = entity::sanitizeIntent(intent, m_lastValidYaw, m_lastValidPitch);
    m_lastValidYaw = sanitized.yaw;
    m_lastValidPitch = sanitized.pitch;
    entity::PlayerMotion next = m_current;
    entity::stepPlayer(next, sanitized, *m_tuning, world);

    m_previous = m_current;
    m_previousSneak = m_currentSneak;
    m_current = next;
    m_currentSneak = sanitized.sneak;
    ++m_lastSent;
    m_history.push_back({m_lastSent, sanitized, next});
    return entity::PlayerInput{m_lastSent, sanitized};
}

std::optional<std::uint32_t> LocalPlayer::neutralize(const entity::CollisionWorld& world)
{
    if (!m_spawned || m_lastSent <= m_lastNeutralized) {
        return std::nullopt;
    }
    m_lastNeutralized = m_lastSent;
    for (Sent& sent : m_history) {
        sent.intent = sent.intent.neutral();
    }
    if (!m_resyncing && m_base) {
        replay(world, false);
    }
    return m_lastSent;
}

glm::dvec3 LocalPlayer::renderPosition(double alpha) const
{
    return glm::mix(m_previous.position, m_current.position, alpha);
}

double LocalPlayer::eyeHeight(double alpha) const
{
    const double previous = eyeFor(m_previousSneak);
    const double current = eyeFor(m_currentSneak);
    return previous + (current - previous) * alpha;
}

LocalPlayerStats LocalPlayer::stats() const
{
    return {.spawned = m_spawned,
            .lastSent = m_lastSent,
            .lastInput = m_base ? m_base->lastInput : 0,
            .serverTick = m_base ? m_base->serverTick : 0,
            .history = m_history.size(),
            .paused = m_paused,
            .resyncing = m_resyncing,
            .frozen = m_base && m_base->frozen,
            .corrections = m_corrections,
            .inputPauses = m_inputPauses,
            .resyncs = m_resyncs};
}

void LocalPlayer::replay(const entity::CollisionWorld& world, bool countCorrection)
{
    const entity::PlayerMotion before = m_current;
    entity::PlayerMotion motion = m_base->motion;
    entity::PlayerMotion previous = motion;
    bool previousSneak = false;
    bool sneak = false;
    for (Sent& sent : m_history) {
        previous = motion;
        previousSneak = sneak;
        entity::stepPlayer(motion, sent.intent, *m_tuning, world);
        sent.after = motion;
        sneak = sent.intent.sneak;
    }
    m_current = motion;
    m_previous = previous;
    m_currentSneak = sneak;
    m_previousSneak = m_history.size() >= 2 ? previousSneak : sneak;
    if (countCorrection && glm::distance(before.position, motion.position) > kCorrectionThreshold) {
        ++m_corrections;
    }
}

void LocalPlayer::hold()
{
    m_previous = m_current;
    m_previousSneak = m_currentSneak;
}

double LocalPlayer::eyeFor(bool sneaking) const
{
    return sneaking ? m_tuning->sneakEyeHeight : m_tuning->eyeHeight;
}

std::optional<std::uint32_t> blockPlayerInput(MovementSampler& sampler, LocalPlayer& player,
                                              const entity::CollisionWorld& world)
{
    sampler.block();
    return player.neutralize(world);
}

} // namespace aurora::client
