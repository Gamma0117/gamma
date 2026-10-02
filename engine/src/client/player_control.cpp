#include "client/player_control.h"

#include "core/constants.h"
#include "core/log.h"

#include <utility>

namespace aurora::client {

PlayerControl::PlayerControl(std::shared_ptr<const data::PlayerMovementTuning> tuning,
                             const entity::CollisionWorld& world, PlayerMessageSink& sink)
    : m_world(world)
    , m_sink(sink)
    , m_player(std::move(tuning))
    , m_clock(core::kTickInterval, core::kMaxClientCatchUpTicks)
{
}

bool PlayerControl::input(const PlayerFrameInput& frame)
{
    if (frame.focusLost) {
        // A captured mouse is released here, so the release check below (or the minimised branch) blocks in this
        // call too; blocking here states the rule where it happens.
        m_cursor.onFocusChanged(false);
        block();
    }
    if (frame.minimised) {
        block(); // No ticks run while minimised; the server goes on with neutral ticks.
        return false;
    }
    if (frame.enabled) {
        if (frame.escapePressed) {
            m_cursor.onEscape();
        }
        if (frame.clickPressed) {
            m_cursor.onClick(frame.uiWantsMouse);
        }
    }
    // A release counts even if a click of the same poll captured again: the input was interrupted.
    if (m_cursor.takeReleased()) {
        block();
    }
    const bool accepting =
        frame.enabled && frame.focused && m_cursor.acceptsMovement(frame.uiWantsKeyboard) && !m_freeFlight;
    if (!accepting && m_sampler.accepting()) {
        block(); // Stopped this frame (UI keyboard); the events above block on their own.
    }
    m_sampler.endFrame(accepting, frame.jumpPressed);
    return true;
}

void PlayerControl::update(const std::optional<entity::PlayerState>& state, core::TickScheduler::TimePoint now,
                           const MovementKeys& keys, float yaw, float pitch)
{
    if (state) {
        const bool spawning = !m_player.spawned();
        m_player.receive(*state, m_world);
        if (spawning) {
            m_clock.reset(now);
            core::logInfo("client", "Player spawned at ({:.2f}, {:.2f}, {:.2f})", state->motion.position.x,
                          state->motion.position.y, state->motion.position.z);
        }
    }
    if (!m_player.spawned()) {
        return;
    }
    const std::uint32_t ticks = m_clock.advance(now).ticksToRun;
    for (std::uint32_t i = 0; i < ticks; ++i) {
        if (const std::optional<entity::PlayerInput> input =
                m_player.tick(m_sampler.sample(keys, yaw, pitch), m_world)) {
            m_sink.sendInput(*input);
        }
    }
}

void PlayerControl::setFreeFlight(bool on)
{
    m_freeFlight = on;
    if (on) {
        block(); // In this frame: the player must not keep walking until the next.
    }
}

glm::dvec3 PlayerControl::eyePosition(core::TickScheduler::TimePoint now) const
{
    const double alpha = m_clock.progress(now);
    return m_player.renderPosition(alpha) + glm::dvec3(0.0, m_player.eyeHeight(alpha), 0.0);
}

void PlayerControl::block()
{
    m_sampler.block();
    if (const std::optional<std::uint32_t> through = m_player.neutralize(m_world)) {
        m_sink.sendNeutralize(*through);
    }
}

} // namespace aurora::client
