#include "render/chunk_mesher.h"

#include "core/constants.h"
#include "core/profiler.h"
#include "render/face_frames.h"
#include "render/mesh_vertex.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

namespace aurora::render {

namespace {

using data::BlockStateId;

constexpr std::int32_t kSize = core::kSectionSize;
constexpr std::int32_t kHaloSize = kSize + 2;
constexpr std::size_t kHaloVolume = static_cast<std::size_t>(kHaloSize) * kHaloSize * kHaloSize;

// Coordinates -1..16 on each axis, relative to the section.
std::size_t haloIndex(std::int32_t x, std::int32_t y, std::int32_t z)
{
    return static_cast<std::size_t>(((y + 1) * kHaloSize + (z + 1)) * kHaloSize + (x + 1));
}

struct Halo {
    std::array<BlockStateId, kHaloVolume> states{};
    std::array<bool, kHaloVolume> occluders{};
};

const StateLook& lookOf(const MeshResources& resources, BlockStateId state)
{
    // States outside the table cannot come from the server's registry; draw them like the unknown block.
    return state < resources.states.size() ? resources.states[state] : resources.states[data::kUnknownState];
}

// -1, 0 or 1: which neighbour along an axis a halo coordinate falls into.
std::int32_t neighbourOf(std::int32_t coordinate)
{
    return coordinate < 0 ? -1 : (coordinate >= kSize ? 1 : 0);
}

void fillHalo(Halo& halo, const client::MeshInput& input, const MeshResources& resources)
{
    // The 27 sections around (dy, dz, dx), null for air; `below` marks the layer under the world.
    std::array<const world::ChunkSection*, 27> sections{};
    std::array<bool, 3> below{};
    for (std::int32_t dy = -1; dy <= 1; ++dy) {
        const std::int32_t section = input.section + dy;
        below[static_cast<std::size_t>(dy + 1)] = section < 0;
        for (std::int32_t dz = -1; dz <= 1; ++dz) {
            for (std::int32_t dx = -1; dx <= 1; ++dx) {
                const world::ChunkSnapshot* chunk =
                    input.chunks[static_cast<std::size_t>((dz + 1) * 3 + (dx + 1))].get();
                assert(chunk != nullptr);
                const bool inWorld = section >= 0 && section < core::kSectionsPerChunk;
                sections[static_cast<std::size_t>(((dy + 1) * 3 + (dz + 1)) * 3 + (dx + 1))] =
                    inWorld ? chunk->section(section) : nullptr;
            }
        }
    }

    for (std::int32_t y = -1; y <= kSize; ++y) {
        const std::int32_t dy = neighbourOf(y);
        const bool belowWorld = below[static_cast<std::size_t>(dy + 1)];
        for (std::int32_t z = -1; z <= kSize; ++z) {
            const std::int32_t dz = neighbourOf(z);
            for (std::int32_t x = -1; x <= kSize; ++x) {
                const std::int32_t dx = neighbourOf(x);
                const std::size_t index = haloIndex(x, y, z);
                if (belowWorld) {
                    halo.occluders[index] = true; // Solid ground below the world: nothing faces it.
                    continue;
                }
                const world::ChunkSection* section =
                    sections[static_cast<std::size_t>(((dy + 1) * 3 + (dz + 1)) * 3 + (dx + 1))];
                if (section == nullptr) {
                    continue; // Air.
                }
                // A one-state section needs no per-cell lookup.
                const BlockStateId state = section->bitsPerEntry() == 0
                                               ? section->palette()[0]
                                               : section->get(x - dx * kSize, y - dy * kSize, z - dz * kSize);
                halo.states[index] = state;
                halo.occluders[index] = lookOf(resources, state).occludes;
            }
        }
    }
}

// One face direction. "Right" and "up" are the screen directions of a viewer outside looking at the face, so
// textures come out upright and unmirrored.
struct FaceInfo {
    std::array<std::int32_t, 3> normal;
    std::int32_t normalAxis;
    std::int32_t rightAxis;
    std::int32_t rightSign;
    std::int32_t upAxis;
    std::int32_t upSign;
};

// BlockFace order: down, up, north (-z), south (+z), west (-x), east (+x).
constexpr std::array<FaceInfo, 6> kFaces{{
    {{0, -1, 0}, 1, 0, 1, 2, 1},  // Down: seen from below, right +x, up +z.
    {{0, 1, 0}, 1, 0, 1, 2, -1},  // Up: seen from above, right +x, up -z (north at the top).
    {{0, 0, -1}, 2, 0, -1, 1, 1}, // North: right -x, up +y.
    {{0, 0, 1}, 2, 0, 1, 1, 1},   // South: right +x, up +y.
    {{-1, 0, 0}, 0, 2, 1, 1, 1},  // West: right +z, up +y.
    {{1, 0, 0}, 0, 2, -1, 1, 1},  // East: right -z, up +y.
}};

// The same frames as kFaceFrames, which the texture rotations of MeshResources are computed from.
constexpr bool matchesFaceFrames()
{
    for (std::size_t face = 0; face < kFaces.size(); ++face) {
        const FaceInfo& info = kFaces[face];
        const FaceFrame& frame = kFaceFrames[face];
        for (std::int32_t axis = 0; axis < 3; ++axis) {
            const auto a = static_cast<std::size_t>(axis);
            if (frame.normal[a] != info.normal[a] || frame.right[a] != (axis == info.rightAxis ? info.rightSign : 0) ||
                frame.up[a] != (axis == info.upAxis ? info.upSign : 0)) {
                return false;
            }
        }
    }
    return true;
}
static_assert(matchesFaceFrames());

// Corner order: bottom-left, bottom-right, top-right, top-left; signs along (right, up).
constexpr std::array<std::array<std::int32_t, 2>, 4> kCornerSigns{{{-1, -1}, {1, -1}, {1, 1}, {-1, 1}}};

// A face's merge key: bit 31 set for "a face", layer, cutout bit, 2-bit AO per corner, then the texture's quarter
// turns (0 for every face that is not turned, so their keys are as before). 0 = no face.
constexpr std::uint32_t kFaceBit = 1u << 31;

std::uint32_t makeKey(std::uint16_t layer, bool cutout, const std::array<std::uint32_t, 4>& ao, std::uint8_t rotation)
{
    return kFaceBit | layer | (cutout ? 1u << 16 : 0u) | ao[0] << 17 | ao[1] << 19 | ao[2] << 21 | ao[3] << 23 |
           static_cast<std::uint32_t>(rotation & 3u) << 25;
}

std::uint32_t keyAo(std::uint32_t key, std::size_t corner)
{
    return (key >> (17 + 2 * corner)) & 3u;
}

std::uint32_t keyRotation(std::uint32_t key)
{
    return (key >> 25) & 3u;
}

class Mesher {
public:
    Mesher(const Halo& halo, const MeshResources& resources)
        : m_halo(halo)
        , m_resources(resources)
    {
    }

    client::MeshData run()
    {
        for (std::size_t face = 0; face < kFaces.size(); ++face) {
            for (std::int32_t slice = 0; slice < kSize; ++slice) {
                meshSlice(face, slice);
            }
        }
        return std::move(m_mesh);
    }

private:
    bool occluderAt(const std::array<std::int32_t, 3>& p) const
    {
        return m_halo.occluders[haloIndex(p[0], p[1], p[2])];
    }

    std::uint32_t faceKey(std::size_t face, const std::array<std::int32_t, 3>& cell) const
    {
        const FaceInfo& info = kFaces[face];
        const BlockStateId state = m_halo.states[haloIndex(cell[0], cell[1], cell[2])];
        const StateLook& look = lookOf(m_resources, state);
        if (look.material == FaceMaterial::None) {
            return 0;
        }
        std::array<std::int32_t, 3> front = cell;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            front[axis] += info.normal[axis];
        }
        if (occluderAt(front)) {
            return 0;
        }

        std::array<std::uint32_t, 4> ao{};
        for (std::size_t corner = 0; corner < 4; ++corner) {
            std::array<std::int32_t, 3> side1 = front;
            std::array<std::int32_t, 3> side2 = front;
            side1[static_cast<std::size_t>(info.rightAxis)] += kCornerSigns[corner][0] * info.rightSign;
            side2[static_cast<std::size_t>(info.upAxis)] += kCornerSigns[corner][1] * info.upSign;
            std::array<std::int32_t, 3> diagonal = side1;
            diagonal[static_cast<std::size_t>(info.upAxis)] += kCornerSigns[corner][1] * info.upSign;
            const bool s1 = occluderAt(side1);
            const bool s2 = occluderAt(side2);
            const bool c = occluderAt(diagonal);
            ao[corner] = (s1 && s2) ? 0u : 3u - static_cast<std::uint32_t>(s1) - s2 - c;
        }
        return makeKey(look.layers[face], look.material == FaceMaterial::Cutout, ao, look.rotations[face]);
    }

    void meshSlice(std::size_t face, std::int32_t slice)
    {
        const FaceInfo& info = kFaces[face];
        // mask[b][a]: a along the right axis, b along the up axis (raw coordinates, not signs).
        std::array<std::array<std::uint32_t, kSize>, kSize> mask{};
        bool any = false;
        for (std::int32_t b = 0; b < kSize; ++b) {
            for (std::int32_t a = 0; a < kSize; ++a) {
                std::array<std::int32_t, 3> cell{};
                cell[static_cast<std::size_t>(info.normalAxis)] = slice;
                cell[static_cast<std::size_t>(info.rightAxis)] = a;
                cell[static_cast<std::size_t>(info.upAxis)] = b;
                const std::uint32_t key = faceKey(face, cell);
                mask[static_cast<std::size_t>(b)][static_cast<std::size_t>(a)] = key;
                any = any || key != 0;
            }
        }
        if (!any) {
            return;
        }

        for (std::int32_t b = 0; b < kSize; ++b) {
            for (std::int32_t a = 0; a < kSize;) {
                const std::uint32_t key = mask[static_cast<std::size_t>(b)][static_cast<std::size_t>(a)];
                if (key == 0) {
                    ++a;
                    continue;
                }
                std::int32_t width = 1;
                while (a + width < kSize &&
                       mask[static_cast<std::size_t>(b)][static_cast<std::size_t>(a + width)] == key) {
                    ++width;
                }
                std::int32_t height = 1;
                for (bool grow = true; grow && b + height < kSize;) {
                    for (std::int32_t i = 0; i < width; ++i) {
                        if (mask[static_cast<std::size_t>(b + height)][static_cast<std::size_t>(a + i)] != key) {
                            grow = false;
                            break;
                        }
                    }
                    if (grow) {
                        ++height;
                    }
                }
                for (std::int32_t row = b; row < b + height; ++row) {
                    for (std::int32_t column = a; column < a + width; ++column) {
                        mask[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)] = 0;
                    }
                }
                emitQuad(face, slice, a, b, width, height, key);
                a += width;
            }
        }
    }

    void emitQuad(std::size_t face, std::int32_t slice, std::int32_t a, std::int32_t b, std::int32_t width,
                  std::int32_t height, std::uint32_t key)
    {
        const FaceInfo& info = kFaces[face];
        const std::int32_t plane = slice + (info.normal[static_cast<std::size_t>(info.normalAxis)] > 0 ? 1 : 0);
        // Screen left/right and bottom/top edges in raw coordinates.
        const std::int32_t left = info.rightSign > 0 ? a : a + width;
        const std::int32_t right = info.rightSign > 0 ? a + width : a;
        const std::int32_t bottom = info.upSign > 0 ? b : b + height;
        const std::int32_t top = info.upSign > 0 ? b + height : b;

        struct Corner {
            std::int32_t r;
            std::int32_t t;
            std::uint8_t u;
            std::uint8_t v;
        };
        const auto w = static_cast<std::uint8_t>(width);
        const auto h = static_cast<std::uint8_t>(height);
        // Unturned, u grows to the right and v downwards from the top edge of the texture. Turned by quarter turns
        // (counter-clockwise as seen), the texture's right runs up, left or down the face instead; whole-block
        // offsets keep u and v within 0..16 without changing what is drawn, since textures repeat every block.
        const std::array<std::array<std::uint8_t, 8>, 4> kUv{{
            {0, h, w, h, w, 0, 0, 0}, // (u, v) of bottom-left, bottom-right, top-right, top-left.
            {0, 0, 0, w, h, w, h, 0},
            {w, 0, 0, 0, 0, h, w, h},
            {h, w, h, 0, 0, 0, 0, w},
        }};
        const std::array<std::uint8_t, 8>& uv = kUv[keyRotation(key)];
        const std::array<Corner, 4> corners{{
            {left, bottom, uv[0], uv[1]},
            {right, bottom, uv[2], uv[3]},
            {right, top, uv[4], uv[5]},
            {left, top, uv[6], uv[7]},
        }};

        std::array<std::array<std::uint32_t, 2>, 4> packed{};
        for (std::size_t corner = 0; corner < 4; ++corner) {
            std::array<std::int32_t, 3> position{};
            position[static_cast<std::size_t>(info.normalAxis)] = plane;
            position[static_cast<std::size_t>(info.rightAxis)] = corners[corner].r;
            position[static_cast<std::size_t>(info.upAxis)] = corners[corner].t;
            MeshVertex vertex;
            vertex.x = static_cast<std::uint8_t>(position[0]);
            vertex.y = static_cast<std::uint8_t>(position[1]);
            vertex.z = static_cast<std::uint8_t>(position[2]);
            vertex.u = corners[corner].u;
            vertex.v = corners[corner].v;
            vertex.ao = static_cast<std::uint8_t>(keyAo(key, corner));
            vertex.face = static_cast<std::uint8_t>(face);
            vertex.cutout = (key & (1u << 16)) != 0;
            vertex.layer = static_cast<std::uint16_t>(key & 0xffffu);
            packed[corner] = packVertex(vertex);
        }

        // Diagonal 0-2 unless that pair is the brighter one: then start at corner 1 (diagonal 1-3).
        const bool flip = keyAo(key, 0) + keyAo(key, 2) > keyAo(key, 1) + keyAo(key, 3);
        const auto base = static_cast<std::uint32_t>(m_mesh.vertexWords.size() / 2);
        for (std::size_t i = 0; i < 4; ++i) {
            const std::array<std::uint32_t, 2>& words = packed[(i + (flip ? 1 : 0)) % 4];
            m_mesh.vertexWords.push_back(words[0]);
            m_mesh.vertexWords.push_back(words[1]);
        }
        for (const std::uint32_t index : {0u, 1u, 2u, 0u, 2u, 3u}) {
            m_mesh.indices.push_back(base + index);
        }
    }

    const Halo& m_halo;
    const MeshResources& m_resources;
    client::MeshData m_mesh;
};

// A section of one occluding state, wrapped on all six sides by sections of one occluding state (or the ground
// below the world), has no visible face at all. Most of the ground is like this, so it skips the halo.
bool isBuried(const client::MeshInput& input, const MeshResources& resources)
{
    const auto solid = [&](const world::ChunkSection* section) {
        return section != nullptr && section->bitsPerEntry() == 0 && lookOf(resources, section->palette()[0]).occludes;
    };
    const auto at = [&](std::int32_t dx, std::int32_t dz, std::int32_t section) {
        return input.chunks[static_cast<std::size_t>((dz + 1) * 3 + (dx + 1))]->section(section);
    };
    const std::int32_t section = input.section;
    const bool belowSolid = section == 0 || solid(at(0, 0, section - 1));
    const bool aboveSolid = section + 1 < core::kSectionsPerChunk && solid(at(0, 0, section + 1));
    return solid(at(0, 0, section)) && belowSolid && aboveSolid && solid(at(-1, 0, section)) &&
           solid(at(1, 0, section)) && solid(at(0, -1, section)) && solid(at(0, 1, section));
}

} // namespace

client::MeshData meshSection(const client::MeshInput& input, const MeshResources& resources)
{
    AURORA_PROFILE_ZONE_N("Mesh section");
    const world::ChunkSection* section = input.center().section(input.section);
    if (section == nullptr || isBuried(input, resources)) {
        return {}; // All air, or nothing but solid ground around: no faces.
    }
    const auto halo = std::make_unique<Halo>(); // ~17 KB: kept off worker stacks.
    fillHalo(*halo, input, resources);
    return Mesher(*halo, resources).run();
}

client::MeshFunction makeChunkMesher(std::shared_ptr<const MeshResources> resources)
{
    return [resources = std::move(resources)](const client::MeshInput& input) {
        return meshSection(input, *resources);
    };
}

} // namespace aurora::render
