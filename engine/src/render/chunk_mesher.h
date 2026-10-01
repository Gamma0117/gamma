#pragma once

#include "client/mesh_types.h"
#include "render/mesh_resources.h"

#include <memory>

namespace aurora::render {

// Builds the mesh of one section (CPU only; runs on worker threads).
//
// - Input: the section plus a one-block halo around it taken from the 26 surrounding sections, across chunk and
//   section borders (18 x 18 x 18 blocks). Below the world counts as solid, so the underside of the world gets
//   no faces; above the world is air.
// - A face is made where a block with a material faces a block that does not occlude (see StateLook).
// - Greedy meshing: per face direction and slice, neighbouring faces merge into one rectangle when direction,
//   texture layer, material and all four corner AO values are equal. Texture coordinates count blocks, so the
//   texture repeats once per block across a merged rectangle.
// - Corner AO: brightness 3 - (side1 + side2 + corner) of the occluders around the corner in the layer in front
//   of the face, or 0 when both sides are occluded.
// - Each rectangle is two triangles, counter-clockwise seen from outside. Corners go bottom-left, bottom-right,
//   top-right, top-left as seen from outside (textures upright). The default diagonal joins bottom-left and
//   top-right; when those two corners are together brighter than the other two, the corners are rotated by one
//   so the diagonal runs through the darker pair, which keeps the shading symmetric.
client::MeshData meshSection(const client::MeshInput& input, const MeshResources& resources);

// A meshing function for the client's scheduler that keeps `resources` alive for as long as any copy of it
// (including queued jobs) exists.
client::MeshFunction makeChunkMesher(std::shared_ptr<const MeshResources> resources);

} // namespace aurora::render
