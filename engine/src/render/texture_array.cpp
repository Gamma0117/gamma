#include "render/texture_array.h"

#include "render/mesh_vertex.h"

#include <glad/glad.h>

#include <algorithm>
#include <format>
#include <vector>

namespace aurora::render {

namespace {

constexpr GLsizei kSize = static_cast<GLsizei>(data::kBlockTextureSize);
constexpr GLsizei kMipLevels = 6; // 32, 16, 8, 4, 2, 1.

// Magenta and black 8 x 8 checks: impossible to mistake for a real texture.
std::vector<std::uint8_t> missingTexturePixels()
{
    std::vector<std::uint8_t> pixels;
    pixels.reserve(static_cast<std::size_t>(kSize * kSize * 4));
    for (GLsizei y = 0; y < kSize; ++y) {
        for (GLsizei x = 0; x < kSize; ++x) {
            const bool magenta = ((x / 8) + (y / 8)) % 2 == 0;
            pixels.insert(pixels.end(), {static_cast<std::uint8_t>(magenta ? 255 : 0), 0,
                                         static_cast<std::uint8_t>(magenta ? 255 : 0), 255});
        }
    }
    return pixels;
}

} // namespace

TextureArray::~TextureArray()
{
    destroy();
}

bool TextureArray::create(std::span<const data::BlockTexture> textures, const TextureLayers& layers,
                          std::string& error)
{
    destroy();
    GLint driverLimit = 0;
    glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &driverLimit);
    const std::uint32_t limit = std::min(static_cast<std::uint32_t>(driverLimit), kMaxTextureLayers);
    if (layers.layerCount > limit) {
        error = std::format("{} texture layers (built-in layer 0 included) exceed the limit of {} (driver allows "
                            "{}, the vertex format {})",
                            layers.layerCount, limit, driverLimit, kMaxTextureLayers);
        return false;
    }

    glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &m_texture);
    glTextureStorage3D(m_texture, kMipLevels, GL_RGBA8, kSize, kSize, static_cast<GLsizei>(layers.layerCount));
    // Image rows are uploaded top row first, so texture coordinate v = 0 is the top edge (as the mesher expects).
    const std::vector<std::uint8_t> missing = missingTexturePixels();
    glTextureSubImage3D(m_texture, 0, 0, 0, kMissingTextureLayer, kSize, kSize, 1, GL_RGBA, GL_UNSIGNED_BYTE,
                        missing.data());
    for (const data::BlockTexture& texture : textures) {
        const auto layer = layers.layerOf.find(texture.id.str());
        if (layer == layers.layerOf.end()) {
            continue;
        }
        glTextureSubImage3D(m_texture, 0, 0, 0, layer->second, kSize, kSize, 1, GL_RGBA, GL_UNSIGNED_BYTE,
                            texture.image.pixels.data());
    }
    glGenerateTextureMipmap(m_texture);
    glTextureParameteri(m_texture, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTextureParameteri(m_texture, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
    glTextureParameteri(m_texture, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTextureParameteri(m_texture, GL_TEXTURE_WRAP_T, GL_REPEAT);
    m_layerCount = layers.layerCount;
    return true;
}

void TextureArray::destroy()
{
    if (m_texture != 0) {
        glDeleteTextures(1, &m_texture);
        m_texture = 0;
        m_layerCount = 0;
    }
}

void TextureArray::bind(std::uint32_t unit) const
{
    glBindTextureUnit(unit, m_texture);
}

} // namespace aurora::render
