#pragma once

#include <array>
#include <cstddef>
#include <string>

struct GLFWwindow;

namespace aurora::platform {

struct WindowDesc {
    int width = 1280;
    int height = 720;
    std::string title = "Aurora";
    bool vsync = true;
};

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
};
inline constexpr std::size_t kKeyCount = static_cast<std::size_t>(Key::LeftControl) + 1; // Keep in sync.

enum class MouseButton {
    Left,
    Right,
};
inline constexpr std::size_t kMouseButtonCount = static_cast<std::size_t>(MouseButton::Right) + 1;

// GLFW window with an OpenGL 4.5 core context. One window per process.
class Window {
public:
    Window() = default;
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    // Creates the window, makes the context current and loads GL functions. Installs GLFW input callbacks (keys,
    // mouse buttons, cursor position, focus), so it must run before ImGuiLayer::init: ImGui's GLFW backend chains
    // to callbacks installed before it, and both see every event.
    bool create(const WindowDesc& desc);
    void destroy();

    bool shouldClose() const;
    void requestClose();

    // Both start a new input frame: the was...() queries then report events from this call only.
    void pollEvents();
    void waitEvents();
    void swapBuffers();

    void setVsync(bool enabled);
    bool isVsync() const { return m_vsync; }

    bool isKeyDown(Key key) const;
    // True if the key went down during the last pollEvents()/waitEvents(). Auto-repeat does not count.
    bool wasKeyPressed(Key key) const;
    bool wasMouseButtonPressed(MouseButton button) const { return m_buttonPressed[static_cast<std::size_t>(button)]; }

    // Latest cursor position in window pixels, and whether it changed during the last poll. While the cursor is
    // captured it keeps growing without bounds (relative movement).
    bool cursorMoved() const { return m_cursorMoved; }
    double cursorX() const { return m_cursorX; }
    double cursorY() const { return m_cursorY; }

    // Set when focus changed during the last poll: the new state.
    bool focusChanged() const { return m_focusChanged; }
    bool isFocused() const { return m_focused; }

    // Captured: the cursor is hidden and locked to the window, and movement is reported unaccelerated (raw
    // motion) where the system supports it. Released: the normal cursor.
    void setCursorCaptured(bool captured);
    bool isCursorCaptured() const { return m_cursorCaptured; }
    bool hasRawMouseMotion() const { return m_rawMouseMotion; }

    int framebufferWidth() const { return m_framebufferWidth; }
    int framebufferHeight() const { return m_framebufferHeight; }
    bool isMinimized() const { return m_framebufferWidth == 0 || m_framebufferHeight == 0; }

    GLFWwindow* nativeHandle() const { return m_handle; }

private:
    static void onFramebufferResize(GLFWwindow* handle, int width, int height);
    static void onKey(GLFWwindow* handle, int key, int scancode, int action, int mods);
    static void onMouseButton(GLFWwindow* handle, int button, int action, int mods);
    static void onCursorPos(GLFWwindow* handle, double x, double y);
    static void onFocus(GLFWwindow* handle, int focused);
    void startInputFrame();

    GLFWwindow* m_handle = nullptr;
    std::array<bool, kKeyCount> m_keyPressed{};
    std::array<bool, kMouseButtonCount> m_buttonPressed{};
    double m_cursorX = 0.0;
    double m_cursorY = 0.0;
    bool m_cursorMoved = false;
    bool m_focused = true;
    bool m_focusChanged = false;
    bool m_cursorCaptured = false;
    bool m_rawMouseMotion = false;
    int m_framebufferWidth = 0;
    int m_framebufferHeight = 0;
    bool m_vsync = true;
};

} // namespace aurora::platform
