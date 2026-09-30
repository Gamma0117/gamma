#include "render/renderer.h"

#include <glad/glad.h>

#include <cstdio>

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
    std::fprintf(stderr, "[render] GL debug (id %u): %s\n", id, message);
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

    std::printf("[render] OpenGL %s | %s\n", m_glVersion.c_str(), m_glRenderer.c_str());
    return true;
}

void Renderer::clear(const ClearColor& color)
{
    glClearColor(color.r, color.g, color.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

} // namespace aurora::render
