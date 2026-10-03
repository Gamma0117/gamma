#pragma once

#include "client/client_world.h"
#include "client/mesh_scheduler.h"
#include "client/mesh_types.h"
#include "core/constants.h"
#include "render/chunk_mesher.h"
#include "render/mesh_resources.h"
#include "render/upload_queue.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace aurora::test {

// The renderer's table without GL, with the mesh data, checking that everything it uploads is current.
struct GpuTable {
    std::unordered_map<client::SectionKey, std::pair<client::MeshKey, client::MeshData>, client::SectionKeyHash> meshes;
    int uploads = 0;
    int removes = 0;
    std::vector<client::MeshKey> taken; // Every key taken into the table (uploads and empty results), in order.

    void update(const client::ClientWorld& world, render::UploadQueue& queue)
    {
        std::erase_if(meshes, [&](const auto& item) { return !render::isDrawable(world, item.second.first); });
        for (client::ReadyMesh& ready : queue.take(world, 1 << 30)) {
            CHECK(world.isCurrent(ready.key)); // Never a stale result.
            const auto existing = meshes.find(ready.key.section);
            const std::optional<client::MeshKey> onGpu =
                existing == meshes.end() ? std::nullopt : std::optional(existing->second.first);
            switch (render::chooseMeshUpdate(ready, onGpu)) {
            case render::MeshUpdate::Upload:
                meshes[ready.key.section] = {ready.key, std::move(ready.mesh)};
                taken.push_back(ready.key);
                ++uploads;
                break;
            case render::MeshUpdate::Remove:
                meshes.erase(ready.key.section);
                taken.push_back(ready.key);
                ++removes;
                break;
            case render::MeshUpdate::Ignore:
                break;
            }
        }
    }
};

// Every section the client draws: its GPU mesh equals meshing it now from the client's snapshots, and no other
// section holds one.
inline void checkMatchesFullMesh(const client::ClientWorld& world, const GpuTable& gpu,
                                 const render::MeshResources& resources)
{
    std::size_t expectedMeshes = 0;
    for (const world::ChunkPos pos : world.eligibleChunks()) {
        const auto chunks = world.neighbourhood(pos);
        REQUIRE(chunks);
        for (std::int32_t section = 0; section < core::kSectionsPerChunk; ++section) {
            const client::SectionKey key{pos, section};
            const client::MeshData expected = render::meshSection({*chunks, section}, resources);
            const auto found = gpu.meshes.find(key);
            INFO("chunk (" << pos.x << ", " << pos.z << ") section " << section);
            if (expected.empty()) {
                CHECK(found == gpu.meshes.end());
                continue;
            }
            ++expectedMeshes;
            REQUIRE(found != gpu.meshes.end());
            CHECK(world.isCurrent(found->second.first));
            CHECK(found->second.second.vertexWords == expected.vertexWords);
            CHECK(found->second.second.indices == expected.indices);
        }
    }
    CHECK(gpu.meshes.size() == expectedMeshes);
}

} // namespace aurora::test
