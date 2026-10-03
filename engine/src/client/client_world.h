#pragma once

#include "client/mesh_types.h"
#include "core/constants.h"
#include "world/chunk_snapshot.h"
#include "world/chunk_update.h"
#include "world/coordinates.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace aurora::client {

// The client's copy of the world: the snapshots the server sent, nothing else. Main thread only. One per server
// session: generations only order the loads of one server World, so a new session needs a new ClientWorld.
//
// Render eligibility: a chunk is drawn (and meshed) only while it is within the render distance of the center
// (square distance) and all eight neighbours have arrived, because faces and corner shading at its edges depend
// on them. Before that it is simply not drawn; there is no temporary mesh. The server loads one ring further than
// the render distance, so every chunk inside it gets its neighbours.
//
// Stamps: every section has a stamp, a number from one counter that only grows. A section gets a new stamp
// whenever its mesh inputs or its eligibility change: its chunk is loaded or replaced, a neighbour arrives,
// leaves or is replaced, the chunk enters or leaves the render distance, or blocks change in it or next to it. A
// mesh made for an older stamp is stale: isCurrent() is false for it, so it is neither kept nor uploaded.
//
// Block changes (Changed): applied only to the load they name (same generation) and only if newer than what is
// held (larger revision); anything else is a duplicate or stale and ignored. The server sends every Changed of a
// load in order, so each one names the sections that differ from the one before. A mesh reads one block beyond
// its section, so each changed section and the sections around it (up to 27: the section above and below, in the
// chunk and its eight held neighbours) get a new stamp and are listed by takeChangedSections(). Eligibility does
// not change.
class ClientWorld {
public:
    explicit ClientWorld(std::int32_t renderDistance);

    std::int32_t renderDistance() const { return m_renderDistance; }
    world::ChunkPos center() const { return m_center; }

    // Applies one update from the server. A Loaded with a newer generation replaces what is held; an Unloaded
    // removes the chunk only if its generation matches; a Changed replaces the snapshot of the same load if its
    // revision is newer. Anything else is stale or a duplicate and ignored. True if the update was taken.
    bool apply(const world::ChunkUpdate& update);
    // The chunk the camera is in. Re-evaluates eligibility.
    void setCenter(world::ChunkPos center);

    std::shared_ptr<const world::ChunkSnapshot> snapshot(world::ChunkPos pos) const;
    bool isEligible(world::ChunkPos pos) const;
    // nullopt if the chunk is not held.
    std::optional<std::uint64_t> stamp(const SectionKey& key) const;
    // The key a mesh of this section would get now; nullopt unless the chunk is held and eligible.
    std::optional<MeshKey> currentKey(const SectionKey& key) const;
    // Held, same generation, eligible and same stamp.
    bool isCurrent(const MeshKey& key) const;
    // The chunk and its neighbours for meshing (see MeshInput). Only for eligible chunks; nullopt otherwise.
    std::optional<std::array<std::shared_ptr<const world::ChunkSnapshot>, 9>> neighbourhood(world::ChunkPos pos) const;

    // Chunks whose sections got new stamps since the last call (each once), from loads, unloads and eligibility.
    // The mesh scheduler reacts to these.
    std::vector<world::ChunkPos> takeChangedChunks();
    // Sections that got new stamps from block changes since the last call (each once), in held chunks.
    std::vector<SectionKey> takeChangedSections();

    std::size_t chunkCount() const { return m_chunks.size(); }
    std::size_t eligibleCount() const;
    // Every position within the render distance is held and eligible.
    bool isAreaComplete() const;
    // Eligible chunks, in no particular order.
    std::vector<world::ChunkPos> eligibleChunks() const;

private:
    struct Entry {
        std::shared_ptr<const world::ChunkSnapshot> snapshot;
        bool eligible = false;
        std::array<std::uint64_t, core::kSectionsPerChunk> stamps{};
    };

    bool computeEligible(world::ChunkPos pos) const;
    // Recomputes eligibility; new stamps if it changed or `inputsChanged`.
    void refresh(world::ChunkPos pos, bool inputsChanged);
    // refresh() for the eight neighbours, whose inputs changed.
    void refreshNeighbours(world::ChunkPos pos);
    // New stamps for the sections around every changed section of `pos`.
    void markChangedSections(world::ChunkPos pos, std::uint32_t changedSections);

    std::int32_t m_renderDistance;
    world::ChunkPos m_center;
    std::unordered_map<world::ChunkPos, Entry, world::ChunkPosHash> m_chunks;
    std::uint64_t m_lastStamp = 0;
    std::vector<world::ChunkPos> m_changed;
    std::unordered_set<world::ChunkPos, world::ChunkPosHash> m_changedSet;
    std::vector<SectionKey> m_changedSections;
    std::unordered_set<SectionKey, SectionKeyHash> m_changedSectionSet;
};

} // namespace aurora::client
