#include "client/cursor_controller.h"

#include <utility>

namespace aurora::client {

void CursorController::onClick(bool uiWantsMouse)
{
    if (!uiWantsMouse) {
        setCaptured(true);
    }
}

void CursorController::onEscape()
{
    setCaptured(false);
}

void CursorController::onFocusChanged(bool focused)
{
    if (!focused) {
        setCaptured(false);
    }
    forgetCursor();
}

void CursorController::onCursorMoved(double x, double y)
{
    const glm::dvec2 position(x, y);
    if (m_hasLastPosition && m_captured) {
        m_delta += position - m_lastPosition;
    }
    m_lastPosition = position;
    m_hasLastPosition = true;
}

glm::dvec2 CursorController::takeLookDelta()
{
    return std::exchange(m_delta, glm::dvec2(0.0));
}

void CursorController::setCaptured(bool captured)
{
    if (captured != m_captured) {
        m_released = m_released || !captured;
        m_captured = captured;
        forgetCursor();
    }
}

void CursorController::forgetCursor()
{
    m_hasLastPosition = false;
    m_delta = glm::dvec2(0.0);
}

} // namespace aurora::client
