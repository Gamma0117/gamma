#include "render/chunk_renderer.h"

#include "client/camera.h"
#include "client/client_world.h"
#include "client/frustum.h"
#include "core/constants.h"
#include "core/profiler.h"

#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>

#include <optional>
#include <utility>

namespace aurora::render {

namespace {

constexpr GLuint kTextureUnit = 0;
constexpr GLsizei kVertexBytes = 8;

} // namespace

ChunkRenderer::~ChunkRenderer()
{
    shutdown();
}

bool ChunkRenderer::init(const std::filesystem::path& shaderFolder, std::span<const data::BlockTexture> textures,
                         const TextureLayers& layers, std::string& error)
{
    if (!m_textures.create(textures, layers, error)) {
        return false;
    }
    if (!m_shader.loadFiles(shaderFolder / "chunk.vert", shaderFolder / "chunk.frag", error)) {
        return false;
    }
    m_viewProjectionLocation = m_shader.uniformLocation("uViewProjection");
    m_sectionOffsetLocation = m_shader.uniformLocation("uSectionOffset");
    m_shader.use();
    glUniform1i(m_shader.uniformLocation("uBlocks"), static_cast<GLint>(kTextureUnit));

    // One vertex array for every section: attribute 0 is the two packed words, read as integers.
    glCreateVertexArrays(1, &m_vertexArray);
    glEnableVertexArrayAttrib(m_vertexArray, 0);
    glVertexArrayAttribIFormat(m_vertexArray, 0, 2, GL_UNSIGNED_INT, 0);
    glVertexArrayAttribBinding(m_vertexArray, 0, 0);
    return true;
}

void ChunkRenderer::shutdown()
{
    for (auto& [key, mesh] : m_meshes) {
        release(mesh);
    }
    m_meshes.clear();
    m_uploads.clear();
    if (m_vertexArray != 0) {
        glDeleteVertexArrays(1, &m_vertexArray);
        m_vertexArray = 0;
    }
    m_shader.destroy();
    m_textures.destroy();
}

void ChunkRenderer::queueUploads(std::vector<client::ReadyMesh> meshes)
{
    m_uploads.push(std::move(meshes));
}

void ChunkRenderer::update(const client::ClientWorld& world, std::size_t byteBudget)
{
    AURORA_PROFILE_ZONE_N("Chunk renderer update");
    std::erase_if(m_meshes, [&](auto& item) {
        if (isDrawable(world, item.second.key)) {
            return false;
        }
        release(item.second);
        return true;
    });
    for (const client::ReadyMesh& ready : m_uploads.take(world, byteBudget)) {
        const auto existing = m_meshes.find(ready.key.section);
        const std::optional<client::MeshKey> onGpu =
            existing == m_meshes.end() ? std::nullopt : std::optional(existing->second.key);
        switch (chooseMeshUpdate(ready, onGpu)) {
        case MeshUpdate::Upload:
            upload(ready);
            break;
        case MeshUpdate::Remove:
            if (existing != m_meshes.end()) {
                release(existing->second);
                m_meshes.erase(existing);
            }
            break;
        case MeshUpdate::Ignore:
            break;
        }
    }
}

void ChunkRenderer::draw(const client::Camera& camera, float aspect, float farPlane)
{
    AURORA_PROFILE_ZONE_N("Draw chunks");
    m_drawnSections = 0;
    m_drawCalls = 0;
    if (m_meshes.empty()) {
        return;
    }

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    const glm::mat4 viewProjection = camera.projection(aspect, farPlane) * camera.viewRotation();
    const client::Frustum frustum(viewProjection);
    m_shader.use();
    m_textures.bind(kTextureUnit);
    glUniformMatrix4fv(m_viewProjectionLocation, 1, GL_FALSE, glm::value_ptr(viewProjection));
    glBindVertexArray(m_vertexArray);

    for (const auto& [key, mesh] : m_meshes) {
        const glm::dvec3 origin(static_cast<double>(world::chunkOrigin(key.pos.x)),
                                static_cast<double>(world::sectionBottomY(key.section)),
                                static_cast<double>(world::chunkOrigin(key.pos.z)));
        const glm::vec3 offset = client::relativeTo(origin, camera.position());
        if (!frustum.intersects(offset, offset + glm::vec3(static_cast<float>(core::kSectionSize)))) {
            continue;
        }
        glUniform3fv(m_sectionOffsetLocation, 1, glm::value_ptr(offset));
        glVertexArrayVertexBuffer(m_vertexArray, 0, mesh.buffer, 0, kVertexBytes);
        glVertexArrayElementBuffer(m_vertexArray, mesh.buffer);
        glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT,
                       reinterpret_cast<const void*>(static_cast<std::uintptr_t>(mesh.indexOffset)));
        ++m_drawnSections;
        ++m_drawCalls;
    }
    glBindVertexArray(0);
}

ChunkRenderStats ChunkRenderer::stats() const
{
    ChunkRenderStats stats;
    stats.sections = m_meshes.size();
    stats.pendingUploads = m_uploads.size();
    for (const auto& [key, mesh] : m_meshes) {
        stats.vertices += mesh.vertices;
        stats.gpuBytes += mesh.bytes;
    }
    stats.drawnSections = m_drawnSections;
    stats.drawCalls = m_drawCalls;
    return stats;
}

void ChunkRenderer::upload(const client::ReadyMesh& ready)
{
    const std::size_t vertexBytes = ready.mesh.vertexWords.size() * sizeof(std::uint32_t);
    const std::size_t indexBytes = ready.mesh.indices.size() * sizeof(std::uint32_t);
    GpuMesh mesh;
    mesh.key = ready.key;
    mesh.indexCount = static_cast<std::int32_t>(ready.mesh.indices.size());
    mesh.indexOffset = vertexBytes;
    mesh.vertices = ready.mesh.vertexCount();
    mesh.bytes = vertexBytes + indexBytes;
    glCreateBuffers(1, &mesh.buffer);
    glNamedBufferStorage(mesh.buffer, static_cast<GLsizeiptr>(mesh.bytes), nullptr, GL_DYNAMIC_STORAGE_BIT);
    glNamedBufferSubData(mesh.buffer, 0, static_cast<GLsizeiptr>(vertexBytes), ready.mesh.vertexWords.data());
    glNamedBufferSubData(mesh.buffer, static_cast<GLintptr>(vertexBytes), static_cast<GLsizeiptr>(indexBytes),
                         ready.mesh.indices.data());

    const auto [existing, added] = m_meshes.try_emplace(ready.key.section, mesh);
    if (!added) {
        release(existing->second);
        existing->second = mesh;
    }
}

void ChunkRenderer::release(GpuMesh& mesh)
{
    if (mesh.buffer != 0) {
        glDeleteBuffers(1, &mesh.buffer);
        mesh.buffer = 0;
    }
}

} // namespace aurora::render
