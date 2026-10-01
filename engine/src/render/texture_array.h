#pragma once

#include "data/block_textures.h"
#include "render/mesh_resources.h"

#include <cstdint>
#include <span>
#include <string>

namespace aurora::render {

// One GL_TEXTURE_2D_ARRAY for every block texture, 32 x 32 RGBA8 per layer, with mipmaps. Layer 0 is the
// built-in "missing" texture (magenta and black checks); the others follow `layers`. Magnification is nearest,
// minification uses mipmaps; coordinates repeat. Requires a current GL context.
class TextureArray {
public:
    TextureArray() = default;
    ~TextureArray();

    TextureArray(const TextureArray&) = delete;
    TextureArray& operator=(const TextureArray&) = delete;

    // `textures` are the decoded images; `layers` numbers them (assignTextureLayers). Fails with `error` set if
    // the layers exceed what the driver allows (GL_MAX_ARRAY_TEXTURE_LAYERS) or the 16-bit vertex field.
    bool create(std::span<const data::BlockTexture> textures, const TextureLayers& layers, std::string& error);
    void destroy();

    void bind(std::uint32_t unit) const;
    std::uint32_t layerCount() const { return m_layerCount; }

private:
    std::uint32_t m_texture = 0;
    std::uint32_t m_layerCount = 0;
};

} // namespace aurora::render
