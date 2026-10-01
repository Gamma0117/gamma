#include "data/block_loader.h"
#include "data/block_textures.h"

#include "../support/png_test_support.h"
#include "data_test_support.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

using namespace aurora::data;
using aurora::test::describeIssues;
using aurora::test::hasIssue;
using aurora::test::Rgba;
using aurora::test::solidPng;
using aurora::test::TempGame;

namespace {

constexpr std::string_view kStone = R"({"id": "aurora:stone", "hardness": 1.5, "textures": {"all": "block/stone"}})";
constexpr Rgba kGrey{120, 120, 128, 255};
constexpr Rgba kRed{200, 30, 30, 255};

std::span<const std::byte> asBytes(const std::string& text)
{
    return std::as_bytes(std::span(text.data(), text.size()));
}

void writeTexture(const TempGame& game, std::string_view pack, std::string_view path, const std::string& bytes)
{
    game.write(std::filesystem::path(std::string(pack)) / "assets/aurora/textures" / (std::string(path) + ".png"),
               bytes);
}

struct Loaded {
    BlockLoadResult blocks;
    BlockTextureLoadResult textures;
};

Loaded loadAll(const std::vector<DataPack>& packs)
{
    Loaded loaded{loadBlocks(packs), {}};
    if (loaded.blocks.registry) {
        loaded.textures = loadBlockTextures(*loaded.blocks.registry, loaded.blocks.textureFiles);
    }
    return loaded;
}

// Base pack "base" with aurora:stone using block/stone, whose PNG holds `png`.
Loaded loadStoneWith(const std::string& png)
{
    TempGame game("texture");
    game.block("base", "aurora", "stone.json", kStone);
    writeTexture(game, "base", "block/stone", png);
    return loadAll({game.pack("base", true)});
}

bool hasTextureError(const Loaded& loaded, std::string_view text)
{
    return hasIssue(loaded.textures.issues, IssueSeverity::Error, "stone.png", "", text);
}

} // namespace

TEST_CASE("PNG decoding gives RGBA pixels top row first", "[data][textures]")
{
    RgbaImage source = aurora::test::solidImage(2, 2, kGrey);
    source.pixels[0] = 10; // Top-left red channel.
    source.pixels[3 * 4 + 2] = 250; // Bottom-right blue channel.
    const std::string png = aurora::test::encodePng(source);

    const DecodeResult decoded = decodePng(asBytes(png));
    INFO(decoded.error);
    REQUIRE(decoded.image);
    CHECK(decoded.image->width == 2);
    CHECK(decoded.image->height == 2);
    CHECK(decoded.image->pixels == source.pixels);
}

TEST_CASE("Only PNG data decodes", "[data][textures]")
{
    const std::string png = solidPng(4, 4, kGrey);
    // A BMP header, a JPEG header, text and nothing at all, whatever the file is called.
    for (const std::string& bytes : {std::string("BM\x36\x00\x00\x00", 6), std::string("\xff\xd8\xff\xe0", 4),
                                     std::string("not an image"), std::string()}) {
        const DecodeResult decoded = decodePng(asBytes(bytes));
        CHECK_FALSE(decoded.image);
        CHECK(decoded.error.find("not a PNG file") != std::string::npos);
    }
    // The signature alone, or a PNG cut short, does not decode either.
    for (const std::size_t length : {std::size_t{8}, png.size() / 2}) {
        const DecodeResult decoded = decodePng(asBytes(png.substr(0, length)));
        INFO("first " << length << " bytes");
        CHECK_FALSE(decoded.image);
        CHECK(decoded.error.find("cannot decode the PNG") != std::string::npos);
    }
}

TEST_CASE("Block textures load from the files the block loader resolved", "[data][textures]")
{
    TempGame game("texture_packs");
    game.block("base", "aurora", "stone.json", kStone);
    writeTexture(game, "base", "block/stone", solidPng(32, 32, kGrey));
    writeTexture(game, "mod", "block/stone", solidPng(32, 32, kRed));

    SECTION("The base pack alone")
    {
        const Loaded loaded = loadAll({game.pack("base", true)});
        INFO(describeIssues(loaded.blocks.issues) << describeIssues(loaded.textures.issues));
        REQUIRE(loaded.textures.textures.size() == 1);
        CHECK(loaded.textures.textures[0].id.str() == "aurora:block/stone");
        CHECK(loaded.textures.textures[0].image.pixels[0] == kGrey[0]);
    }
    SECTION("A later pack's file replaces the texture")
    {
        const Loaded loaded = loadAll({game.pack("base", true), game.pack("mod")});
        INFO(describeIssues(loaded.blocks.issues) << describeIssues(loaded.textures.issues));
        const std::filesystem::path modFile = game.root() / "mod/assets/aurora/textures/block/stone.png";
        CHECK(loaded.blocks.textureFiles.at("aurora:block/stone") == modFile);
        REQUIRE(loaded.textures.textures.size() == 1);
        CHECK(loaded.textures.textures[0].file == modFile);
        CHECK(loaded.textures.textures[0].image.pixels[0] == kRed[0]);
    }
    SECTION("A broken file in the later pack is an error, not a reason to use the earlier one")
    {
        writeTexture(game, "mod", "block/stone", "garbage");
        const Loaded loaded = loadAll({game.pack("base", true), game.pack("mod")});
        INFO(describeIssues(loaded.textures.issues));
        CHECK(loaded.textures.textures.empty());
        REQUIRE(loaded.textures.issues.size() == 1);
        CHECK(loaded.textures.issues[0].file == game.root() / "mod/assets/aurora/textures/block/stone.png");
        CHECK(loaded.textures.issues[0].message.find("not a PNG file") != std::string::npos);
    }
}

TEST_CASE("Block texture files must be 32x32 PNG", "[data][textures]")
{
    SECTION("32x32 loads") { CHECK(loadStoneWith(solidPng(32, 32, kGrey)).textures.issues.empty()); }
    SECTION("16x16 is too small")
    {
        const Loaded loaded = loadStoneWith(solidPng(16, 16, kGrey));
        INFO(describeIssues(loaded.textures.issues));
        CHECK(loaded.textures.textures.empty());
        CHECK(hasTextureError(loaded, "must be 32x32 pixels, this one is 16x16"));
    }
    SECTION("32x64 is the wrong shape")
    {
        const Loaded loaded = loadStoneWith(solidPng(32, 64, kGrey));
        CHECK(hasTextureError(loaded, "this one is 32x64"));
    }
    SECTION("A text file named .png")
    {
        const Loaded loaded = loadStoneWith("hello");
        CHECK(hasTextureError(loaded, "not a PNG file"));
    }
    SECTION("A cut-off PNG")
    {
        const std::string png = solidPng(32, 32, kGrey);
        const Loaded loaded = loadStoneWith(png.substr(0, png.size() - 20));
        CHECK(hasTextureError(loaded, "cannot decode the PNG"));
    }
    SECTION("An empty file (enough for the block loader, which only checks that it exists)")
    {
        const Loaded loaded = loadStoneWith("");
        REQUIRE(loaded.blocks.registry);
        CHECK(hasTextureError(loaded, "not a PNG file"));
    }
}

TEST_CASE("Every bad texture is reported and none is handed out", "[data][textures]")
{
    TempGame game("texture_several");
    game.block("base", "aurora", "stone.json", kStone);
    game.block("base", "aurora", "dirt.json",
               R"({"id": "aurora:dirt", "hardness": 1, "textures": {"all": "block/dirt"}})");
    game.block("base", "aurora", "log.json",
               R"({"id": "aurora:log", "hardness": 1, "textures": {"all": "block/log", "top": "block/log_top"}})");
    writeTexture(game, "base", "block/stone", "bad");
    writeTexture(game, "base", "block/dirt", solidPng(8, 8, kGrey));
    writeTexture(game, "base", "block/log", solidPng(32, 32, kGrey));
    writeTexture(game, "base", "block/log_top", solidPng(32, 32, kGrey));

    const Loaded loaded = loadAll({game.pack("base", true)});
    INFO(describeIssues(loaded.blocks.issues) << describeIssues(loaded.textures.issues));
    REQUIRE(loaded.blocks.registry);
    CHECK(loaded.textures.textures.empty());
    CHECK(countIssues(loaded.textures.issues, IssueSeverity::Error) == 2);
    CHECK(hasIssue(loaded.textures.issues, IssueSeverity::Error, "stone.png", "", "not a PNG file"));
    CHECK(hasIssue(loaded.textures.issues, IssueSeverity::Error, "dirt.png", "", "this one is 8x8"));
}

TEST_CASE("Block textures load from a game folder with a non-ASCII path", "[data][textures]")
{
    TempGame game("texture_unicode");
    // Built from UTF-8 so the name survives on Windows too, where narrow strings are not UTF-8.
    const std::filesystem::path root = game.root() / std::filesystem::path(u8"게임 폴더 é");
    const auto write = [&](const std::filesystem::path& relative, const std::string& bytes) {
        std::filesystem::create_directories((root / relative).parent_path());
        std::ofstream file(root / relative, std::ios::binary);
        file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    };
    write("data/aurora/blocks/stone.json", std::string(kStone));
    write("assets/aurora/textures/block/stone.png", solidPng(32, 32, kGrey));

    const Loaded loaded = loadAll({DataPack{"base", root, true}});
    INFO(describeIssues(loaded.blocks.issues) << describeIssues(loaded.textures.issues));
    REQUIRE(loaded.blocks.registry);
    CHECK(loaded.textures.issues.empty());
    CHECK(loaded.textures.textures.size() == 1);
}

#ifndef _WIN32
TEST_CASE("A texture file that cannot be read is an error", "[data][textures]")
{
    TempGame game("texture_permissions");
    game.block("base", "aurora", "stone.json", kStone);
    writeTexture(game, "base", "block/stone", solidPng(32, 32, kGrey));
    const std::filesystem::path file = game.root() / "base/assets/aurora/textures/block/stone.png";
    std::filesystem::permissions(file, std::filesystem::perms::none);
    if (std::ifstream(file).good()) {
        SKIP("permission bits are not enforced for this user (e.g. root)");
    }

    const Loaded loaded = loadAll({game.pack("base", true)});
    INFO(describeIssues(loaded.textures.issues));
    CHECK(loaded.textures.textures.empty());
    CHECK(hasIssue(loaded.textures.issues, IssueSeverity::Error, "stone.png", "", "cannot open the file"));
}
#endif

TEST_CASE("The shipped block textures all load", "[data][textures]")
{
    const std::vector<DataPack> packs{{"aurora", std::filesystem::path(AURORA_SOURCE_DIR) / "game", true}};
    const Loaded loaded = loadAll(packs);
    INFO(describeIssues(loaded.blocks.issues) << describeIssues(loaded.textures.issues));
    REQUIRE(loaded.blocks.registry);
    CHECK(loaded.textures.issues.empty());
    // stone, dirt, grass top and side, cobblestone, oak log top and side, oak planks.
    CHECK(loaded.textures.textures.size() == 8);
}
