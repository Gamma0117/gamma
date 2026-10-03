#pragma once

#include "data/block_registry.h"
#include "data/load_issue.h"
#include "data/resource_id.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace aurora::data {

// Block and item textures are 32 x 32 pixels (1 block = 32 px).
inline constexpr std::uint32_t kBlockTextureSize = 32;

// Decoded 8-bit RGBA pixels, top row first.
struct RgbaImage {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> pixels; // width * height * 4 bytes.
};

struct DecodeResult {
    std::optional<RgbaImage> image;
    std::string error; // Why decoding failed, when image is empty.
};

// Decodes a PNG held in memory. Only PNG is accepted: anything without the PNG signature fails, whatever its file
// name says.
DecodeResult decodePng(std::span<const std::byte> bytes);

// Reads a whole file through std::filesystem (so non-ASCII paths work on Windows too). nullopt with `error` set on
// failure.
std::optional<std::vector<std::byte>> readFileBytes(const std::filesystem::path& file, std::string& error);

// Reads and decodes `file` as a block texture: a PNG of exactly kBlockTextureSize x kBlockTextureSize. nullopt with
// an Error issue naming the file otherwise.
std::optional<RgbaImage> readBlockTextureFile(const std::filesystem::path& file, std::vector<LoadIssue>& issues);

struct BlockTexture {
    ResourceId id;
    std::filesystem::path file;
    RgbaImage image;
};

struct BlockTextureLoadResult {
    // Every face texture of the registry's blocks, sorted by id. Filled only when there were no errors.
    std::vector<BlockTexture> textures;
    std::vector<LoadIssue> issues;
};

// Decodes the face texture of every block in `registry` from the file the block loader resolved for it
// (BlockLoadResult::textureFiles); the packs are not searched again. Each must be a PNG of exactly
// kBlockTextureSize x kBlockTextureSize. Emissive textures wait for the shader step (P0-10). Every problem is
// collected as a LoadIssue naming the file.
BlockTextureLoadResult loadBlockTextures(const BlockRegistry& registry,
                                         const std::map<std::string, std::filesystem::path, std::less<>>& files);

// Logs every issue and a summary line.
void logBlockTextureLoadResult(const BlockTextureLoadResult& result);

} // namespace aurora::data
