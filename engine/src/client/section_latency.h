#pragma once

#include "client/mesh_types.h"
#include "world/chunk_update.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

namespace aurora::client {

class ClientWorld;

struct SectionLatencyStats {
    std::uint64_t completed = 0;  // Every completion of the run.
    std::uint64_t canceled = 0;   // The chunk went (unloaded, reloaded) or stopped being drawn first.
    std::uint64_t failed = 0;     // Meshing failed for the section's new inputs.
    std::size_t pending = 0;      // Still waiting.
    std::uint64_t overflowed = 0; // Dropped unfinished because more were waiting than the record holds.
    // The newest `recentCount` completions (at most the tracker's `recent`), in milliseconds.
    std::size_t recentCount = 0;
    double recentMaxMs = 0.0;
    double recentMeanMs = 0.0;
    // The newest `windowCount` completions (at most the tracker's `window`): nearest-rank percentiles and maximum.
    // Older completions left the window (`windowEvicted`), so these are not percentiles of the whole run.
    std::size_t windowCount = 0;
    std::uint64_t windowEvicted = 0;
    double windowP50Ms = 0.0;
    double windowP95Ms = 0.0;
    double windowMaxMs = 0.0;
    // The largest completion of the whole run, kept as completions come.
    double runMaxMs = 0.0;
    // Completions dropped before takeCompletions() collected them (their raw values are lost).
    std::uint64_t unreportedDropped = 0;
};

// Section update latency: for every section a Changed names directly, the time from when the server published the
// change (ChunkUpdate::publishedAt, the P0 process clock) until the GPU table first takes a mesh or an empty
// result for that section, in the same load, made for the stamp ClientWorld gave the section with that change or a
// later one. Main thread only.
//
// What it means: the GPU had caught up with that change. A mesh made later includes it, so changes merged into
// one newer mesh complete together when it is taken; an older (stale) mesh never completes one. It does not mean
// the change itself was on screen (A -> B -> A may never show B), nor that the corner shading of the sections
// around it is done, nor when the frame was presented.
//
// Everything it keeps is bounded: `capacity` unfinished samples, the `window` newest completions (`recent` of
// them for the mean) and up to `maxUnreported` completions not yet taken. Percentiles of a whole measured run come
// from the raw completions: take them every frame (takeCompletions) and write them out, then compute the
// percentiles from that record afterwards; they are valid only if nothing was dropped (unreportedDropped == 0).
class SectionLatencyTracker {
public:
    using Clock = std::chrono::steady_clock;

    struct Completion {
        SectionKey section;
        Clock::time_point publishedAt{};
        Clock::time_point takenAt{};
        double milliseconds = 0.0;
    };

    static constexpr std::size_t kDefaultCapacity = 512;
    static constexpr std::size_t kDefaultRecent = 32;
    static constexpr std::size_t kDefaultWindow = 256;
    static constexpr std::size_t kDefaultMaxUnreported = 4096;

    // `capacity`: unfinished samples kept at most (the oldest go first, counted as overflowed). `recent` and
    // `window`: completions the recent mean/max and the window percentiles cover (window >= recent).
    // `maxUnreported`: completions kept for takeCompletions() (the oldest go first, counted as unreportedDropped).
    explicit SectionLatencyTracker(std::size_t capacity = kDefaultCapacity, std::size_t recent = kDefaultRecent,
                                   std::size_t window = kDefaultWindow,
                                   std::size_t maxUnreported = kDefaultMaxUnreported);

    // Only for an update ClientWorld::apply() took (returned true): a Changed starts one sample per changed
    // section. A duplicate or stale update the world ignored must not come here; nothing else tells them apart.
    void onApplied(const world::ChunkUpdate& update, const ClientWorld& world);
    // The renderer took this current result into its table (an upload, or an empty result) at `now`.
    void onGpuTaken(const MeshKey& key, Clock::time_point now);
    // Meshing failed for this current key.
    void onMeshFailed(const MeshKey& key);
    // Once per frame after the world's updates: samples whose chunk is gone, reloaded or not drawn any more are
    // canceled.
    void update(const ClientWorld& world);

    // Cheap when nothing completed since the last call: the window percentiles are only recomputed after a new
    // completion.
    SectionLatencyStats stats() const;
    // The completions since the last call, oldest first.
    std::vector<Completion> takeCompletions();

private:
    struct Sample {
        SectionKey section;
        std::uint64_t generation = 0;
        std::uint64_t stamp = 0;
        Clock::time_point publishedAt{};
    };

    void complete(const Sample& sample, Clock::time_point now);

    std::size_t m_capacity;
    std::size_t m_recentSize;
    std::size_t m_windowSize;
    std::size_t m_maxUnreported;
    std::deque<Sample> m_pending;
    std::deque<double> m_window; // Newest last.
    std::deque<Completion> m_unreported;
    std::uint64_t m_completed = 0;
    std::uint64_t m_canceled = 0;
    std::uint64_t m_failed = 0;
    std::uint64_t m_overflowed = 0;
    std::uint64_t m_windowEvicted = 0;
    std::uint64_t m_unreportedDropped = 0;
    double m_runMaxMs = 0.0;
    // The window's percentiles, recomputed by stats() only after a completion changed the window.
    mutable bool m_windowDirty = false;
    mutable double m_windowP50Ms = 0.0;
    mutable double m_windowP95Ms = 0.0;
    mutable double m_windowMaxMs = 0.0;
};

} // namespace aurora::client
