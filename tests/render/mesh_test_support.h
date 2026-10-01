#pragma once

#include "client/mesh_types.h"
#include "core/constants.h"
#include "render/mesh_resources.h"
#include "render/mesh_vertex.h"
#include "world/chunk_section.h"
#include "world/chunk_snapshot.h"

#include <array>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace aurora::test {

// States of the hand-made test table (meshResourcesForTests).
namespace state {
inline constexpr data::BlockStateId kAir = 0;
inline constexpr data::BlockStateId kUnknown = 1;
inline constexpr data::BlockStateId kStone = 2;     // Opaque, layer 1 on every face.
inline constexpr data::BlockStateId kGrass = 3;     // Opaque, down 3, up 2, sides 4.
inline constexpr data::BlockStateId kGlass = 4;     // Cutout, layer 5.
inline constexpr data::BlockStateId kOpaque5 = 5;   // Opaque, layer 5 (same texture as glass).
inline constexpr data::BlockStateId kInvisible = 6; // No faces, does not occlude.
inline constexpr data::BlockStateId kLeaves = 7;    // Cutout, layer 6.
} // namespace state

inline std::shared_ptr<const render::MeshResources> meshResourcesForTests()
{
    using render::FaceMaterial;
    const auto same = [](std::uint16_t layer) {
        std::array<std::uint16_t, 6> layers{};
        layers.fill(layer);
        return layers;
    };
    auto resources = std::make_shared<render::MeshResources>();
    resources->states = {
        {FaceMaterial::None, false, same(0)},          // air
        {FaceMaterial::Opaque, true, same(0)},         // unknown: the missing texture
        {FaceMaterial::Opaque, true, same(1)},         // stone
        {FaceMaterial::Opaque, true, {3, 2, 4, 4, 4, 4}}, // grass
        {FaceMaterial::Cutout, false, same(5)},        // glass
        {FaceMaterial::Opaque, true, same(5)},         // opaque, glass texture
        {FaceMaterial::None, false, same(0)},          // invisible
        {FaceMaterial::Cutout, false, same(6)},        // leaves
    };
    return resources;
}

// A section to mesh with its 26 neighbours, built block by block. Coordinates are relative to the origin of
// the section being meshed: x and z -16..31, y -16..31 (the sections below and above).
class TestNeighbourhood {
public:
    explicit TestNeighbourhood(std::int32_t section = 5)
        : m_section(section)
    {
    }

    void set(std::int32_t x, std::int32_t y, std::int32_t z, data::BlockStateId state)
    {
        const auto split = [](std::int32_t coordinate) {
            const std::int32_t outer = coordinate < 0 ? -1 : (coordinate >= 16 ? 1 : 0);
            return std::pair{outer, coordinate - outer * 16};
        };
        const auto [dx, lx] = split(x);
        const auto [dy, ly] = split(y);
        const auto [dz, lz] = split(z);
        sectionAt(dx, m_section + dy, dz).set(lx, ly, lz, state);
    }

    // Fills one whole section, given as chunk offset and absolute section index.
    void fill(std::int32_t dx, std::int32_t sectionIndex, std::int32_t dz, data::BlockStateId state)
    {
        sectionAt(dx, sectionIndex, dz) = world::ChunkSection::filled(state);
    }

    client::MeshInput build() const
    {
        client::MeshInput input;
        input.section = m_section;
        for (std::int32_t dz = -1; dz <= 1; ++dz) {
            for (std::int32_t dx = -1; dx <= 1; ++dx) {
                const std::size_t chunk = static_cast<std::size_t>((dz + 1) * 3 + (dx + 1));
                world::ChunkSnapshot::Sections sections;
                for (std::size_t s = 0; s < sections.size(); ++s) {
                    if (m_sections[chunk][s]) {
                        sections[s] = std::make_shared<const world::ChunkSection>(*m_sections[chunk][s]);
                    }
                }
                input.chunks[chunk] = std::make_shared<const world::ChunkSnapshot>(world::ChunkPos{dx, dz}, 1,
                                                                                  std::move(sections));
            }
        }
        return input;
    }

private:
    world::ChunkSection& sectionAt(std::int32_t dx, std::int32_t sectionIndex, std::int32_t dz)
    {
        auto& slot = m_sections[static_cast<std::size_t>((dz + 1) * 3 + (dx + 1))]
                               [static_cast<std::size_t>(sectionIndex)];
        if (!slot) {
            slot = std::make_unique<world::ChunkSection>();
        }
        return *slot;
    }

    std::int32_t m_section;
    std::array<std::array<std::unique_ptr<world::ChunkSection>, core::kSectionsPerChunk>, 9> m_sections;
};

// The mesh's vertices, decoded, four per quad in emitted order.
inline std::vector<std::array<render::MeshVertex, 4>> quadsOf(const client::MeshData& mesh)
{
    std::vector<std::array<render::MeshVertex, 4>> quads;
    for (std::size_t vertex = 0; vertex + 3 < mesh.vertexCount(); vertex += 4) {
        std::array<render::MeshVertex, 4> quad;
        for (std::size_t i = 0; i < 4; ++i) {
            quad[i] = render::unpackVertex(mesh.vertexWords[(vertex + i) * 2], mesh.vertexWords[(vertex + i) * 2 + 1]);
        }
        quads.push_back(quad);
    }
    return quads;
}

} // namespace aurora::test
