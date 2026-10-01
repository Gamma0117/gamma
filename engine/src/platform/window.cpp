#include "platform/window.h"

#include "core/log.h"

// glad must come before GLFW so GLFW does not pull in the system GL header.
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <algorithm>

namespace aurora::platform {

namespace {

constexpr int kGlMajor = 4;
constexpr int kGlMinor = 5;

// GLFW key code for each Key, indexed by the enum value.
constexpr std::array<int, kKeyCount> kGlfwKeys{
    GLFW_KEY_ESCAPE, GLFW_KEY_F3,    GLFW_KEY_W,          GLFW_KEY_A,
    GLFW_KEY_S,      GLFW_KEY_D,     GLFW_KEY_SPACE,      GLFW_KEY_LEFT_SHIFT,
    GLFW_KEY_LEFT_CONTROL,
};

constexpr std::array<int, kMouseButtonCount> kGlfwButtons{GLFW_MOUSE_BUTTON_LEFT, GLFW_MOUSE_BUTTON_RIGHT};
static_assert(std::ranges::none_of(kGlfwKeys, [](int code) { return code == 0; }), "Every Key needs a GLFW key code");

void onGlfwError(int code, const char* description)
{
    core::logError("platform", "GLFW error {}: {}", code, description);
}

std::size_t keyIndex(Key key)
{
    return static_cast<std::size_t>(key);
}

} // namespace

Window::~Window()
{
    destroy();
}

bool Window::create(const WindowDesc& desc)
{
    if (m_handle != nullptr) {
        core::logError("platform", "Window already created");
        return false;
    }

    glfwSetErrorCallback(onGlfwError);
    if (glfwInit() != GLFW_TRUE) {
        core::logError("platform", "glfwInit failed");
        return false;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, kGlMajor);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, kGlMinor);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#ifndef NDEBUG
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
#endif

    m_handle = glfwCreateWindow(desc.width, desc.height, desc.title.c_str(), nullptr, nullptr);
    if (m_handle == nullptr) {
        core::logError("platform", "Failed to create window with OpenGL {}.{} core context", kGlMajor, kGlMinor);
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(m_handle);
    if (gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)) == 0 || GLAD_GL_VERSION_4_5 == 0) {
        core::logError("platform", "Failed to load OpenGL {}.{} functions", kGlMajor, kGlMinor);
        destroy();
        return false;
    }

    glfwSetWindowUserPointer(m_handle, this);
    glfwSetFramebufferSizeCallback(m_handle, onFramebufferResize);
    glfwSetKeyCallback(m_handle, onKey);
    glfwSetMouseButtonCallback(m_handle, onMouseButton);
    glfwSetCursorPosCallback(m_handle, onCursorPos);
    glfwSetWindowFocusCallback(m_handle, onFocus);
    glfwGetCursorPos(m_handle, &m_cursorX, &m_cursorY);
    m_focused = glfwGetWindowAttrib(m_handle, GLFW_FOCUSED) == GLFW_TRUE;
    glfwGetFramebufferSize(m_handle, &m_framebufferWidth, &m_framebufferHeight);
    glViewport(0, 0, m_framebufferWidth, m_framebufferHeight);

    setVsync(desc.vsync);
    core::logInfo("platform", "Window {}x{} created (framebuffer {}x{})", desc.width, desc.height,
                  m_framebufferWidth, m_framebufferHeight);
    return true;
}

void Window::destroy()
{
    if (m_handle == nullptr) {
        return;
    }
    glfwDestroyWindow(m_handle);
    m_handle = nullptr;
    m_framebufferWidth = 0;
    m_framebufferHeight = 0;
    glfwTerminate();
}

bool Window::shouldClose() const
{
    return m_handle == nullptr || glfwWindowShouldClose(m_handle) == GLFW_TRUE;
}

void Window::requestClose()
{
    if (m_handle != nullptr) {
        glfwSetWindowShouldClose(m_handle, GLFW_TRUE);
    }
}

void Window::pollEvents()
{
    startInputFrame();
    glfwPollEvents();
}

void Window::waitEvents()
{
    startInputFrame();
    glfwWaitEvents();
}

void Window::startInputFrame()
{
    m_keyPressed.fill(false);
    m_buttonPressed.fill(false);
    m_cursorMoved = false;
    m_focusChanged = false;
}

void Window::setCursorCaptured(bool captured)
{
    if (m_handle == nullptr || captured == m_cursorCaptured) {
        return;
    }
    m_cursorCaptured = captured;
    glfwSetInputMode(m_handle, GLFW_CURSOR, captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    // Raw motion only applies while the cursor is disabled; without it GLFW still reports relative movement.
    m_rawMouseMotion = captured && glfwRawMouseMotionSupported() == GLFW_TRUE;
    glfwSetInputMode(m_handle, GLFW_RAW_MOUSE_MOTION, m_rawMouseMotion ? GLFW_TRUE : GLFW_FALSE);
}

void Window::swapBuffers()
{
    glfwSwapBuffers(m_handle);
}

void Window::setVsync(bool enabled)
{
    m_vsync = enabled;
    glfwSwapInterval(enabled ? 1 : 0);
}

bool Window::isKeyDown(Key key) const
{
    return m_handle != nullptr && glfwGetKey(m_handle, kGlfwKeys[keyIndex(key)]) == GLFW_PRESS;
}

bool Window::wasKeyPressed(Key key) const
{
    return m_keyPressed[keyIndex(key)];
}

void Window::onFramebufferResize(GLFWwindow* handle, int width, int height)
{
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(handle));
    self->m_framebufferWidth = width;
    self->m_framebufferHeight = height;
    if (width > 0 && height > 0) {
        glViewport(0, 0, width, height);
    }
}

void Window::onMouseButton(GLFWwindow* handle, int button, int action, int /*mods*/)
{
    if (action != GLFW_PRESS) {
        return;
    }
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(handle));
    for (std::size_t i = 0; i < kMouseButtonCount; ++i) {
        if (kGlfwButtons[i] == button) {
            self->m_buttonPressed[i] = true;
        }
    }
}

void Window::onCursorPos(GLFWwindow* handle, double x, double y)
{
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(handle));
    self->m_cursorX = x;
    self->m_cursorY = y;
    self->m_cursorMoved = true;
}

void Window::onFocus(GLFWwindow* handle, int focused)
{
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(handle));
    self->m_focused = focused == GLFW_TRUE;
    self->m_focusChanged = true;
}

void Window::onKey(GLFWwindow* handle, int key, int /*scancode*/, int action, int /*mods*/)
{
    // Only the initial press counts; GLFW_REPEAT (key held down) and GLFW_RELEASE are ignored.
    if (action != GLFW_PRESS) {
        return;
    }
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(handle));
    for (std::size_t i = 0; i < kKeyCount; ++i) {
        if (kGlfwKeys[i] == key) {
            self->m_keyPressed[i] = true;
        }
    }
}

} // namespace aurora::platform
