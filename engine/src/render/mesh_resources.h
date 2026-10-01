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

// The look of every state of `registry`. A face whose texture has no layer uses kMissingTextureLayer.
std::shared_ptr<const MeshResources> buildMeshResources(const data::BlockRegistry& registry,
                                                        const TextureLayers& layers);

} // namespace aurora::render
