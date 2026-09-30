#include "data/block_loader.h"

#include "data_test_support.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <vector>

using namespace aurora::data;
using aurora::test::describeIssues;
using aurora::test::hasIssue;
using aurora::test::TempGame;

namespace {

constexpr std::string_view kStone = R"({"id": "aurora:stone", "hardness": 1.5, "textures": {"all": "block/stone"}})";

BlockLoadResult load(const std::vector<DataPack>& packs)
{
    return loadBlocks(packs);
}

// A base pack "base" with the stone texture and one block file.
BlockLoadResult loadSingleFile(std::string_view json)
{
    TempGame game("single");
    game.texture("base", "aurora", "block/stone");
    game.block("base", "aurora", "test.json", json);
    return load({game.pack("base", true)});
}

std::vector<std::string> blockIds(const BlockRegistry& registry)
{
    std::vector<std::string> ids;
    for (const BlockDefinition& block : registry.blocks()) {
        ids.push_back(block.id.str());
    }
    return ids;
}

// {"a": [...], "b": [...]} with `values` numbered values each, for state-limit cases.
std::string numberedStates(const std::vector<std::size_t>& valueCounts)
{
    std::string states;
    for (std::size_t p = 0; p < valueCounts.size(); ++p) {
        std::string values;
        for (std::size_t v = 0; v < valueCounts[p]; ++v) {
            values += std::format("{}\"v{}\"", v == 0 ? "" : ",", v);
        }
        states += std::format("{}\"p{}\": [{}]", p == 0 ? "" : ", ", p, values);
    }
    return "{" + states + "}";
}

std::string blockWithStates(std::string_view id, const std::vector<std::size_t>& valueCounts)
{
    return std::format(R"({{"id": "{}", "hardness": 1, "textures": {{"all": "block/stone"}}, "states": {}}})", id,
                       numberedStates(valueCounts));
}

} // namespace

TEST_CASE("A valid pack loads with every field applied", "[data][loader]")
{
    TempGame game("valid");
    for (const std::string_view texture : {"block/stone", "block/grass_top", "block/grass_side", "block/dirt",
                                           "block/log_top", "block/log_side", "block/lamp", "block/lamp_e"}) {
        game.texture("base", "aurora", texture);
    }
    game.block("base", "aurora", "stone.json", kStone);
    game.block("base", "aurora", "grass.json", R"({
        "id": "aurora:grass_block", "hardness": 0.6,
        "textures": {"top": "block/grass_top", "bottom": "block/dirt", "side": "block/grass_side"}
    })");
    game.block("base", "aurora", "log.json", R"({
        "id": "aurora:oak_log", "hardness": 2,
        "textures": {"top": "block/log_top", "bottom": "block/log_top", "side": "block/log_side"},
        "states": {"axis": ["x", "y", "z"]}, "default_state": {"axis": "y"}
    })");
    game.block("base", "aurora", "lamp.json", R"({
        "id": "aurora:lamp", "unbreakable": true, "explosion_resistance": 600, "light": 15,
        "render": "cutout", "solid": false,
        "textures": {"all": "aurora:block/lamp", "emissive": "block/lamp_e"},
        "tool": "pickaxe", "min_tool_rank": 3, "drops": "aurora:loot/lamp", "generation": {"y_min": 0}
    })");

    const BlockLoadResult result = load({game.pack("base", true)});
    INFO(describeIssues(result.issues));
    REQUIRE(result.registry);
    CHECK(result.issues.empty()); // Deferred fields (tool, drops, ...) are accepted silently.
    CHECK(result.filesRead == 4);

    const BlockRegistry& registry = *result.registry;
    CHECK(blockIds(registry) == std::vector<std::string>{"aurora:air", "aurora:unknown", "aurora:grass_block",
                                                         "aurora:lamp", "aurora:oak_log", "aurora:stone"});
    CHECK(registry.stateCount() == 8);

    const BlockDefinition& grass = *registry.findBlock("aurora:grass_block");
    CHECK(grass.hardness == 0.6f);
    CHECK(grass.faceTextures[static_cast<std::size_t>(BlockFace::Up)].str() == "aurora:block/grass_top");
    CHECK(grass.faceTextures[static_cast<std::size_t>(BlockFace::Down)].str() == "aurora:block/dirt");
    CHECK(grass.faceTextures[static_cast<std::size_t>(BlockFace::North)].str() == "aurora:block/grass_side");
    CHECK(grass.faceTextures[static_cast<std::size_t>(BlockFace::East)].str() == "aurora:block/grass_side");
    CHECK(grass.render == RenderLayer::Opaque);
    CHECK(grass.solid);
    CHECK(grass.light == 0);
    CHECK(grass.sourcePack == "base");
    CHECK(grass.sourceFile.filename() == "grass.json");

    const BlockDefinition& lamp = *registry.findBlock("aurora:lamp");
    CHECK(lamp.unbreakable);
    CHECK(lamp.explosionResistance == 600.0f);
    CHECK(lamp.light == 15);
    CHECK(lamp.render == RenderLayer::Cutout);
    CHECK_FALSE(lamp.solid);
    CHECK(lamp.emissiveTexture.str() == "aurora:block/lamp_e");

    CHECK(registry.stateToString(registry.findBlock("aurora:oak_log")->defaultState) == "aurora:oak_log[axis=y]");
}

TEST_CASE("Each kind of bad block file is reported with its field", "[data][loader]")
{
    struct Case {
        std::string_view json;
        std::string_view pointer;
        std::string_view text;
    };
    // Every case is a complete file; `pointer` and `text` must match one of its errors.
    const std::array cases{
        // Syntax and shape
        Case{R"({"id": "aurora:a",})", "", "parse error at line 1"},
        Case{R"([1, 2])", "", "must hold one JSON object"},
        Case{R"({"id": "aurora:a", "hardness": 1e999, "textures": {"all": "block/stone"}})", "", "overflow"},
        Case{R"({"id": "aurora:a", "hardness": 1, "hardness": 2, "textures": {"all": "block/stone"}})",
             "/hardness", "duplicate key 'hardness'"},
        Case{R"({"id": "aurora:a", "hardness": 1, "textures": {"all": "block/stone", "all": "block/stone"}})",
             "/textures/all", "duplicate key 'all'"},
        Case{R"({"id": "aurora:a", "hardness": 1, "textures": {"all": "block/stone"},
                 "generation": {"veins": [{"size": 1}, {"size": 2, "size": 3}]}})",
             "/generation/veins/1/size", "duplicate key 'size'"},
        // id
        Case{R"({"hardness": 1, "textures": {"all": "block/stone"}})", "", "missing required field 'id'"},
        Case{R"({"id": 5, "hardness": 1, "textures": {"all": "block/stone"}})", "/id", "expected a string"},
        Case{R"({"id": "Aurora:Stone", "hardness": 1, "textures": {"all": "block/stone"}})", "/id",
             "not a valid id"},
        Case{R"({"id": "stone", "hardness": 1, "textures": {"all": "block/stone"}})", "/id", "not a valid id"},
        Case{R"({"id": "aurora:blocks/stone", "hardness": 1, "textures": {"all": "block/stone"}})", "/id",
             "must not contain '/'"},
        Case{R"({"id": "aurora:air", "hardness": 1, "textures": {"all": "block/stone"}})", "/id",
             "built into the engine"},
        // Breaking
        Case{R"({"id": "aurora:a", "textures": {"all": "block/stone"}})", "", "missing required field 'hardness'"},
        Case{R"({"id": "aurora:a", "hardness": "abc", "textures": {"all": "block/stone"}})", "/hardness",
             "expected a number >= 0, got string \"abc\""},
        Case{R"({"id": "aurora:a", "hardness": -1, "textures": {"all": "block/stone"}})", "/hardness",
             "finite number >= 0"},
        Case{R"({"id": "aurora:a", "hardness": 1e300, "textures": {"all": "block/stone"}})", "/hardness",
             "finite number >= 0"},
        Case{R"({"id": "aurora:a", "hardness": 1, "unbreakable": true, "textures": {"all": "block/stone"}})",
             "/hardness", "not both"},
        Case{R"({"id": "aurora:a", "hardness": 1, "unbreakable": false, "textures": {"all": "block/stone"}})",
             "/unbreakable", "only \"unbreakable\": true is allowed"},
        Case{R"({"id": "aurora:a", "hardness": 1, "unbreakable": false, "textures": {"all": "block/stone"}})",
             "/hardness", "not both"},
        Case{R"({"id": "aurora:a", "unbreakable": false, "textures": {"all": "block/stone"}})", "/unbreakable",
             "only \"unbreakable\": true is allowed"},
        Case{R"({"id": "aurora:a", "unbreakable": "yes", "textures": {"all": "block/stone"}})", "/unbreakable",
             "expected true or false"},
        Case{R"({"id": "aurora:a", "hardness": 1, "explosion_resistance": -5, "textures": {"all": "block/stone"}})",
             "/explosion_resistance", "finite number >= 0"},
        // Look
        Case{R"({"id": "aurora:a", "hardness": 1, "light": 16, "textures": {"all": "block/stone"}})", "/light",
             "whole number 0-15"},
        Case{R"({"id": "aurora:a", "hardness": 1, "light": 7.0, "textures": {"all": "block/stone"}})", "/light",
             "whole number 0-15"},
        Case{R"({"id": "aurora:a", "hardness": 1, "light": -1, "textures": {"all": "block/stone"}})", "/light",
             "whole number 0-15"},
        Case{R"({"id": "aurora:a", "hardness": 1, "render": "glass", "textures": {"all": "block/stone"}})",
             "/render", "\"opaque\", \"cutout\" or \"translucent\""},
        Case{R"({"id": "aurora:a", "hardness": 1, "solid": 1, "textures": {"all": "block/stone"}})", "/solid",
             "expected true or false"},
        // Textures
        Case{R"({"id": "aurora:a", "hardness": 1})", "", "missing required field 'textures'"},
        Case{R"({"id": "aurora:a", "hardness": 1, "textures": "block/stone"})", "/textures", "expected an object"},
        Case{R"({"id": "aurora:a", "hardness": 1, "textures": {"all": 5}})", "/textures/all", "expected a texture"},
        Case{R"({"id": "aurora:a", "hardness": 1, "textures": {"all": "Block/Stone"}})", "/textures/all",
             "expected a texture"},
        Case{R"({"id": "aurora:a", "hardness": 1, "textures": {"all": "block/nope"}})", "/textures/all",
             "texture aurora:block/nope not found: no data pack has assets/aurora/textures/block/nope.png"},
        Case{R"({"id": "aurora:a", "hardness": 1, "textures": {"top": "block/stone"}})", "/textures",
             "no texture for the bottom face"},
        Case{R"({"id": "aurora:a", "hardness": 1, "textures": {"top": "block/stone", "bottom": "block/stone"}})",
             "/textures", "no texture for the north face; set 'north', 'side' or 'all'"},
        // States
        Case{R"({"id": "aurora:a", "hardness": 1, "textures": {"all": "block/stone"}, "states": ["x"]})", "/states",
             "expected an object"},
        Case{R"({"id": "aurora:a", "hardness": 1, "textures": {"all": "block/stone"}, "states": {"axis": []}})",
             "/states/axis", "non-empty array"},
        Case{R"({"id": "aurora:a", "hardness": 1, "textures": {"all": "block/stone"}, "states": {"Axis": ["x"]}})",
             "/states/Axis", "[a-z0-9_] only"},
        Case{R"({"id": "aurora:a", "hardness": 1, "textures": {"all": "block/stone"},
                 "states": {"axis": ["x", "Y"]}})",
             "/states/axis/1", "[a-z0-9_] strings"},
        Case{R"({"id": "aurora:a", "hardness": 1, "textures": {"all": "block/stone"},
                 "states": {"axis": ["x", "x"]}})",
             "/states/axis/1", "listed twice"},
        Case{R"({"id": "aurora:a", "hardness": 1, "textures": {"all": "block/stone"},
                 "states": {"axis": ["x", "y"]}, "default_state": {"color": "red"}})",
             "/default_state/color", "not a property listed in 'states'"},
        Case{R"({"id": "aurora:a", "hardness": 1, "textures": {"all": "block/stone"},
                 "states": {"axis": ["x", "y", "z"]}, "default_state": {"axis": "q"}})",
             "/default_state/axis", "expected one of x, y, z"},
        Case{R"({"id": "aurora:a", "hardness": 1, "textures": {"all": "block/stone"}, "default_state": 3})",
             "/default_state", "expected an object"},
    };

    for (const Case& c : cases) {
        CAPTURE(c.json);
        const BlockLoadResult result = loadSingleFile(c.json);
        INFO(describeIssues(result.issues));
        CHECK_FALSE(result.registry);
        CHECK(hasIssue(result.issues, IssueSeverity::Error, "test.json", c.pointer, c.text));
    }
}

TEST_CASE("Per-block state combinations are limited before they are built", "[data][loader]")
{
    // 3 * 5 * 17 * 257 = 65535: one more than data blocks may use.
    const BlockLoadResult tooMany = loadSingleFile(blockWithStates("aurora:big", {3, 5, 17, 257}));
    INFO(describeIssues(tooMany.issues));
    CHECK(hasIssue(tooMany.issues, IssueSeverity::Error, "test.json", "/states", "more than 65534"));

    // 64^8 = 2^48 combinations fail the same way without being enumerated.
    const BlockLoadResult huge = loadSingleFile(blockWithStates("aurora:huge", std::vector<std::size_t>(8, 64)));
    CHECK(hasIssue(huge.issues, IssueSeverity::Error, "test.json", "/states", "more than 65534"));

    // 2 * 7 * 31 * 151 = 65534 fills the 16-bit range exactly.
    const BlockLoadResult full = loadSingleFile(blockWithStates("aurora:full", {2, 7, 31, 151}));
    INFO(describeIssues(full.issues));
    REQUIRE(full.registry);
    CHECK(full.registry->stateCount() == 65536);
}

TEST_CASE("One property may hold every available value", "[data][loader]")
{
    // 65534 values in a single array: the most a data set may use. The duplicate check must stay linear.
    const BlockLoadResult result = loadSingleFile(blockWithStates("aurora:variants", {kMaxDataBlockStates}));
    INFO(describeIssues(result.issues));
    REQUIRE(result.registry);
    CHECK(result.registry->stateCount() == 65536);
    CHECK(result.registry->stateToString(65535) == "aurora:variants[p0=v65533]");
    CHECK(result.registry->parseState("aurora:variants[p0=v65533]").state == BlockStateId{65535});

    const BlockLoadResult duplicate = loadSingleFile(
        R"({"id": "aurora:a", "hardness": 1, "textures": {"all": "block/stone"}, "states": {"p": ["a", "b", "a"]}})");
    CHECK(hasIssue(duplicate.issues, IssueSeverity::Error, "test.json", "/states/p/2", "value 'a' is listed twice"));
}

TEST_CASE("Unreadable folders or files are errors and are never skipped", "[data][loader]")
{
    TempGame game("unreadable");
    game.texture("base", "aurora", "block/stone");
    game.block("base", "aurora", "stone.json", kStone);
    const std::filesystem::path data = game.root() / "base" / "data";

    SECTION("a namespace entry whose type cannot be read")
    {
        if (!aurora::test::makeSelfLoop(data / "looped")) {
            SKIP("symbolic links are not available here");
        }
        const BlockLoadResult result = load({game.pack("base", true)});
        INFO(describeIssues(result.issues));
        CHECK_FALSE(result.registry);
        CHECK(hasIssue(result.issues, IssueSeverity::Error, "looped", "", "cannot read this entry"));
    }
    SECTION("a blocks folder that cannot be accessed")
    {
        std::filesystem::create_directories(data / "fancy");
        if (!aurora::test::makeSelfLoop(data / "fancy" / "blocks")) {
            SKIP("symbolic links are not available here");
        }
        const BlockLoadResult result = load({game.pack("base", true)});
        INFO(describeIssues(result.issues));
        CHECK_FALSE(result.registry);
        CHECK(hasIssue(result.issues, IssueSeverity::Error, "blocks", "", "cannot access the folder"));
    }
    SECTION("a block file entry whose type cannot be read")
    {
        if (!aurora::test::makeSelfLoop(data / "aurora" / "blocks" / "looped.json")) {
            SKIP("symbolic links are not available here");
        }
        const BlockLoadResult result = load({game.pack("base", true)});
        INFO(describeIssues(result.issues));
        CHECK_FALSE(result.registry);
        CHECK(hasIssue(result.issues, IssueSeverity::Error, "looped.json", "", "cannot read this entry"));
    }
    SECTION("a texture file that cannot be checked")
    {
        game.block("base", "aurora", "odd.json",
                   R"({"id": "aurora:odd", "hardness": 1, "textures": {"all": "block/odd"}})");
        if (!aurora::test::makeSelfLoop(game.root() / "base/assets/aurora/textures/block/odd.png")) {
            SKIP("symbolic links are not available here");
        }
        const BlockLoadResult result = load({game.pack("base", true)});
        INFO(describeIssues(result.issues));
        CHECK_FALSE(result.registry);
        CHECK(hasIssue(result.issues, IssueSeverity::Error, "odd.json", "/textures/all", "cannot check"));
    }
}

TEST_CASE("Broken links are errors while working links are followed", "[data][loader]")
{
    TempGame game("broken_links");
    game.texture("base", "aurora", "block/stone");
    game.block("base", "aurora", "stone.json", kStone);
    const std::filesystem::path data = game.root() / "base" / "data";

    SECTION("a blocks folder link whose target moved away")
    {
        game.texture("base", "fancy", "block/gem");
        game.write("base/real_fancy_blocks/gem.json",
                   R"({"id": "fancy:gem", "hardness": 1, "textures": {"all": "block/gem"}})");
        std::error_code error;
        std::filesystem::create_directories(data / "fancy", error);
        std::filesystem::create_directory_symlink(game.root() / "base/real_fancy_blocks", data / "fancy/blocks", error);
        if (error) {
            SKIP("symbolic links are not available here");
        }

        const BlockLoadResult linked = load({game.pack("base", true)});
        INFO(describeIssues(linked.issues));
        REQUIRE(linked.registry);
        CHECK(linked.registry->blockCount() == 4); // A working link is followed.

        std::filesystem::rename(game.root() / "base/real_fancy_blocks", game.root() / "base/moved_away");
        const BlockLoadResult broken = load({game.pack("base", true)});
        INFO(describeIssues(broken.issues));
        CHECK_FALSE(broken.registry);
        CHECK(hasIssue(broken.issues, IssueSeverity::Error, "blocks", "", "broken link"));
    }
    SECTION("a namespace folder that is a broken link")
    {
        if (!aurora::test::makeBrokenLink(data / "gone")) {
            SKIP("symbolic links are not available here");
        }
        const BlockLoadResult result = load({game.pack("base", true)});
        INFO(describeIssues(result.issues));
        CHECK_FALSE(result.registry);
        CHECK(hasIssue(result.issues, IssueSeverity::Error, "gone", "", "broken link"));
    }
    SECTION("a block file that is a broken link")
    {
        if (!aurora::test::makeBrokenLink(data / "aurora" / "blocks" / "gone.json")) {
            SKIP("symbolic links are not available here");
        }
        const BlockLoadResult result = load({game.pack("base", true)});
        INFO(describeIssues(result.issues));
        CHECK_FALSE(result.registry);
        CHECK(hasIssue(result.issues, IssueSeverity::Error, "gone.json", "", "broken link"));
    }
    SECTION("a texture that is a broken link in a later pack")
    {
        // The earlier pack's working file must not hide the later pack's broken one.
        if (!aurora::test::makeBrokenLink(game.root() / "mod/assets/aurora/textures/block/stone.png")) {
            SKIP("symbolic links are not available here");
        }
        const BlockLoadResult result = load({game.pack("base", true), game.pack("mod")});
        INFO(describeIssues(result.issues));
        CHECK_FALSE(result.registry);
        CHECK(hasIssue(result.issues, IssueSeverity::Error, "stone.json", "/textures/all", "broken link"));
    }
    SECTION("a texture folder that is a broken link in a later pack")
    {
        // The link is in the middle of the texture path (…/textures/block -> missing), not the file itself.
        if (!aurora::test::makeBrokenLink(game.root() / "mod/assets/aurora/textures/block")) {
            SKIP("symbolic links are not available here");
        }
        const BlockLoadResult result = load({game.pack("base", true), game.pack("mod")});
        INFO(describeIssues(result.issues));
        CHECK_FALSE(result.registry);
        CHECK(hasIssue(result.issues, IssueSeverity::Error, "stone.json", "/textures/all", "broken link"));
    }
    SECTION("a file in place of a texture folder in a later pack")
    {
        // …/textures/block is a regular file: the path to block/stone.png cannot be resolved, which is not the
        // same as the texture being absent from that pack.
        game.write("mod/assets/aurora/textures/block", "not a folder");
        const BlockLoadResult result = load({game.pack("base", true), game.pack("mod")});
        INFO(describeIssues(result.issues));
        CHECK_FALSE(result.registry);
        CHECK(hasIssue(result.issues, IssueSeverity::Error, "stone.json", "/textures/all", "block is not a folder"));
    }
    SECTION("controls: an absent texture folder falls back and a working folder link is followed")
    {
        // The later pack has assets/aurora/textures but no block/ folder: the earlier pack's file is used.
        std::filesystem::create_directories(game.root() / "mod/assets/aurora/textures");
        const BlockLoadResult absent = load({game.pack("base", true), game.pack("mod")});
        INFO(describeIssues(absent.issues));
        CHECK(absent.registry);

        game.texture("real", "aurora", "block/stone");
        std::error_code error;
        std::filesystem::create_directory_symlink(game.root() / "real/assets/aurora/textures/block",
                                                  game.root() / "mod/assets/aurora/textures/block", error);
        if (error) {
            SKIP("symbolic links are not available here");
        }
        const BlockLoadResult linked = load({game.pack("base", true), game.pack("mod")});
        INFO(describeIssues(linked.issues));
        CHECK(linked.registry);
    }
    SECTION("a data pack folder that is a broken link")
    {
        if (!aurora::test::makeBrokenLink(game.root() / "mod")) {
            SKIP("symbolic links are not available here");
        }
        const BlockLoadResult result = load({game.pack("base", true), game.pack("mod")});
        INFO(describeIssues(result.issues));
        CHECK_FALSE(result.registry);
        CHECK(hasIssue(result.issues, IssueSeverity::Error, "mod", "", "broken link"));
    }
}

#ifndef _WIN32
TEST_CASE("A namespace folder without permissions is an error", "[data][loader]")
{
    TempGame game("permissions");
    game.texture("base", "aurora", "block/stone");
    game.texture("base", "fancy", "block/gem");
    game.block("base", "aurora", "stone.json", kStone);
    game.block("base", "fancy", "gem.json", R"({"id": "fancy:gem", "hardness": 1, "textures": {"all": "block/gem"}})");
    const std::filesystem::path fancy = game.root() / "base/data/fancy";

    const BlockLoadResult readable = load({game.pack("base", true)});
    REQUIRE(readable.registry);
    CHECK(readable.registry->blockCount() == 4); // While readable, both blocks load.

    std::filesystem::permissions(fancy, std::filesystem::perms::none);
    struct RestorePermissions {
        std::filesystem::path path;
        ~RestorePermissions()
        {
            std::error_code error;
            std::filesystem::permissions(path, std::filesystem::perms::owner_all, error);
        }
    } restore{fancy};

    std::error_code probe;
    [[maybe_unused]] const std::filesystem::directory_iterator probeListing(fancy, probe);
    if (!probe) {
        SKIP("permission bits are not enforced for this user (e.g. root)");
    }

    const BlockLoadResult result = load({game.pack("base", true)});
    INFO(describeIssues(result.issues));
    CHECK_FALSE(result.registry); // Never a registry without fancy:gem.
    CHECK(hasIssue(result.issues, IssueSeverity::Error, "blocks", "", "cannot access the folder"));
}
#endif

TEST_CASE("Errors in several files are all reported", "[data][loader]")
{
    TempGame game("several");
    game.texture("base", "aurora", "block/stone");
    game.block("base", "aurora", "a_syntax.json", R"({"id": "aurora:a" "hardness": 1})");
    game.block("base", "aurora", "b_type.json", R"({"id": "aurora:b", "hardness": "hard", "light": 99,
                                                    "textures": {"all": "block/stone"}})");
    game.block("base", "aurora", "c_texture.json", R"({"id": "aurora:c", "hardness": 1,
                                                       "textures": {"all": "block/missing"}})");
    game.block("base", "aurora", "d_valid.json", kStone);
    game.write("base/data/aurora/blocks/notes.txt", "not a block file");

    const BlockLoadResult result = load({game.pack("base", true)});
    INFO(describeIssues(result.issues));
    CHECK_FALSE(result.registry);
    CHECK(result.filesRead == 4);
    CHECK(hasIssue(result.issues, IssueSeverity::Error, "a_syntax.json", "", "parse error"));
    CHECK(hasIssue(result.issues, IssueSeverity::Error, "b_type.json", "/hardness", "expected a number"));
    CHECK(hasIssue(result.issues, IssueSeverity::Error, "b_type.json", "/light", "0-15"));
    CHECK(hasIssue(result.issues, IssueSeverity::Error, "c_texture.json", "/textures/all", "not found"));
    CHECK(countIssues(result.issues, IssueSeverity::Error) == 4);
}

TEST_CASE("Unknown fields and texture slots warn but still load", "[data][loader]")
{
    const BlockLoadResult result = loadSingleFile(
        R"({"id": "aurora:a", "hardness": 1, "hardnes": 2, "textures": {"all": "block/stone", "topp": "block/x"}})");
    INFO(describeIssues(result.issues));
    REQUIRE(result.registry);
    CHECK(hasIssue(result.issues, IssueSeverity::Warning, "test.json", "/hardnes", "unknown field 'hardnes'"));
    CHECK(hasIssue(result.issues, IssueSeverity::Warning, "test.json", "/textures/topp", "unknown texture slot"));
    CHECK(countIssues(result.issues, IssueSeverity::Error) == 0);
}

TEST_CASE("The same block id twice in one pack is an error", "[data][loader]")
{
    TempGame game("duplicate");
    game.texture("base", "aurora", "block/stone");
    game.block("base", "aurora", "a.json", kStone);
    game.block("base", "aurora", "b.json", kStone);

    const BlockLoadResult result = load({game.pack("base", true)});
    INFO(describeIssues(result.issues));
    CHECK_FALSE(result.registry);
    CHECK(hasIssue(result.issues, IssueSeverity::Error, "b.json", "/id", "already defined in"));
    CHECK(hasIssue(result.issues, IssueSeverity::Error, "b.json", "/id", "a.json (same data pack 'base')"));
}

TEST_CASE("A later pack replaces a block with textures from its own folder", "[data][loader]")
{
    TempGame game("override");
    game.texture("base", "aurora", "block/stone");
    game.block("base", "aurora", "stone.json", kStone);
    game.block("mod", "fancy", "stone.json",
               R"({"id": "aurora:stone", "hardness": 9, "textures": {"all": "block/fancy_stone"}})");

    SECTION("texture present in the mod")
    {
        game.texture("mod", "fancy", "block/fancy_stone");
        const BlockLoadResult result = load({game.pack("base", true), game.pack("mod")});
        INFO(describeIssues(result.issues));
        REQUIRE(result.registry);
        CHECK(hasIssue(result.issues, IssueSeverity::Info, "stone.json", "/id", "replaces aurora:stone"));

        const BlockDefinition& stone = *result.registry->findBlock("aurora:stone");
        CHECK(stone.hardness == 9.0f);
        CHECK(stone.sourcePack == "mod");
        // No namespace in the reference: the folder the file is in (data/fancy), not the block id's namespace.
        CHECK(stone.faceTextures[0].str() == "fancy:block/fancy_stone");
    }
    SECTION("texture missing is reported against the mod file")
    {
        const BlockLoadResult result = load({game.pack("base", true), game.pack("mod")});
        INFO(describeIssues(result.issues));
        CHECK_FALSE(result.registry);
        bool reportedInMod = false;
        for (const LoadIssue& issue : result.issues) {
            reportedInMod = reportedInMod || (issue.pointer == "/textures/all" &&
                                              issue.file == game.root() / "mod/data/fancy/blocks/stone.json");
        }
        CHECK(reportedInMod);
    }
}

TEST_CASE("An explicit namespace reaches another pack's textures", "[data][loader]")
{
    TempGame game("cross_pack");
    game.texture("base", "aurora", "block/stone");
    game.block("base", "aurora", "stone.json", kStone);
    game.block("mod", "fancy", "pillar.json",
               R"({"id": "fancy:pillar", "hardness": 1, "textures": {"all": "aurora:block/stone"}})");

    const BlockLoadResult result = load({game.pack("base", true), game.pack("mod")});
    INFO(describeIssues(result.issues));
    REQUIRE(result.registry);
    CHECK(result.registry->findBlock("fancy:pillar")->faceTextures[0].str() == "aurora:block/stone");
}

TEST_CASE("The total state limit applies to the final set after replacements", "[data][loader]")
{
    TempGame game("final_set");
    game.texture("base", "aurora", "block/stone");
    game.block("base", "aurora", "big.json", blockWithStates("aurora:big", {2, 7, 31, 151})); // 65534 states
    game.block("base", "aurora", "small.json",
               R"({"id": "aurora:small", "hardness": 1, "textures": {"all": "block/stone"}})");
    game.block("mod", "aurora", "big.json",
               R"({"id": "aurora:big", "hardness": 1, "textures": {"all": "block/stone"}})");

    // The base pack alone needs 65534 + 1 + 2 built-ins = 65537 states.
    const BlockLoadResult baseOnly = load({game.pack("base", true)});
    INFO(describeIssues(baseOnly.issues));
    CHECK_FALSE(baseOnly.registry);
    CHECK(hasIssue(baseOnly.issues, IssueSeverity::Error, "", "", "Too many block states: 65537"));

    // The mod replaces the big block with a one-state one; the replaced definition no longer counts.
    const BlockLoadResult withMod = load({game.pack("base", true), game.pack("mod")});
    INFO(describeIssues(withMod.issues));
    REQUIRE(withMod.registry);
    CHECK(withMod.registry->stateCount() == 4);
}

TEST_CASE("State numbers do not depend on file names or order", "[data][loader]")
{
    TempGame game("order");
    game.texture("one", "aurora", "block/stone");
    game.texture("two", "aurora", "block/stone");
    const std::string log = R"({"id": "aurora:log", "hardness": 1, "textures": {"all": "block/stone"},
                               "states": {"axis": ["x", "y", "z"], "bark": ["no", "yes"]}})";
    const std::string dirt = R"({"id": "aurora:dirt", "hardness": 1, "textures": {"all": "block/stone"}})";
    game.block("one", "aurora", "a.json", log);
    game.block("one", "aurora", "b.json", dirt);
    game.block("one", "aurora", "c.json", kStone);
    game.block("two", "aurora", "a.json", kStone);
    game.block("two", "aurora", "b.json", log);
    game.block("two", "aurora", "c.json", dirt);

    const BlockLoadResult first = load({game.pack("one", true)});
    const BlockLoadResult second = load({game.pack("two", true)});
    REQUIRE(first.registry);
    REQUIRE(second.registry);
    REQUIRE(first.registry->stateCount() == second.registry->stateCount());
    for (std::uint32_t state = 0; state < first.registry->stateCount(); ++state) {
        const auto id = static_cast<BlockStateId>(state);
        CHECK(first.registry->stateToString(id) == second.registry->stateToString(id));
    }
}

TEST_CASE("Data pack folders are checked", "[data][loader]")
{
    TempGame game("folders");

    SECTION("the base pack needs data/aurora/blocks")
    {
        game.write("base/readme.txt", "empty pack");
        const BlockLoadResult result = load({game.pack("base", true)});
        INFO(describeIssues(result.issues));
        CHECK_FALSE(result.registry);
        CHECK(hasIssue(result.issues, IssueSeverity::Error, "blocks", "", "must have this folder"));
    }
    SECTION("an empty blocks folder only warns")
    {
        std::filesystem::create_directories(game.root() / "base/data/aurora/blocks");
        const BlockLoadResult result = load({game.pack("base", true)});
        INFO(describeIssues(result.issues));
        REQUIRE(result.registry);
        CHECK(result.registry->blockCount() == 2);
        CHECK(hasIssue(result.issues, IssueSeverity::Warning, "blocks", "", "no block files"));
    }
    SECTION("a mod without blocks is fine")
    {
        game.texture("base", "aurora", "block/stone");
        game.block("base", "aurora", "stone.json", kStone);
        game.write("mod/data/fancy/items/readme.txt", "items come later");
        const BlockLoadResult result = load({game.pack("base", true), game.pack("mod")});
        INFO(describeIssues(result.issues));
        REQUIRE(result.registry);
        CHECK(result.issues.empty());
    }
    SECTION("a missing pack folder and a bad namespace folder are errors")
    {
        game.texture("base", "aurora", "block/stone");
        game.block("base", "aurora", "stone.json", kStone);
        game.block("base", "Bad_Name", "x.json", kStone);
        const BlockLoadResult result = load({game.pack("base", true), game.pack("absent")});
        INFO(describeIssues(result.issues));
        CHECK_FALSE(result.registry);
        CHECK(hasIssue(result.issues, IssueSeverity::Error, "absent", "", "folder not found"));
        CHECK(hasIssue(result.issues, IssueSeverity::Error, "Bad_Name", "", "not a valid namespace"));
    }
}

TEST_CASE("The shipped game data loads without any issue", "[data][loader]")
{
    const std::filesystem::path gameFolder = std::filesystem::path(AURORA_SOURCE_DIR) / "game";
    const BlockLoadResult result = load({DataPack{"aurora", gameFolder, true}});
    INFO(describeIssues(result.issues));
    REQUIRE(result.registry);
    CHECK(result.issues.empty());

    const BlockRegistry& registry = *result.registry;
    CHECK(blockIds(registry) ==
          std::vector<std::string>{"aurora:air", "aurora:unknown", "aurora:cobblestone", "aurora:dirt",
                                   "aurora:grass_block", "aurora:oak_log", "aurora:oak_planks", "aurora:stone"});
    CHECK(registry.blockCount() == 8);
    CHECK(registry.stateCount() == 10);
    CHECK(registry.stateToString(registry.findBlock("aurora:oak_log")->defaultState) == "aurora:oak_log[axis=y]");
    CHECK(registry.findBlock("aurora:stone")->hardness == 1.5f);
    CHECK(registry.findBlock("aurora:grass_block")->faceTextures[static_cast<std::size_t>(BlockFace::Down)].str() ==
          "aurora:block/dirt");
}
