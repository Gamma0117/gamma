#pragma once

#include "data/block_registry.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace aurora::render {

// The frame of each block face, in BlockFace order: its outward normal and the screen right and up directions of a
// viewer outside looking at it (so textures come out upright and unmirrored). right x up = normal for every face.
struct FaceFrame {
    std::array<std::int32_t, 3> normal;
    std::array<std::int32_t, 3> right;
    std::array<std::int32_t, 3> up;
};

inline constexpr std::array<FaceFrame, data::kBlockFaceCount> kFaceFrames{{
    {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},  // Down: seen from below, right +x, up +z.
    {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}},  // Up: seen from above, right +x, up -z (north at the top).
    {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}}, // North: right -x, up +y.
    {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},   // South: right +x, up +y.
    {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},  // West: right +z, up +y.
    {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}},  // East: right -z, up +y.
}};

// How a face's texture is turned on it, in quarter turns counter-clockwise as the viewer sees it: the texture's
// right points along the face's right (0), up (1), left (2) or down (3).
inline constexpr std::uint8_t kMaxTextureRotation = 3;

} // namespace aurora::render
