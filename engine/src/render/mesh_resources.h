#pragma once

#include "data/block_registry.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace aurora::render {

// How a block state's faces are drawn.
enum class FaceMaterial : std::uint8_t {
    None,   // No faces at all (air and other invisible blocks).
    Opaque, // Solid texels.
    Cutout, // Texels with alpha < 0.5 are discarded. Translucent blocks are drawn like this until P0-10.
};

struct StateLook {
    FaceMaterial material = FaceMaterial::None;
    // Hides the faces of blocks next to it and darkens corners (ambient occlusion). Only opaque blocks do; air,
    // invisible, cutout and translucent blocks do not.
    bool occludes = false;
    std::array<std::uint16_t, data::kBlockFaceCount> layers{}; // Texture array layer per face (BlockFace order).
    // Quarter turns of each face's texture (see kFaceFrames): 0 except for the faces of turned axis states.
    std::array<std::uint8_t, data::kBlockFaceCount> rotations{};
};

// Everything the mesher needs to know about blocks, by state id. Immutable once built; meshing jobs share it.
struct MeshResources {
    std::vector<StateLook> states;
};

// Layer 0 is the built-in "missing" texture; the given textures follow in order from 1.
inline constexpr std::uint16_t kMissingTextureLayer = 0;

struct TextureLayers {
    std::map<std::string, std::uint16_t, std::less<>> layerOf; // By texture id.
    std::uint32_t layerCount = 1;                              // Including layer 0.
};

// Numbers `textureIds` 1, 2, ... in the given order. nullopt if they do not fit the 16-bit layer field
// (kMaxTextureLayers, layer 0 included).
std::optional<TextureLayers> assignTextureLayers(std::span<const std::string> textureIds);

// Where a face of an axis block takes its texture from: a face of the block's own model and the quarter turns.
struct FaceSource {
    data::BlockFace face = data::BlockFace::Down;
    std::uint8_t rotation = 0;
};

// Blocks with an `axis` property (values x, y, z) are modelled along y: their textures are for axis=y. The axis=x
// state is that model turned about Z by -90 degrees ((x, y, z) -> (y, -x, z)) and axis=z turned about X by +90
// degrees ((x, y, z) -> (x, -z, y)), so the ends go east/west or south/north. For each face of the turned state
// (BlockFace order), the model face that lands there and how its texture is turned. `axis` 0 x, 1 y, 2 z.
std::array<FaceSource, data::kBlockFaceCount> axisFaceSources(int axis);

// The look of every state of `registry`. A face whose texture has no layer uses kMissingTextureLayer. States of a
// block with an axis property take their faces from axisFaceSources.
std::shared_ptr<const MeshResources> buildMeshResources(const data::BlockRegistry& registry,
                                                        const TextureLayers& layers);

} // namespace aurora::render
