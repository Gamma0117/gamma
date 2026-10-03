#pragma once

#include "client/mesh_types.h"
#include "world/coordinates.h"

#include <cstddef>
#include <cstdint>
#include <future>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace aurora::core {
class JobSystem;
}

namespace aurora::client {

class ClientWorld;

// A finished mesh for the renderer. An empty one is a result too: the section's current inputs have no faces, so
// whatever the renderer still holds for the section must go.
struct ReadyMesh {
    MeshKey key;
    MeshData mesh;
};

struct MeshSchedulerStats {
    std::size_t waiting = 0;  // Sections that need a mesh and have no job yet.
    std::size_t inFlight = 0; // Submitted jobs not finished yet, stale ones included.
    std::size_t ready = 0;    // Finished results (empty ones too) not yet taken by the renderer.
    std::size_t meshed = 0;   // Sections whose current mesh was handed to the renderer.
    std::size_t empty = 0;    // Sections whose current mesh has no faces (handed over to retire an older mesh).
    std::size_t failed = 0;   // Sections whose current inputs failed to mesh; not retried until they change.
    std::uint64_t stale = 0;  // Results dropped so far because their inputs changed first (never handed over).
};

// Decides which sections to mesh, runs the meshing function on the job system and hands finished meshes to the
// renderer. Main thread only.
//
// - Order: the job system is FIFO, so priority comes from here. At most `maxInFlight` jobs are submitted at a
//   time, always the waiting sections nearest the camera (square chunk distance, then vertical distance).
// - Every submitted job stays in the in-flight list until it actually finishes, even after its inputs went
//   stale, so stale jobs keep holding their slot and the limit is never exceeded.
// - Results are checked with ClientWorld::isCurrent() when collected; stale ones are dropped without waiting.
//   Dropped results (collected stale, or ready but not taken before their section changed) are counted.
// - A job that throws, or that the job system refuses (an invalid future, never waited on), marks the section
//   Failed for those inputs: one log line, no automatic retry until its stamp changes.
// - Block changes come as single sections (ClientWorld::takeChangedSections), after the chunk-level changes of the
//   same frame. Each one is forgotten and, if its chunk is eligible, meshed again. A section that is now all air
//   (null) has no faces, so it gets an empty result at once, without a job: whatever the renderer holds for it
//   goes, and if it holds nothing the empty result changes nothing.
// - Jobs capture only their MeshInput (immutable snapshots) and a copy of the meshing function, never this
//   object or the world, so destroying either with jobs in flight is safe.
class MeshScheduler {
public:
    MeshScheduler(core::JobSystem& jobs, MeshFunction meshFunction, std::size_t maxInFlight);
    ~MeshScheduler();

    MeshScheduler(const MeshScheduler&) = delete;
    MeshScheduler& operator=(const MeshScheduler&) = delete;

    // Once per frame, after the world took this frame's updates: forgets stale state, collects finished jobs and
    // submits more. `cameraSection` is the camera's section index (it may lie outside 0..23).
    void update(ClientWorld& world, world::ChunkPos cameraChunk, std::int32_t cameraSection);

    // Finished results in the order they completed, empty ones included. The renderer checks isCurrent() again
    // before it uploads a mesh or, for an empty one, drops the section's old mesh.
    std::vector<ReadyMesh> takeReady();

    MeshSchedulerStats stats() const;
    // Keys whose meshing failed since the last call (current inputs only).
    std::vector<MeshKey> takeFailures() { return std::exchange(m_failures, {}); }
    // Nothing waiting, in flight or ready, and no failures: every eligible section has its current mesh.
    bool isSettled() const;

private:
    enum class State : std::uint8_t {
        Waiting,
        InFlight,
        Ready,
        Meshed,
        Empty,
        Failed,
    };

    struct Record {
        MeshKey key;
        State state = State::Waiting;
    };

    struct Job {
        MeshKey key;
        std::future<MeshData> result;
    };

    void forgetChunk(world::ChunkPos pos);
    void addChunk(const ClientWorld& world, world::ChunkPos pos);
    void renewSection(const ClientWorld& world, const SectionKey& key);
    void collect(const ClientWorld& world);
    void submit(const ClientWorld& world, world::ChunkPos cameraChunk, std::int32_t cameraSection);
    void setState(const MeshKey& key, State state);

    core::JobSystem& m_jobs;
    MeshFunction m_meshFunction;
    std::size_t m_maxInFlight;
    std::unordered_map<SectionKey, Record, SectionKeyHash> m_records;
    std::vector<Job> m_inFlight;
    std::vector<ReadyMesh> m_ready;
    std::vector<MeshKey> m_failures;
    std::uint64_t m_staleResults = 0;
};

} // namespace aurora::client
