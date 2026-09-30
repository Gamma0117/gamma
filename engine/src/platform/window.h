#pragma once

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
};

// GLFW window with an OpenGL 4.5 core context. One window per process.
class Window {
public:
    Window() = default;
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    // Creates the window, makes the context current and loads GL functions.
    bool create(const WindowDesc& desc);
    void destroy();

    bool shouldClose() const;
    void requestClose();

    void pollEvents();
    void waitEvents();
    void swapBuffers();

    void setVsync(bool enabled);
    bool isVsync() const { return m_vsync; }

    bool isKeyDown(Key key) const;

    int framebufferWidth() const { return m_framebufferWidth; }
    int framebufferHeight() const { return m_framebufferHeight; }
    bool isMinimized() const { return m_framebufferWidth == 0 || m_framebufferHeight == 0; }

    GLFWwindow* nativeHandle() const { return m_handle; }

private:
    static void onFramebufferResize(GLFWwindow* handle, int width, int height);

    GLFWwindow* m_handle = nullptr;
    int m_framebufferWidth = 0;
    int m_framebufferHeight = 0;
    bool m_vsync = true;
};

} // namespace aurora::platform
