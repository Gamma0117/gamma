#include "render/renderer.h"

#include "core/log.h"

#include <glad/glad.h>

namespace aurora::render {

namespace {

std::string glString(GLenum name)
{
    const auto* value = reinterpret_cast<const char*>(glGetString(name));
    return value != nullptr ? value : "unknown";
}

void GLAPIENTRY onGlDebugMessage(GLenum /*source*/, GLenum /*type*/, GLuint id, GLenum severity, GLsizei /*length*/,
                                 const GLchar* message, const void* /*userParam*/)
{
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) {
        return;
    }
    const core::LogLevel level = severity == GL_DEBUG_SEVERITY_HIGH ? core::LogLevel::Error : core::LogLevel::Warn;
    core::logMessage(level, "render", "GL debug (id {}): {}", id, message);
}

} // namespace

bool Renderer::init()
{
    m_glVersion = glString(GL_VERSION);
    m_glRenderer = glString(GL_RENDERER);

    GLint flags = 0;
    glGetIntegerv(GL_CONTEXT_FLAGS, &flags);
    if ((flags & GL_CONTEXT_FLAG_DEBUG_BIT) != 0) {
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback(onGlDebugMessage, nullptr);
    }

    core::logInfo("render", "OpenGL {} | {}", m_glVersion, m_glRenderer);
    return true;
}

void Renderer::clear(const ClearColor& color)
{
    glClearColor(color.r, color.g, color.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

} // namespace aurora::render
