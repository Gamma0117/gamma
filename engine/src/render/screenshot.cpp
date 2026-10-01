#include "render/screenshot.h"

#include "core/utf8.h"

#include <glad/glad.h>

#include <cstdint>
#include <cstring>
#include <format>
#include <fstream>
#include <vector>

// The engine's only stb_image_write implementation.
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace aurora::render {

bool saveFramebufferPng(const std::filesystem::path& file, int width, int height, std::string& error)
{
    if (width <= 0 || height <= 0) {
        error = std::format("nothing to capture ({}x{} framebuffer)", width, height);
        return false;
    }
    const std::size_t rowBytes = static_cast<std::size_t>(width) * 4;
    std::vector<std::uint8_t> pixels(rowBytes * static_cast<std::size_t>(height));
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    // GL rows start at the bottom; PNG rows at the top.
    std::vector<std::uint8_t> flipped(pixels.size());
    for (int row = 0; row < height; ++row) {
        std::memcpy(flipped.data() + static_cast<std::size_t>(row) * rowBytes,
                    pixels.data() + static_cast<std::size_t>(height - 1 - row) * rowBytes, rowBytes);
    }

    std::string png;
    const auto append = [](void* context, void* data, int size) {
        static_cast<std::string*>(context)->append(static_cast<const char*>(data), static_cast<std::size_t>(size));
    };
    if (stbi_write_png_to_func(append, &png, width, height, 4, flipped.data(), static_cast<int>(rowBytes)) == 0) {
        error = "cannot encode the PNG";
        return false;
    }
    std::ofstream stream(file, std::ios::binary | std::ios::trunc);
    stream.write(png.data(), static_cast<std::streamsize>(png.size()));
    stream.close();
    if (!stream) {
        error = std::format("cannot write {}", core::pathToUtf8(file));
        return false;
    }
    return true;
}

} // namespace aurora::render
