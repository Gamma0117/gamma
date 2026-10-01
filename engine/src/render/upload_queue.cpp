#include "render/upload_queue.h"

#include "client/client_world.h"

#include <cstdint>
#include <utility>

namespace aurora::render {

void UploadQueue::push(std::vector<client::ReadyMesh> meshes)
{
    for (client::ReadyMesh& mesh : meshes) {
        m_meshes.push_back(std::move(mesh));
    }
}

std::vector<client::ReadyMesh> UploadQueue::take(const client::ClientWorld& world, std::size_t byteBudget)
{
    std::erase_if(m_meshes, [&](const client::ReadyMesh& ready) { return !world.isCurrent(ready.key); });
    std::vector<client::ReadyMesh> taken;
    std::size_t bytes = 0;
    while (!m_meshes.empty() && (taken.empty() || bytes < byteBudget)) {
        bytes += meshBytes(m_meshes.front().mesh);
        taken.push_back(std::move(m_meshes.front()));
        m_meshes.pop_front();
    }
    return taken;
}

MeshUpdate chooseMeshUpdate(const client::ReadyMesh& ready, const std::optional<client::MeshKey>& onGpu)
{
    if (onGpu && onGpu->stamp > ready.key.stamp) {
        return MeshUpdate::Ignore;
    }
    return ready.mesh.empty() ? MeshUpdate::Remove : MeshUpdate::Upload;
}

std::size_t meshBytes(const client::MeshData& mesh)
{
    return (mesh.vertexWords.size() + mesh.indices.size()) * sizeof(std::uint32_t);
}

bool isDrawable(const client::ClientWorld& world, const client::MeshKey& key)
{
    const auto snapshot = world.snapshot(key.section.pos);
    return snapshot && snapshot->generation() == key.generation && world.isEligible(key.section.pos);
}

} // namespace aurora::render
