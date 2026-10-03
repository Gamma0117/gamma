#pragma once

#include "client/mesh_scheduler.h"
#include "client/mesh_types.h"
#include "data/block_textures.h"
#include "render/mesh_resources.h"
#include "render/shader.h"
#include "render/texture_array.h"
#include "render/upload_queue.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace aurora::client {
class Camera;
class ClientWorld;
} // namespace aurora::client

namespace aurora::render {

struct ChunkRenderStats {
    std::size_t sections = 0;       // Section meshes on the GPU.
    std::size_t pendingUploads = 0; // Meshes waiting for their turn to upload.
    std::size_t vertices = 0;       // On the GPU.
    std::size_t gpuBytes = 0;
    std::size_t drawnSections = 0; // In the last draw, after frustum culling.
    std::size_t drawCalls = 0;
    std::uint64_t staleUploads = 0;  // Queued meshes dropped as stale so far (UploadQueue::dropped).
    std::uint64_t uploadedBytes = 0; // Mesh data uploaded so far.
};

// GPU side of the chunk meshes: one buffer per section (vertices, then uint32 indices), drawn one call per
// visible section with CPU frustum culling. Main thread only (it owns GL objects).
//
// - A queued result is used only if ClientWorld::isCurrent() still holds for its key at upload time. A mesh
//   replaces the section's GPU mesh; an empty result removes it (chooseMeshUpdate).
// - A mesh on the GPU is dropped as soon as its chunk is unloaded, reloaded (new generation) or no longer
//   eligible, so nothing is drawn for a chunk the client should not draw. A newer mesh for the same section
//   replaces it when it is uploaded.
// - Camera-relative drawing: every section's offset is (section origin - camera) computed in double, and the
//   view matrix has no translation (client::Camera).
class ChunkRenderer {
public:
    ChunkRenderer() = default;
    ~ChunkRenderer();

    ChunkRenderer(const ChunkRenderer&) = delete;
    ChunkRenderer& operator=(const ChunkRenderer&) = delete;

    // Builds the texture array and loads chunk.vert / chunk.frag from `shaderFolder`. Requires a GL context.
    bool init(const std::filesystem::path& shaderFolder, std::span<const data::BlockTexture> textures,
              const TextureLayers& layers, std::string& error);
    // Frees every GL object; call before the context goes away. Safe to repeat.
    void shutdown();

    void queueUploads(std::vector<client::ReadyMesh> meshes);
    // Drops stale GPU meshes and queued uploads, then uploads queued meshes up to `byteBudget` bytes this frame
    // (always at least one, so large meshes still get through).
    void update(const client::ClientWorld& world, std::size_t byteBudget);
    void draw(const client::Camera& camera, float aspect, float farPlane);

    std::size_t pendingUploads() const { return m_uploads.size(); }
    // The key of the mesh the section holds on the GPU, if any.
    std::optional<client::MeshKey> meshKeyOf(const client::SectionKey& section) const;
    // The keys of the results update() took into the table since the last call: uploads and empty results (also
    // when the section held nothing), in order.
    std::vector<client::MeshKey> takeTaken() { return std::exchange(m_taken, {}); }
    ChunkRenderStats stats() const;

private:
    struct GpuMesh {
        client::MeshKey key;
        std::uint32_t buffer = 0;
        std::int32_t indexCount = 0;
        std::size_t indexOffset = 0;
        std::size_t vertices = 0;
        std::size_t bytes = 0;
    };

    void upload(const client::ReadyMesh& ready);
    void release(GpuMesh& mesh);

    ShaderProgram m_shader;
    TextureArray m_textures;
    std::uint32_t m_vertexArray = 0;
    std::int32_t m_viewProjectionLocation = -1;
    std::int32_t m_sectionOffsetLocation = -1;
    std::unordered_map<client::SectionKey, GpuMesh, client::SectionKeyHash> m_meshes;
    UploadQueue m_uploads;
    std::vector<client::MeshKey> m_taken;
    std::size_t m_drawnSections = 0;
    std::size_t m_drawCalls = 0;
    std::uint64_t m_uploadedBytes = 0;
};

} // namespace aurora::render
