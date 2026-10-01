#pragma once

#include <glm/glm.hpp>

namespace aurora::client {

// Who owns the mouse: the game (cursor hidden, mouse turns the camera, movement keys move it) or the UI (free
// cursor). Plain state, fed with window events by the app, so it can be tested without a window.
//
// - A click captures, but only when the UI does not want the mouse (not a click on the F3 panel).
// - Esc or losing focus releases. Esc is the place of the future menu; it does not pause the server yet.
// - Every capture, release or focus change forgets the last cursor position and any collected movement, so the
//   first position after it never turns the camera.
class CursorController {
public:
    bool captured() const { return m_captured; }

    void onClick(bool uiWantsMouse);
    void onEscape();
    void onFocusChanged(bool focused);
    // An absolute cursor position, as the window reports it (also while the cursor is hidden).
    void onCursorMoved(double x, double y);

    // Movement collected since the last call, in pixels; zero unless captured.
    glm::dvec2 takeLookDelta();
    // Movement keys reach the camera only while captured and while the UI does not want the keyboard.
    bool acceptsMovement(bool uiWantsKeyboard) const { return m_captured && !uiWantsKeyboard; }

private:
    void setCaptured(bool captured);
    void forgetCursor();

    bool m_captured = false;
    bool m_hasLastPosition = false;
    glm::dvec2 m_lastPosition{0.0};
    glm::dvec2 m_delta{0.0};
};

} // namespace aurora::client
