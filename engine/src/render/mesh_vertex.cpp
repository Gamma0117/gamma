#include "render/mesh_vertex.h"

#include <cassert>

namespace aurora::render {

std::array<std::uint32_t, 2> packVertex(const MeshVertex& vertex)
{
    assert(vertex.x <= kMaxVertexCoordinate && vertex.y <= kMaxVertexCoordinate &&
           vertex.z <= kMaxVertexCoordinate && vertex.u <= kMaxVertexCoordinate &&
           vertex.v <= kMaxVertexCoordinate && vertex.ao <= kMaxAo && vertex.face < 6);
    const auto bits = [](auto value, unsigned shift) { return static_cast<std::uint32_t>(value) << shift; };
    const std::uint32_t word0 = bits(vertex.x, 0) | bits(vertex.y, 5) | bits(vertex.z, 10) | bits(vertex.u, 15) |
                                bits(vertex.v, 20) | bits(vertex.ao, 25) | bits(vertex.face, 27) |
                                bits(vertex.cutout ? 1 : 0, 30);
    return {word0, static_cast<std::uint32_t>(vertex.layer)};
}

MeshVertex unpackVertex(std::uint32_t word0, std::uint32_t word1)
{
    const auto field = [word0](unsigned shift, unsigned bits) {
        return static_cast<std::uint8_t>((word0 >> shift) & ((1u << bits) - 1));
    };
    MeshVertex vertex;
    vertex.x = field(0, 5);
    vertex.y = field(5, 5);
    vertex.z = field(10, 5);
    vertex.u = field(15, 5);
    vertex.v = field(20, 5);
    vertex.ao = field(25, 2);
    vertex.face = field(27, 3);
    vertex.cutout = field(30, 1) != 0;
    vertex.layer = static_cast<std::uint16_t>(word1 & 0xffffu);
    return vertex;
}

} // namespace aurora::render
