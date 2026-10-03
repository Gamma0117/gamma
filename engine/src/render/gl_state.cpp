#include "render/gl_state.h"

#include <glad/glad.h>

namespace aurora::render {

namespace {

GLint integer(GLenum name)
{
    GLint value = 0;
    glGetIntegerv(name, &value);
    return value;
}

void setEnabled(GLenum capability, bool enabled)
{
    if (enabled) {
        glEnable(capability);
    } else {
        glDisable(capability);
    }
}

} // namespace

GlStateSnapshot GlStateSnapshot::capture(std::uint32_t textureUnit)
{
    GlStateSnapshot state;
    state.depthTest = glIsEnabled(GL_DEPTH_TEST) == GL_TRUE;
    state.depthFunc = integer(GL_DEPTH_FUNC);
    GLboolean depthMask = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
    state.depthMask = depthMask == GL_TRUE;
    state.blend = glIsEnabled(GL_BLEND) == GL_TRUE;
    state.blendSrcRgb = integer(GL_BLEND_SRC_RGB);
    state.blendDstRgb = integer(GL_BLEND_DST_RGB);
    state.blendSrcAlpha = integer(GL_BLEND_SRC_ALPHA);
    state.blendDstAlpha = integer(GL_BLEND_DST_ALPHA);
    state.blendEquationRgb = integer(GL_BLEND_EQUATION_RGB);
    state.blendEquationAlpha = integer(GL_BLEND_EQUATION_ALPHA);
    state.cullFace = glIsEnabled(GL_CULL_FACE) == GL_TRUE;
    state.cullFaceMode = integer(GL_CULL_FACE_MODE);
    state.polygonOffsetFill = glIsEnabled(GL_POLYGON_OFFSET_FILL) == GL_TRUE;
    glGetFloatv(GL_POLYGON_OFFSET_FACTOR, &state.polygonOffsetFactor);
    glGetFloatv(GL_POLYGON_OFFSET_UNITS, &state.polygonOffsetUnits);
    state.vertexArray = integer(GL_VERTEX_ARRAY_BINDING);
    state.program = integer(GL_CURRENT_PROGRAM);
    state.activeTexture = integer(GL_ACTIVE_TEXTURE);
    state.textureUnit = textureUnit;
    glActiveTexture(GL_TEXTURE0 + textureUnit);
    state.textureArrayOnUnit = integer(GL_TEXTURE_BINDING_2D_ARRAY);
    glActiveTexture(static_cast<GLenum>(state.activeTexture));
    return state;
}

void GlStateSnapshot::restore() const
{
    setEnabled(GL_DEPTH_TEST, depthTest);
    glDepthFunc(static_cast<GLenum>(depthFunc));
    glDepthMask(depthMask ? GL_TRUE : GL_FALSE);
    setEnabled(GL_BLEND, blend);
    glBlendFuncSeparate(static_cast<GLenum>(blendSrcRgb), static_cast<GLenum>(blendDstRgb),
                        static_cast<GLenum>(blendSrcAlpha), static_cast<GLenum>(blendDstAlpha));
    glBlendEquationSeparate(static_cast<GLenum>(blendEquationRgb), static_cast<GLenum>(blendEquationAlpha));
    setEnabled(GL_CULL_FACE, cullFace);
    glCullFace(static_cast<GLenum>(cullFaceMode));
    setEnabled(GL_POLYGON_OFFSET_FILL, polygonOffsetFill);
    glPolygonOffset(polygonOffsetFactor, polygonOffsetUnits);
    glBindVertexArray(static_cast<GLuint>(vertexArray));
    glUseProgram(static_cast<GLuint>(program));
    glActiveTexture(GL_TEXTURE0 + textureUnit);
    glBindTexture(GL_TEXTURE_2D_ARRAY, static_cast<GLuint>(textureArrayOnUnit));
    glActiveTexture(static_cast<GLenum>(activeTexture));
}

} // namespace aurora::render
