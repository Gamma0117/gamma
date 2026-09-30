#include "platform/window.h"

// glad must come before GLFW so GLFW does not pull in the system GL header.
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <cstdio>

namespace aurora::platform {

namespace {

constexpr int kGlMajor = 4;
constexpr int kGlMinor = 5;

void onGlfwError(int code, const char* description)
{
    std::fprintf(stderr, "[platform] GLFW error %d: %s\n", code, description);
}

int toGlfwKey(Key key)
{
    switch (key) {
    case Key::Escape:
        return GLFW_KEY_ESCAPE;
    }
    return GLFW_KEY_UNKNOWN;
}

} // namespace

Window::~Window()
{
    destroy();
}

bool Window::create(const WindowDesc& desc)
{
    if (m_handle != nullptr) {
        std::fprintf(stderr, "[platform] Window already created\n");
        return false;
    }

    glfwSetErrorCallback(onGlfwError);
    if (glfwInit() != GLFW_TRUE) {
        std::fprintf(stderr, "[platform] glfwInit failed\n");
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
        std::fprintf(stderr, "[platform] Failed to create window with OpenGL %d.%d core context\n", kGlMajor,
                     kGlMinor);
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(m_handle);
    if (gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)) == 0 || GLAD_GL_VERSION_4_5 == 0) {
        std::fprintf(stderr, "[platform] Failed to load OpenGL %d.%d functions\n", kGlMajor, kGlMinor);
        destroy();
        return false;
    }

    glfwSetWindowUserPointer(m_handle, this);
    glfwSetFramebufferSizeCallback(m_handle, onFramebufferResize);
    glfwGetFramebufferSize(m_handle, &m_framebufferWidth, &m_framebufferHeight);
    glViewport(0, 0, m_framebufferWidth, m_framebufferHeight);

    setVsync(desc.vsync);
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
    glfwPollEvents();
}

void Window::waitEvents()
{
    glfwWaitEvents();
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
    return m_handle != nullptr && glfwGetKey(m_handle, toGlfwKey(key)) == GLFW_PRESS;
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

} // namespace aurora::platform
