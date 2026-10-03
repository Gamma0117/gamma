#include "client/movement_sampler.h"

#include <cstdint>

namespace aurora::client {

MovementSampler::MovementSampler(std::size_t paletteSize)
    : m_paletteSize(paletteSize)
{
}

void MovementSampler::block()
{
    m_jumpLatched = false;
    m_armed = false;
    m_attackTapped = false;
    m_useLatched = false;
    m_blockedThisFrame = true;
}

void MovementSampler::endFrame(bool accepting, const FramePresses& presses)
{
    if (accepting && m_accepting && !m_blockedThisFrame) {
        m_jumpLatched = m_jumpLatched || presses.jump;
        if (presses.attack) {
            m_armed = true;
            m_attackTapped = true;
        }
        m_useLatched = m_useLatched || presses.use;
        if (presses.slot && *presses.slot < m_paletteSize) {
            m_slot = *presses.slot;
        }
    }
    if (!presses.attackDown) {
        m_armed = false; // Seen up in this frame, whether or not a tick runs; a tap stays for the next tick.
    }
    if (!accepting) {
        m_jumpLatched = false;
        m_armed = false;
        m_attackTapped = false;
        m_useLatched = false;
    }
    m_accepting = accepting;
    m_blockedThisFrame = false;
}

entity::MovementIntent MovementSampler::sample(const MovementKeys& held, float yaw, float pitch)
{
    const entity::MovementIntent neutral{.yaw = yaw, .pitch = pitch};
    if (!m_accepting) {
        return neutral;
    }
    const auto axis = [](bool positive, bool negative) {
        return static_cast<std::int8_t>((positive ? 1 : 0) - (negative ? 1 : 0));
    };
    entity::MovementIntent intent = neutral;
    intent.forward = axis(held.forward, held.back);
    intent.strafe = axis(held.right, held.left);
    intent.jump = held.jump || m_jumpLatched;
    intent.sneak = held.sneak;
    intent.sprint = held.sprint;
    intent.attack = m_armed || m_attackTapped;
    intent.use = m_useLatched;
    intent.slot = m_slot;
    m_jumpLatched = false;
    m_attackTapped = false;
    m_useLatched = false;
    return intent;
}

} // namespace aurora::client
