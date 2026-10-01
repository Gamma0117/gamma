#pragma once

#include "client/mesh_scheduler.h"
#include "client/mesh_types.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

namespace aurora::client {
class ClientWorld;
}

namespace aurora::render {

// Finished meshes waiting for the GPU (CPU side of ChunkRenderer, so its rules are tested without GL).
class UploadQueue {
public:
    void push(std::vector<client::ReadyMesh> meshes);
    // First drops every queued mesh whose key is no longer current (ClientWorld::isCurrent), then takes meshes in
    // order while less than `byteBudget` bytes are taken; at least one, so a large mesh still goes. The last one
    // taken may go over the budget.
    std::vector<client::ReadyMesh> take(const client::ClientWorld& world, std::size_t byteBudget);
    std::size_t size() const { return m_meshes.size(); }
    void clear() { m_meshes.clear(); }

private:
    std::deque<client::ReadyMesh> m_meshes;
};

enum class MeshUpdate : std::uint8_t {
    Upload, // Put the mesh on the GPU, replacing whatever the section holds.
    Remove, // The section's current mesh has no faces: drop what it holds and upload nothing.
    Ignore, // The section already holds a mesh made for newer inputs (a larger stamp).
};

// What to do with a ready result that is current (taken from the queue) when the section already holds the mesh
// made for `onGpu`, if any. Stamps only grow, so a smaller stamp is older and never replaces or removes a newer mesh.
MeshUpdate chooseMeshUpdate(const client::ReadyMesh& ready, const std::optional<client::MeshKey>& onGpu);

// Bytes a mesh takes on the GPU.
std::size_t meshBytes(const client::MeshData& mesh);

// Whether a mesh already on the GPU may still be drawn: its chunk is the same load and still eligible. A newer
// stamp alone does not retire it; the newer mesh replaces it when uploaded.
bool isDrawable(const client::ClientWorld& world, const client::MeshKey& key);

} // namespace aurora::render
