// Checks a screenshot of the start view (aurora --screenshot): sky at the top, grass in the middle, no "missing"
// texture anywhere.
//
//   aurora_screenshot_check <file.png>
//
// Exit codes, so a test can tell "no terrain" from a broken run:
//   0  the image shows sky above and terrain below
//   2  the image decodes but does not show that (the reason is printed)
//   3  the file is missing, unreadable or not a PNG
//   1  wrong arguments

#include "data/block_textures.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace {

// The app's clear colour, 8-bit (0.10, 0.14, 0.22).
constexpr int kSkyR = 26;
constexpr int kSkyG = 36;
constexpr int kSkyB = 56;

struct Pixel {
    int r;
    int g;
    int b;
};

Pixel pixelAt(const aurora::data::RgbaImage& image, std::uint32_t x, std::uint32_t y)
{
    const std::size_t at = (static_cast<std::size_t>(y) * image.width + x) * 4;
    return {image.pixels[at], image.pixels[at + 1], image.pixels[at + 2]};
}

bool isSky(const Pixel& p)
{
    return std::abs(p.r - kSkyR) <= 3 && std::abs(p.g - kSkyG) <= 3 && std::abs(p.b - kSkyB) <= 3;
}

bool isMissingTexture(const Pixel& p)
{
    return p.r > 150 && p.b > 150 && p.g < 80;
}

bool isGreenish(const Pixel& p)
{
    return p.g > p.r && p.g > p.b;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::fprintf(stderr, "usage: aurora_screenshot_check <file.png>\n");
        return 1;
    }
    const std::filesystem::path file(argv[1]);
    std::string error;
    const std::optional<std::vector<std::byte>> bytes = aurora::data::readFileBytes(file, error);
    if (!bytes) {
        std::printf("unreadable: %s\n", error.c_str());
        return 3;
    }
    const aurora::data::DecodeResult decoded = aurora::data::decodePng(*bytes);
    if (!decoded.image || decoded.image->width < 64 || decoded.image->height < 64) {
        std::printf("not a usable PNG: %s\n", decoded.image ? "too small" : decoded.error.c_str());
        return 3;
    }
    const aurora::data::RgbaImage& image = *decoded.image;

    // Top tenth: sky. Middle band below the horizon (55..95 % of the height, 10..90 % of the width): terrain.
    std::size_t topSky = 0;
    std::size_t topCount = 0;
    for (std::uint32_t y = 0; y < image.height / 10; ++y) {
        for (std::uint32_t x = 0; x < image.width; ++x) {
            topSky += isSky(pixelAt(image, x, y)) ? 1 : 0;
            ++topCount;
        }
    }
    std::size_t ground = 0;
    std::size_t green = 0;
    std::size_t missing = 0;
    std::size_t groundCount = 0;
    for (std::uint32_t y = image.height * 55 / 100; y < image.height * 95 / 100; ++y) {
        for (std::uint32_t x = image.width / 10; x < image.width * 9 / 10; ++x) {
            const Pixel p = pixelAt(image, x, y);
            ground += isSky(p) ? 0 : 1;
            green += isGreenish(p) ? 1 : 0;
            missing += isMissingTexture(p) ? 1 : 0;
            ++groundCount;
        }
    }
    const double skyShare = static_cast<double>(topSky) / static_cast<double>(topCount);
    const double groundShare = static_cast<double>(ground) / static_cast<double>(groundCount);
    const double greenShare = static_cast<double>(green) / static_cast<double>(groundCount);
    std::printf("%ux%u: top %.1f%% sky; terrain band %.1f%% not sky, %.1f%% green, %zu missing-texture pixels\n",
                image.width, image.height, skyShare * 100.0, groundShare * 100.0, greenShare * 100.0, missing);

    if (skyShare < 0.95) {
        std::printf("no terrain check: the top of the image is not sky\n");
        return 2;
    }
    if (groundShare < 0.95 || greenShare < 0.80) {
        std::printf("no terrain: the band below the horizon is not grass\n");
        return 2;
    }
    if (missing > 0) {
        std::printf("no terrain: the missing texture is visible\n");
        return 2;
    }
    std::printf("ok: sky above, grass below\n");
    return 0;
}
