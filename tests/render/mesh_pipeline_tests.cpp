// The CPU path from server snapshots to the renderer's table of section meshes, with the real mesher: ClientWorld,
// MeshScheduler, UploadQueue and the same rules ChunkRenderer applies (isDrawable, chooseMeshUpdate).

#include "client/client_world.h"
#include "client/mesh_scheduler.h"
#include "core/job_system.h"
#include "render/chunk_mesher.h"
#include "render/upload_queue.h"

#include "mesh_test_support.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace aurora;
using client::MeshKey;
using client::SectionKey;
using world::ChunkPos;
namespace state = aurora::test::state;

namespace {

// A chunk whose listed sections are filled with stone; the rest is air.
world::ChunkUpdate stoneChunk(ChunkPos pos, std::uint64_t generation, std::initializer_list<std::int32_t> sections)
{
    world::ChunkSnapshot::Sections stored;
    for (const std::int32_t index : sections) {
        stored[static_cast<std::size_t>(index)] =
            std::make_shared<const world::ChunkSection>(world::ChunkSection::filled(state::kStone));
    }
    return {world::ChunkUpdate::Kind::Loaded, pos, generation,
            std::make_shared<const world::ChunkSnapshot>(pos, generation, std::move(stored))};
}

// The renderer's table of GPU meshes, without GL: which mesh each section holds.
struct GpuTable {
    std::unordered_map<SectionKey, MeshKey, client::SectionKeyHash> meshes;

    // ChunkRenderer::update's rules.
    void update(const client::ClientWorld& world, render::UploadQueue& queue)
    {
        std::erase_if(meshes, [&](const auto& item) { return !render::isDrawable(world, item.second); });
        for (const client::ReadyMesh& ready : queue.take(world, 1 << 30)) {
            const auto existing = meshes.find(ready.key.section);
            const std::optional<MeshKey> onGpu =
                existing == meshes.end() ? std::nullopt : std::optional(existing->second);
            switch (render::chooseMeshUpdate(ready, onGpu)) {
            case render::MeshUpdate::Upload:
                meshes[ready.key.section] = ready.key;
                break;
            case render::MeshUpdate::Remove:
                meshes.erase(ready.key.section);
                break;
            case render::MeshUpdate::Ignore:
                break;
            }
        }
    }
};

struct Pipeline {
    core::JobSystem jobs{2};
    client::ClientWorld world{0}; // Only (0, 0) is drawn; its eight neighbours are its halo.
    client::MeshScheduler scheduler{jobs, render::makeChunkMesher(test::meshResourcesForTests()), 16};
    render::UploadQueue queue;
    GpuTable gpu;

    void settle()
    {
        for (int round = 0; round < 20; ++round) {
            scheduler.update(world, {0, 0}, 4);
            jobs.waitIdle();
            scheduler.update(world, {0, 0}, 4);
            queue.push(scheduler.takeReady());
            gpu.update(world, queue);
            if (scheduler.isSettled() && queue.size() == 0) {
                return;
            }
        }
        FAIL("the pipeline did not settle");
    }
};

} // namespace

TEST_CASE("A section that now meshes to nothing loses its old GPU mesh", "[render][pipeline]")
{
    Pipeline pipeline;
    // Sections 3 to 5 of the middle chunk are stone, its neighbours empty: all three have faces.
    pipeline.world.apply(stoneChunk({0, 0}, 1, {3, 4, 5}));
    std::uint64_t generation = 2;
    for (std::int32_t dz = -1; dz <= 1; ++dz) {
        for (std::int32_t dx = -1; dx <= 1; ++dx) {
            if (dx != 0 || dz != 0) {
                pipeline.world.apply(stoneChunk({dx, dz}, generation++, {}));
            }
        }
    }
    pipeline.settle();
    REQUIRE(pipeline.gpu.meshes.size() == 3);
    const MeshKey before = pipeline.gpu.meshes.at({{0, 0}, 4});

    // The neighbours are replaced by stone in the same sections: the middle chunk keeps its load and eligibility
    // (only its stamps change), section 4 is now buried and meshes to nothing, 3 and 5 keep their outer faces.
    for (std::int32_t dz = -1; dz <= 1; ++dz) {
        for (std::int32_t dx = -1; dx <= 1; ++dx) {
            if (dx != 0 || dz != 0) {
                pipeline.world.apply(stoneChunk({dx, dz}, generation++, {3, 4, 5}));
            }
        }
    }
    REQUIRE(pipeline.world.isEligible({0, 0}));
    CHECK(render::isDrawable(pipeline.world, before)); // Same load: drawable until replaced or removed.
    pipeline.settle();

    CHECK(pipeline.gpu.meshes.size() == 2);
    CHECK_FALSE(pipeline.gpu.meshes.contains({{0, 0}, 4}));
    for (const auto& [section, key] : pipeline.gpu.meshes) {
        CHECK(pipeline.world.isCurrent(key));
    }
    CHECK(pipeline.scheduler.stats().empty == 1);
    CHECK(pipeline.scheduler.stats().meshed == 2);
}
