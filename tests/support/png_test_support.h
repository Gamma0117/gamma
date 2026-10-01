#pragma once

#include "data/block_textures.h"

#include <array>
#include <cstdint>
#include <string>

namespace aurora::test {

using Rgba = std::array<std::uint8_t, 4>;

// PNG file bytes for `image` (8-bit RGBA).
std::string encodePng(const data::RgbaImage& image);

// A width x height image of one colour.
data::RgbaImage solidImage(std::uint32_t width, std::uint32_t height, Rgba color);

// PNG file bytes of a width x height image of one colour.
std::string solidPng(std::uint32_t width, std::uint32_t height, Rgba color);

} // namespace aurora::test
