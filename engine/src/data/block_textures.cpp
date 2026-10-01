#include "data/block_textures.h"

#include "core/log.h"
#include "core/profiler.h"
#include "core/utf8.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <fstream>
#include <iterator>
#include <limits>
#include <set>
#include <utility>

// The only stb_image implementation in the engine. PNG only, and no stdio: files are read through
// std::filesystem and decoded from memory, so stb never sees a path.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include <stb_image.h>

namespace aurora::data {

namespace {

constexpr std::array<std::uint8_t, 8> kPngSignature{0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};

} // namespace

DecodeResult decodePng(std::span<const std::byte> bytes)
{
    if (bytes.size() < kPngSignature.size() ||
        std::memcmp(bytes.data(), kPngSignature.data(), kPngSignature.size()) != 0) {
        return {std::nullopt, "not a PNG file (it does not start with the PNG signature)"};
    }
    if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return {std::nullopt, "file too large"};
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(bytes.data()),
                                            static_cast<int>(bytes.size()), &width, &height, &channels, 4);
    if (pixels == nullptr) {
        return {std::nullopt, std::format("cannot decode the PNG: {}", stbi_failure_reason())};
    }
    RgbaImage image;
    image.width = static_cast<std::uint32_t>(width);
    image.height = static_cast<std::uint32_t>(height);
    image.pixels.assign(pixels, pixels + static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
    stbi_image_free(pixels);
    return {std::move(image), {}};
}

std::optional<std::vector<std::byte>> readFileBytes(const std::filesystem::path& file, std::string& error)
{
    std::ifstream stream(file, std::ios::binary);
    if (!stream) {
        error = "cannot open the file";
        return std::nullopt;
    }
    std::vector<char> text{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    if (stream.bad()) {
        error = "cannot read the file";
        return std::nullopt;
    }
    std::vector<std::byte> bytes(text.size());
    std::memcpy(bytes.data(), text.data(), text.size());
    return bytes;
}

BlockTextureLoadResult loadBlockTextures(const BlockRegistry& registry,
                                         const std::map<std::string, std::filesystem::path, std::less<>>& files)
{
    AURORA_PROFILE_ZONE();
    BlockTextureLoadResult result;

    std::set<std::string, std::less<>> ids;
    for (const BlockDefinition& block : registry.blocks()) {
        for (const ResourceId& texture : block.faceTextures) {
            if (!texture.empty()) {
                ids.insert(texture.str());
            }
        }
    }

    std::vector<BlockTexture> textures;
    for (const std::string& id : ids) {
        const auto found = files.find(id);
        if (found == files.end()) {
            // The block loader resolves every texture of a registry it hands out; a gap is a programming error.
            result.issues.push_back({IssueSeverity::Error, {}, {}, std::format("texture {} has no file", id)});
            continue;
        }
        const std::filesystem::path& file = found->second;
        std::string error;
        const std::optional<std::vector<std::byte>> bytes = readFileBytes(file, error);
        if (!bytes) {
            result.issues.push_back({IssueSeverity::Error, file, {}, error});
            continue;
        }
        DecodeResult decoded = decodePng(*bytes);
        if (!decoded.image) {
            result.issues.push_back({IssueSeverity::Error, file, {}, decoded.error});
            continue;
        }
        if (decoded.image->width != kBlockTextureSize || decoded.image->height != kBlockTextureSize) {
            result.issues.push_back(
                {IssueSeverity::Error, file, {},
                 std::format("a block texture must be {0}x{0} pixels, this one is {1}x{2}", kBlockTextureSize,
                             decoded.image->width, decoded.image->height)});
            continue;
        }
        textures.push_back({*ResourceId::parse(id), file, std::move(*decoded.image)});
    }

    if (countIssues(result.issues, IssueSeverity::Error) == 0) {
        result.textures = std::move(textures);
    }
    return result;
}

void logBlockTextureLoadResult(const BlockTextureLoadResult& result)
{
    logIssues(result.issues);
    const std::size_t errors = countIssues(result.issues, IssueSeverity::Error);
    if (errors == 0) {
        core::logInfo("data", "Loaded {} block textures ({}x{} PNG)", result.textures.size(), kBlockTextureSize,
                      kBlockTextureSize);
    } else {
        core::logError("data", "Block textures have {} error(s); fix the lines above", errors);
    }
}

} // namespace aurora::data
