#pragma once

#include <array>
#include <cstdint>

namespace aurora::render {

// Chunk mesh vertex, 8 bytes. The same layout is unpacked in game/assets/aurora/shaders/chunk.vert; change both
// together.
//
//   word 0: bits  0-4  x       position in the section, 0..16
//           bits  5-9  y
//           bits 10-14 z
//           bits 15-19 u       texture coordinate in blocks, 0..16 (the texture repeats every block)
//           bits 20-24 v       counted down from the top edge of the texture, 0..16
//           bits 25-26 ao      corner brightness 0..3: 3 = nothing around the corner, 0 = darkest
//           bits 27-29 face    0 down, 1 up, 2 north (-z), 3 south (+z), 4 west (-x), 5 east (+x)
//           bit  30    cutout  1: discard texels with alpha < 0.5
//           bit  31    reserved, 0
//   word 1: bits  0-15 layer   texture array layer 0..65535 (layer 0 is the built-in "missing" texture)
//           bits 16-31 reserved, 0
struct MeshVertex {
    std::uint8_t x = 0;
    std::uint8_t y = 0;
    std::uint8_t z = 0;
    std::uint8_t u = 0;
    std::uint8_t v = 0;
    std::uint8_t ao = 3;
    std::uint8_t face = 0;
    bool cutout = false;
    std::uint16_t layer = 0;

    friend bool operator==(const MeshVertex&, const MeshVertex&) = default;
};

// Fields beyond their ranges are an error (asserted), never silently wrapped.
std::array<std::uint32_t, 2> packVertex(const MeshVertex& vertex);
MeshVertex unpackVertex(std::uint32_t word0, std::uint32_t word1);

inline constexpr std::uint32_t kMaxVertexCoordinate = 16;
inline constexpr std::uint32_t kMaxAo = 3;
// Layer numbers are 16-bit: at most this many layers, the built-in layer 0 included.
inline constexpr std::uint32_t kMaxTextureLayers = 1u << 16;

} // namespace aurora::render
