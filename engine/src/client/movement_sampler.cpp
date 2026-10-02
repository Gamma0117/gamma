#include "client/movement_sampler.h"

#include <cstdint>

namespace aurora::client {

void MovementSampler::block()
{
    m_jumpLatched = false;
    m_blockedThisFrame = true;
}

void MovementSampler::endFrame(bool accepting, bool jumpPressed)
{
    if (accepting && m_accepting && !m_blockedThisFrame && jumpPressed) {
        m_jumpLatched = true;
    }
    if (!accepting) {
        m_jumpLatched = false;
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
    m_jumpLatched = false;
    return intent;
}

} // namespace aurora::client
