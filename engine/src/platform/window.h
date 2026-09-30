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
};
inline constexpr std::size_t kKeyCount = static_cast<std::size_t>(Key::F3) + 1; // Keep in sync with the last Key.

// GLFW window with an OpenGL 4.5 core context. One window per process.
class Window {
public:
    Window() = default;
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    // Creates the window, makes the context current and loads GL functions. Installs GLFW input callbacks, so
    // it must run before ImGuiLayer::init: ImGui's GLFW backend chains to callbacks installed before it.
    bool create(const WindowDesc& desc);
    void destroy();

    bool shouldClose() const;
    void requestClose();

    // Both start a new input frame: wasKeyPressed() then reports presses from this call only.
    void pollEvents();
    void waitEvents();
    void swapBuffers();

    void setVsync(bool enabled);
    bool isVsync() const { return m_vsync; }

    bool isKeyDown(Key key) const;
    // True if the key went down during the last pollEvents()/waitEvents(). Auto-repeat does not count.
    bool wasKeyPressed(Key key) const;

    int framebufferWidth() const { return m_framebufferWidth; }
    int framebufferHeight() const { return m_framebufferHeight; }
    bool isMinimized() const { return m_framebufferWidth == 0 || m_framebufferHeight == 0; }

    GLFWwindow* nativeHandle() const { return m_handle; }

private:
    static void onFramebufferResize(GLFWwindow* handle, int width, int height);
    static void onKey(GLFWwindow* handle, int key, int scancode, int action, int mods);

    GLFWwindow* m_handle = nullptr;
    std::array<bool, kKeyCount> m_keyPressed{};
    int m_framebufferWidth = 0;
    int m_framebufferHeight = 0;
    bool m_vsync = true;
};

} // namespace aurora::platform
