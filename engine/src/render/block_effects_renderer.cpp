#include "render/block_effects_renderer.h"

#include "client/camera.h"
#include "core/profiler.h"
#include "data/player_interaction.h"
#include "render/face_frames.h"
#include "render/gl_state.h"

#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <vector>

namespace aurora::render {

namespace {

constexpr GLsizei kCrackSize = static_cast<GLsizei>(data::kBlockTextureSize);
constexpr GLsizei kCrackMipLevels = 6; // 32 .. 1.
constexpr std::size_t kEffectFloats = 6;   // Position, then u, v, layer.
constexpr std::size_t kOutlineVertices = 24; // 12 edges.
constexpr std::size_t kCrackVertices = 36;   // 6 faces, 2 triangles each.
constexpr std::size_t kInstanceFloats = 7;   // Centre, then colour and size.
// The selection lines sit this far outside the cell, so the cell's own faces never hide them.
constexpr double kOutlineMargin = 0.002;
constexpr std::array<float, 4> kOutlineColor{0.04f, 0.04f, 0.04f, 1.0f};

void pushVertex(std::vector<float>& out, const glm::vec3& position, float u, float v, float layer)
{
    out.insert(out.end(), {position.x, position.y, position.z, u, v, layer});
}

} // namespace

BlockEffectsRenderer::~BlockEffectsRenderer()
{
    shutdown();
}

bool BlockEffectsRenderer::init(const std::filesystem::path& shaderFolder,
                                std::span<const data::RgbaImage> crackStages, std::string& error)
{
    if (crackStages.size() != data::kCrackStageCount) {
        error = std::format("expected {} crack stages, got {}", data::kCrackStageCount, crackStages.size());
        return false;
    }
    if (!m_effects.loadFiles(shaderFolder / "effects.vert", shaderFolder / "effects.frag", error) ||
        !m_particles.loadFiles(shaderFolder / "particles.vert", shaderFolder / "particles.frag", error)) {
        return false;
    }
    m_effectsViewProjection = m_effects.uniformLocation("uViewProjection");
    m_effectsTextured = m_effects.uniformLocation("uTextured");
    m_effectsColor = m_effects.uniformLocation("uColor");
    m_particlesViewProjection = m_particles.uniformLocation("uViewProjection");
    m_particlesRight = m_particles.uniformLocation("uCameraRight");
    m_particlesUp = m_particles.uniformLocation("uCameraUp");
    {
        const GlStateSnapshot state = GlStateSnapshot::capture(kCrackTextureUnit);
        m_effects.use();
        glUniform1i(m_effects.uniformLocation("uCracks"), static_cast<GLint>(kCrackTextureUnit));
        state.restore();
    }

    // Crack stages: one array layer each, rows top first (v = 0 is the top edge, as for blocks).
    glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &m_crackTexture);
    glTextureStorage3D(m_crackTexture, kCrackMipLevels, GL_RGBA8, kCrackSize, kCrackSize,
                       static_cast<GLsizei>(crackStages.size()));
    for (std::size_t stage = 0; stage < crackStages.size(); ++stage) {
        const data::RgbaImage& image = crackStages[stage];
        if (image.width != data::kBlockTextureSize || image.height != data::kBlockTextureSize) {
            error = std::format("crack stage {} is {}x{}, not {}x{}", stage, image.width, image.height, kCrackSize,
                                kCrackSize);
            return false;
        }
        glTextureSubImage3D(m_crackTexture, 0, 0, 0, static_cast<GLint>(stage), kCrackSize, kCrackSize, 1, GL_RGBA,
                            GL_UNSIGNED_BYTE, image.pixels.data());
    }
    glGenerateTextureMipmap(m_crackTexture);
    glTextureParameteri(m_crackTexture, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
    glTextureParameteri(m_crackTexture, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTextureParameteri(m_crackTexture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_crackTexture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    m_crackStages = static_cast<std::uint32_t>(crackStages.size());

    // Lines and crack faces: one small buffer rewritten each frame.
    glCreateBuffers(1, &m_effectsBuffer);
    glNamedBufferStorage(m_effectsBuffer,
                         static_cast<GLsizeiptr>((kOutlineVertices + kCrackVertices) * kEffectFloats * sizeof(float)),
                         nullptr, GL_DYNAMIC_STORAGE_BIT);
    glCreateVertexArrays(1, &m_effectsArray);
    glVertexArrayVertexBuffer(m_effectsArray, 0, m_effectsBuffer, 0,
                              static_cast<GLsizei>(kEffectFloats * sizeof(float)));
    glEnableVertexArrayAttrib(m_effectsArray, 0);
    glVertexArrayAttribFormat(m_effectsArray, 0, 3, GL_FLOAT, GL_FALSE, 0);
    glVertexArrayAttribBinding(m_effectsArray, 0, 0);
    glEnableVertexArrayAttrib(m_effectsArray, 1);
    glVertexArrayAttribFormat(m_effectsArray, 1, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float));
    glVertexArrayAttribBinding(m_effectsArray, 1, 0);

    // Fragments: a unit square as a triangle strip, and one instance record per fragment.
    constexpr std::array<float, 8> kCorners{-0.5f, -0.5f, 0.5f, -0.5f, -0.5f, 0.5f, 0.5f, 0.5f};
    glCreateBuffers(1, &m_cornerBuffer);
    glNamedBufferStorage(m_cornerBuffer, sizeof(kCorners), kCorners.data(), 0);
    glCreateBuffers(1, &m_instanceBuffer);
    glCreateVertexArrays(1, &m_particleArray);
    glVertexArrayVertexBuffer(m_particleArray, 0, m_cornerBuffer, 0, 2 * sizeof(float));
    glEnableVertexArrayAttrib(m_particleArray, 0);
    glVertexArrayAttribFormat(m_particleArray, 0, 2, GL_FLOAT, GL_FALSE, 0);
    glVertexArrayAttribBinding(m_particleArray, 0, 0);
    glVertexArrayBindingDivisor(m_particleArray, 1, 1);
    glEnableVertexArrayAttrib(m_particleArray, 1);
    glVertexArrayAttribFormat(m_particleArray, 1, 3, GL_FLOAT, GL_FALSE, 0);
    glVertexArrayAttribBinding(m_particleArray, 1, 1);
    glEnableVertexArrayAttrib(m_particleArray, 2);
    glVertexArrayAttribFormat(m_particleArray, 2, 4, GL_FLOAT, GL_FALSE, 3 * sizeof(float));
    glVertexArrayAttribBinding(m_particleArray, 2, 1);
    return true;
}

void BlockEffectsRenderer::shutdown()
{
    const auto deleteBuffer = [](std::uint32_t& buffer) {
        if (buffer != 0) {
            glDeleteBuffers(1, &buffer);
            buffer = 0;
        }
    };
    const auto deleteArray = [](std::uint32_t& array) {
        if (array != 0) {
            glDeleteVertexArrays(1, &array);
            array = 0;
        }
    };
    deleteArray(m_effectsArray);
    deleteArray(m_particleArray);
    deleteBuffer(m_effectsBuffer);
    deleteBuffer(m_cornerBuffer);
    deleteBuffer(m_instanceBuffer);
    m_instanceCapacity = 0;
    if (m_crackTexture != 0) {
        glDeleteTextures(1, &m_crackTexture);
        m_crackTexture = 0;
    }
    m_effects.destroy();
    m_particles.destroy();
}

void BlockEffectsRenderer::draw(const client::Camera& camera, float aspect, float farPlane,
                                const BlockEffectsFrame& frame)
{
    AURORA_PROFILE_ZONE_N("Draw block effects");
    m_stats = {};
    m_stats.particles = frame.particles.size();
    if (!frame.outline && !frame.crack && frame.particles.empty()) {
        return;
    }
    const GlStateSnapshot saved = GlStateSnapshot::capture(kCrackTextureUnit);
    const glm::mat4 viewRotation = camera.viewRotation();
    const glm::mat4 viewProjection = camera.projection(aspect, farPlane) * viewRotation;
    const glm::dvec3& eye = camera.position();

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(frame.ignoreDepth ? GL_ALWAYS : GL_LEQUAL);
    glDepthMask(GL_FALSE);

    if (frame.outline || frame.crack) {
        std::vector<float> vertices;
        vertices.reserve((kOutlineVertices + kCrackVertices) * kEffectFloats);
        if (frame.outline) {
            const glm::dvec3 low = glm::dvec3(frame.outline->x, frame.outline->y, frame.outline->z) - kOutlineMargin;
            const glm::dvec3 high = low + (1.0 + 2.0 * kOutlineMargin);
            const auto corner = [&](int i) {
                return client::relativeTo({(i & 1) != 0 ? high.x : low.x, (i & 2) != 0 ? high.y : low.y,
                                           (i & 4) != 0 ? high.z : low.z},
                                          eye);
            };
            // Corners differing in one bit share an edge.
            for (int a = 0; a < 8; ++a) {
                for (const int bit : {1, 2, 4}) {
                    if ((a & bit) == 0) {
                        pushVertex(vertices, corner(a), 0.0f, 0.0f, 0.0f);
                        pushVertex(vertices, corner(a | bit), 0.0f, 0.0f, 0.0f);
                    }
                }
            }
        }
        const std::size_t crackFirst = vertices.size() / kEffectFloats;
        if (frame.crack) {
            const glm::dvec3 centre =
                glm::dvec3(frame.crack->cell.x, frame.crack->cell.y, frame.crack->cell.z) + 0.5;
            const float layer = static_cast<float>(std::min(frame.crack->stage, m_crackStages - 1));
            for (const FaceFrame& face : kFaceFrames) {
                const glm::dvec3 n(face.normal[0], face.normal[1], face.normal[2]);
                const glm::dvec3 r(face.right[0], face.right[1], face.right[2]);
                const glm::dvec3 u(face.up[0], face.up[1], face.up[2]);
                const auto at = [&](double sr, double su) {
                    return client::relativeTo(centre + 0.5 * n + 0.5 * sr * r + 0.5 * su * u, eye);
                };
                // Bottom-left, bottom-right, top-right, top-left; counter-clockwise from outside.
                const glm::vec3 bl = at(-1, -1), br = at(1, -1), tr = at(1, 1), tl = at(-1, 1);
                pushVertex(vertices, bl, 0.0f, 1.0f, layer);
                pushVertex(vertices, br, 1.0f, 1.0f, layer);
                pushVertex(vertices, tr, 1.0f, 0.0f, layer);
                pushVertex(vertices, bl, 0.0f, 1.0f, layer);
                pushVertex(vertices, tr, 1.0f, 0.0f, layer);
                pushVertex(vertices, tl, 0.0f, 0.0f, layer);
            }
        }
        glNamedBufferSubData(m_effectsBuffer, 0, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)),
                             vertices.data());
        m_effects.use();
        glUniformMatrix4fv(m_effectsViewProjection, 1, GL_FALSE, glm::value_ptr(viewProjection));
        glBindVertexArray(m_effectsArray);
        if (frame.outline) {
            glDisable(GL_BLEND);
            glDisable(GL_CULL_FACE);
            glUniform1i(m_effectsTextured, 0);
            glUniform4fv(m_effectsColor, 1, kOutlineColor.data());
            glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(kOutlineVertices));
            ++m_stats.drawCalls;
        }
        if (frame.crack) {
            glEnable(GL_BLEND);
            glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
            glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            glEnable(GL_CULL_FACE);
            glCullFace(GL_BACK);
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(-1.0f, -4.0f);
            glActiveTexture(GL_TEXTURE0 + kCrackTextureUnit);
            glBindTexture(GL_TEXTURE_2D_ARRAY, m_crackTexture);
            glUniform1i(m_effectsTextured, 1);
            glDrawArrays(GL_TRIANGLES, static_cast<GLint>(crackFirst), static_cast<GLsizei>(kCrackVertices));
            ++m_stats.drawCalls;
        }
    }

    if (!frame.particles.empty()) {
        std::vector<float> instances;
        instances.reserve(frame.particles.size() * kInstanceFloats);
        for (const client::BlockParticles::Instance& particle : frame.particles) {
            const glm::vec3 centre = client::relativeTo(particle.position, eye);
            instances.insert(instances.end(), {centre.x, centre.y, centre.z, particle.colour[0] / 255.0f,
                                               particle.colour[1] / 255.0f, particle.colour[2] / 255.0f,
                                               particle.size});
        }
        if (frame.particles.size() > m_instanceCapacity) {
            m_instanceCapacity = frame.particles.size() * 2;
            glNamedBufferData(m_instanceBuffer,
                              static_cast<GLsizeiptr>(m_instanceCapacity * kInstanceFloats * sizeof(float)), nullptr,
                              GL_STREAM_DRAW);
        }
        glNamedBufferSubData(m_instanceBuffer, 0, static_cast<GLsizeiptr>(instances.size() * sizeof(float)),
                             instances.data());
        glVertexArrayVertexBuffer(m_particleArray, 1, m_instanceBuffer, 0,
                                  static_cast<GLsizei>(kInstanceFloats * sizeof(float)));
        glDisable(GL_BLEND);
        glDisable(GL_CULL_FACE);
        glDisable(GL_POLYGON_OFFSET_FILL);
        m_particles.use();
        glUniformMatrix4fv(m_particlesViewProjection, 1, GL_FALSE, glm::value_ptr(viewProjection));
        // The camera's right and up in world space: the first two rows of the view rotation.
        const glm::vec3 right(viewRotation[0][0], viewRotation[1][0], viewRotation[2][0]);
        const glm::vec3 up(viewRotation[0][1], viewRotation[1][1], viewRotation[2][1]);
        glUniform3fv(m_particlesRight, 1, glm::value_ptr(right));
        glUniform3fv(m_particlesUp, 1, glm::value_ptr(up));
        glBindVertexArray(m_particleArray);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, static_cast<GLsizei>(frame.particles.size()));
        ++m_stats.drawCalls;
    }
    saved.restore();
}

} // namespace aurora::render
