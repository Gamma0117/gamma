#pragma once

#include <cstdint>

namespace aurora::render {

// The GL state a renderer may change for its own drawing and must give back: depth, blending, culling, polygon
// offset, the vertex array, the program, the active texture unit and the 2D-array texture bound on `textureUnit`.
// Requires a current GL context.
struct GlStateSnapshot {
    bool depthTest = false;
    std::int32_t depthFunc = 0;
    bool depthMask = false;
    bool blend = false;
    std::int32_t blendSrcRgb = 0;
    std::int32_t blendDstRgb = 0;
    std::int32_t blendSrcAlpha = 0;
    std::int32_t blendDstAlpha = 0;
    std::int32_t blendEquationRgb = 0;
    std::int32_t blendEquationAlpha = 0;
    bool cullFace = false;
    std::int32_t cullFaceMode = 0;
    bool polygonOffsetFill = false;
    float polygonOffsetFactor = 0.0f;
    float polygonOffsetUnits = 0.0f;
    std::int32_t vertexArray = 0;
    std::int32_t program = 0;
    std::int32_t activeTexture = 0;
    std::uint32_t textureUnit = 0;
    std::int32_t textureArrayOnUnit = 0;

    static GlStateSnapshot capture(std::uint32_t textureUnit);
    void restore() const;

    bool operator==(const GlStateSnapshot&) const = default;
};

} // namespace aurora::render
