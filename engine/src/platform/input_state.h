#pragma once

#include <array>
#include <cstddef>
#include <utility>

namespace aurora::platform {

enum class Key {
    Escape,
    F3,
    W,
    A,
    S,
    D,
    Space,
    LeftShift,
    LeftControl,
    Digit1, // The number row, 1 to 9 in order.
    Digit2,
    Digit3,
    Digit4,
    Digit5,
    Digit6,
    Digit7,
    Digit8,
    Digit9,
};
inline constexpr std::size_t kKeyCount = static_cast<std::size_t>(Key::Digit9) + 1; // Keep in sync.

enum class MouseButton {
    Left,
    Right,
};
inline constexpr std::size_t kMouseButtonCount = static_cast<std::size_t>(MouseButton::Right) + 1;

// A window's input events between frames, kept apart from GLFW so the rules can be tested. The window feeds it
// from its callbacks and calls startFrame() before every poll or wait.
//
// - Presses and cursor movement belong to one frame: startFrame() clears them.
// - A focus loss is kept until takeFocusLost() consumes it. Frames that only wait (a minimised window) call
//   startFrame() too, so a per-frame flag would be lost there and a captured mouse would stay captured.
// - Losing focus voids the key and mouse button presses already recorded, and presses while unfocused are not
//   recorded. One poll can hold "click, then focus lost" (even "click, lost, regained"); the click must not capture
//   the mouse right after the loss released it, and a key press (a jump) from before the loss must not reach the
//   game afterwards. Presses after focus came back count as usual. So an F3 or Esc pressed in the same poll just
//   before a focus loss is dropped too; the focus loss releases the mouse anyway.
class InputState {
public:
    void startFrame()
    {
        m_keyPressed.fill(false);
        m_buttonPressed.fill(false);
        m_cursorMoved = false;
    }

    void onKeyPressed(Key key)
    {
        if (m_focused) {
            m_keyPressed[static_cast<std::size_t>(key)] = true;
        }
    }
    void onButtonPressed(MouseButton button)
    {
        if (m_focused) {
            m_buttonPressed[static_cast<std::size_t>(button)] = true;
        }
    }
    void onCursorMoved(double x, double y)
    {
        m_cursorX = x;
        m_cursorY = y;
        m_cursorMoved = true;
    }
    void onFocus(bool focused)
    {
        m_focused = focused;
        if (!focused) {
            m_focusLost = true;
            m_keyPressed.fill(false);
            m_buttonPressed.fill(false);
        }
    }
    // The starting position, without counting as movement.
    void setCursor(double x, double y)
    {
        m_cursorX = x;
        m_cursorY = y;
    }

    bool wasKeyPressed(Key key) const { return m_keyPressed[static_cast<std::size_t>(key)]; }
    bool wasButtonPressed(MouseButton button) const { return m_buttonPressed[static_cast<std::size_t>(button)]; }
    bool cursorMoved() const { return m_cursorMoved; }
    double cursorX() const { return m_cursorX; }
    double cursorY() const { return m_cursorY; }
    bool isFocused() const { return m_focused; }
    // True once per focus loss, however many frames passed since.
    bool takeFocusLost() { return std::exchange(m_focusLost, false); }

private:
    std::array<bool, kKeyCount> m_keyPressed{};
    std::array<bool, kMouseButtonCount> m_buttonPressed{};
    double m_cursorX = 0.0;
    double m_cursorY = 0.0;
    bool m_cursorMoved = false;
    bool m_focused = true;
    bool m_focusLost = false;
};

} // namespace aurora::platform
