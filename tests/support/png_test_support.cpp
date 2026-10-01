#include "png_test_support.h"

// The test executable's own PNG writer; the engine only decodes.
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace aurora::test {

std::string encodePng(const data::RgbaImage& image)
{
    std::string bytes;
    const auto append = [](void* context, void* data, int size) {
        static_cast<std::string*>(context)->append(static_cast<const char*>(data), static_cast<std::size_t>(size));
    };
    stbi_write_png_to_func(append, &bytes, static_cast<int>(image.width), static_cast<int>(image.height), 4,
                           image.pixels.data(), static_cast<int>(image.width * 4));
    return bytes;
}

data::RgbaImage solidImage(std::uint32_t width, std::uint32_t height, Rgba color)
{
    data::RgbaImage image{width, height, {}};
    image.pixels.reserve(static_cast<std::size_t>(width) * height * 4);
    for (std::uint32_t i = 0; i < width * height; ++i) {
        image.pixels.insert(image.pixels.end(), color.begin(), color.end());
    }
    return image;
}

std::string solidPng(std::uint32_t width, std::uint32_t height, Rgba color)
{
    return encodePng(solidImage(width, height, color));
}

} // namespace aurora::test
