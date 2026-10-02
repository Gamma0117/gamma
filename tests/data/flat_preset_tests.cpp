#include "core/log.h"
#include "data/flat_preset.h"

#include "data_test_support.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using namespace aurora::data;
using aurora::test::describeIssues;
using aurora::test::hasIssue;
using aurora::test::TempGame;

namespace {

constexpr std::string_view kPresetPath = "data/aurora/worldgen/flat.json";
constexpr std::string_view kStonePreset = R"({"layers": [{"block": "aurora:stone", "height": 5}]})";
constexpr std::string_view kDirtPreset = R"({"layers": [{"block": "aurora:dirt", "height": 2}]})";

// Writes flat.json into pack "base" and loads it against the test registry.
FlatPresetLoadResult loadPreset(const TempGame& game, std::string_view json, const BlockRegistry& registry)
{
    game.write(std::filesystem::path("base") / kPresetPath, json);
    const std::vector<DataPack> packs{game.pack("base", true)};
    return loadFlatPreset(packs, registry);
}

bool hasError(const FlatPresetLoadResult& result, std::string_view pointer, std::string_view text)
{
    return hasIssue(result.issues, IssueSeverity::Error, "flat.json", pointer, text);
}

} // namespace

TEST_CASE("Flat preset loads the layers bottom first", "[data][flat]")
{
    const auto registry = aurora::test::makeTestRegistry();
    TempGame game("flat_ok");
    const FlatPresetLoadResult result = loadPreset(game, R"({
        "layers": [
            {"block": "aurora:stone", "height": 124},
            {"block": "aurora:oak_log", "height": 2},
            {"block": "aurora:oak_log[axis=x]", "height": 1},
            {"block": "aurora:grass_block", "height": 1}
        ]
    })",
                                                   *registry);
    INFO(describeIssues(result.issues));
    REQUIRE(result.preset);
    CHECK(result.issues.empty());
    CHECK(result.file.filename() == "flat.json");

    const std::vector<FlatLayer>& layers = result.preset->layers;
    REQUIRE(layers.size() == 4);
    CHECK(registry->stateToString(layers[0].state) == "aurora:stone");
    CHECK(layers[0].height == 124);
    // A block id alone is its default state (axis=y), not the block's first state (axis=x).
    CHECK(registry->stateToString(layers[1].state) == "aurora:oak_log[axis=y]");
    CHECK(layers[1].state != registry->findBlock("aurora:oak_log")->firstState);
    CHECK(registry->stateToString(layers[2].state) == "aurora:oak_log[axis=x]");
    CHECK(registry->stateToString(layers[3].state) == "aurora:grass_block");
    CHECK(result.preset->totalHeight() == 128);
}

TEST_CASE("Flat preset layers must fit in the world height", "[data][flat]")
{
    const auto registry = aurora::test::makeTestRegistry();
    TempGame game("flat_height");

    SECTION("384 blocks fill the world exactly")
    {
        const FlatPresetLoadResult result = loadPreset(
            game, R"({"layers": [{"block": "aurora:stone", "height": 300}, {"block": "aurora:dirt", "height": 84}]})",
            *registry);
        INFO(describeIssues(result.issues));
        REQUIRE(result.preset);
        CHECK(result.preset->totalHeight() == 384);
    }
    SECTION("385 blocks are one too many")
    {
        const FlatPresetLoadResult result = loadPreset(
            game, R"({"layers": [{"block": "aurora:stone", "height": 300}, {"block": "aurora:dirt", "height": 85}]})",
            *registry);
        INFO(describeIssues(result.issues));
        CHECK_FALSE(result.preset);
        CHECK(hasError(result, "/layers/1/height", "this layer needs 85, only 84 of 384 are left"));
    }
    SECTION("A huge height is compared before adding")
    {
        const FlatPresetLoadResult result = loadPreset(
            game,
            R"({"layers": [{"block": "aurora:stone", "height": 1},
                           {"block": "aurora:dirt", "height": 18446744073709551615}]})",
            *registry);
        INFO(describeIssues(result.issues));
        CHECK_FALSE(result.preset);
        CHECK(hasError(result, "/layers/1/height", "only 383 of 384 are left"));
    }
}

TEST_CASE("Flat preset heights must be whole numbers of at least 1", "[data][flat]")
{
    const auto registry = aurora::test::makeTestRegistry();
    TempGame game("flat_height_type");
    for (const std::string_view height : {"0", "-1", "1.5", "2.0", "1e3", "true", "null", "\"3\"", "[3]"}) {
        const std::string json =
            std::string(R"({"layers": [{"block": "aurora:stone", "height": )") + std::string(height) + "}]}";
        const FlatPresetLoadResult result = loadPreset(game, json, *registry);
        INFO("height " << height << "\n" << describeIssues(result.issues));
        CHECK_FALSE(result.preset);
        CHECK(hasError(result, "/layers/0/height", "expected a whole number >= 1"));
    }
}

TEST_CASE("Flat preset blocks are parsed strictly", "[data][flat]")
{
    const auto registry = aurora::test::makeTestRegistry();
    TempGame game("flat_block");
    struct Case {
        std::string_view block;
        std::string_view message;
    };
    for (const Case& bad : {Case{R"("aurora:bedrock")", "unknown block"},
                            Case{R"("aurora:oak_log[axis=w]")", "axis"},
                            Case{R"("aurora:stone[]")", ""},
                            Case{R"("stone")", ""},
                            Case{"7", "expected a block state string, got number 7"}}) {
        const std::string json =
            std::string(R"({"layers": [{"block": )") + std::string(bad.block) + R"(, "height": 1}]})";
        const FlatPresetLoadResult result = loadPreset(game, json, *registry);
        INFO("block " << bad.block << "\n" << describeIssues(result.issues));
        CHECK_FALSE(result.preset);
        CHECK(hasError(result, "/layers/0/block", bad.message));
    }
}

TEST_CASE("Flat preset structure errors are reported with their field", "[data][flat]")
{
    const auto registry = aurora::test::makeTestRegistry();
    TempGame game("flat_structure");
    struct Case {
        std::string_view json;
        std::string_view pointer;
        std::string_view message;
    };
    for (const Case& bad : {
             Case{R"([1, 2])", "", "must hold one JSON object, got array"},
             Case{R"({})", "", "missing required field 'layers'"},
             Case{R"({"layers": []})", "/layers", "expected a non-empty array of layers, got array"},
             Case{R"({"layers": {"block": "aurora:stone"}})", "/layers", "got object"},
             Case{R"({"layers": [5]})", "/layers/0", "expected an object"},
             Case{R"({"layers": [{"height": 1}]})", "/layers/0", "missing required field 'block'"},
             Case{R"({"layers": [{"block": "aurora:stone"}]})", "/layers/0", "missing required field 'height'"},
             Case{R"({"layers": [{"block": "aurora:stone", "height": 1, "height": 2}]})", "/layers/0/height",
                  "duplicate key 'height'"},
             Case{R"({"layers": [{"block": "aurora:stone", "height": 1},]})", "", "parse error"},
         }) {
        const FlatPresetLoadResult result = loadPreset(game, bad.json, *registry);
        INFO(bad.json << "\n" << describeIssues(result.issues));
        CHECK_FALSE(result.preset);
        CHECK(hasError(result, bad.pointer, bad.message));
    }
}

TEST_CASE("Unknown flat preset fields are warnings", "[data][flat]")
{
    const auto registry = aurora::test::makeTestRegistry();
    TempGame game("flat_unknown_field");
    const FlatPresetLoadResult result =
        loadPreset(game, R"({"comment": "x", "layers": [{"block": "aurora:stone", "height": 4, "note": 1}]})",
                   *registry);
    INFO(describeIssues(result.issues));
    REQUIRE(result.preset);
    CHECK(hasIssue(result.issues, IssueSeverity::Warning, "flat.json", "/comment", "unknown field"));
    CHECK(hasIssue(result.issues, IssueSeverity::Warning, "flat.json", "/layers/0/note", "unknown field"));
}

TEST_CASE("Flat preset file comes from the last pack that has one", "[data][flat]")
{
    const auto registry = aurora::test::makeTestRegistry();
    TempGame game("flat_packs");
    const std::vector<DataPack> packs{game.pack("base", true), game.pack("mod")};

    SECTION("No pack has the file")
    {
        const FlatPresetLoadResult result = loadFlatPreset(packs, *registry);
        INFO(describeIssues(result.issues));
        CHECK_FALSE(result.preset);
        CHECK(hasIssue(result.issues, IssueSeverity::Error, "flat.json", "", "missing required file"));
    }
    SECTION("A mod replaces the base preset")
    {
        game.write(std::filesystem::path("base") / kPresetPath, kStonePreset);
        game.write(std::filesystem::path("mod") / kPresetPath, kDirtPreset);
        const FlatPresetLoadResult result = loadFlatPreset(packs, *registry);
        INFO(describeIssues(result.issues));
        REQUIRE(result.preset);
        CHECK(result.file == game.root() / "mod" / kPresetPath);
        REQUIRE(result.preset->layers.size() == 1);
        CHECK(registry->stateToString(result.preset->layers[0].state) == "aurora:dirt");
    }
    SECTION("A folder in place of the file is an error, not a reason to use the base preset")
    {
        game.write(std::filesystem::path("base") / kPresetPath, kStonePreset);
        std::filesystem::create_directories(game.root() / "mod" / kPresetPath);
        const FlatPresetLoadResult result = loadFlatPreset(packs, *registry);
        INFO(describeIssues(result.issues));
        CHECK_FALSE(result.preset);
        CHECK(hasIssue(result.issues, IssueSeverity::Error, "flat.json", "", "expected a file"));
    }
    SECTION("A broken link in place of the file is an error")
    {
        game.write(std::filesystem::path("base") / kPresetPath, kStonePreset);
        if (!aurora::test::makeBrokenLink(game.root() / "mod" / kPresetPath)) {
            SKIP("symbolic links are not available");
        }
        const FlatPresetLoadResult result = loadFlatPreset(packs, *registry);
        INFO(describeIssues(result.issues));
        CHECK_FALSE(result.preset);
        CHECK(hasIssue(result.issues, IssueSeverity::Error, "flat.json", "", "broken link"));
    }
}

TEST_CASE("The shipped flat preset is 124 stone 3 dirt and 1 grass", "[data][flat]")
{
    const std::vector<DataPack> packs{{"aurora", std::filesystem::path(AURORA_SOURCE_DIR) / "game", true}};
    const BlockLoadResult blocks = loadBlocks(packs);
    INFO(describeIssues(blocks.issues));
    REQUIRE(blocks.registry);
    const FlatPresetLoadResult result = loadFlatPreset(packs, *blocks.registry);
    INFO(describeIssues(result.issues));
    REQUIRE(result.preset);
    CHECK(result.issues.empty());

    const std::vector<FlatLayer>& layers = result.preset->layers;
    REQUIRE(layers.size() == 3);
    CHECK(blocks.registry->stateToString(layers[0].state) == "aurora:stone");
    CHECK(layers[0].height == 124);
    CHECK(blocks.registry->stateToString(layers[1].state) == "aurora:dirt");
    CHECK(layers[1].height == 3);
    CHECK(blocks.registry->stateToString(layers[2].state) == "aurora:grass_block");
    CHECK(layers[2].height == 1);
}

TEST_CASE("Flat preset boxes load in file order", "[data][flat]")
{
    const auto registry = aurora::test::makeTestRegistry();
    TempGame game("flat_boxes");
    const FlatPresetLoadResult result = loadPreset(game, R"({
        "layers": [{"block": "aurora:stone", "height": 128}],
        "boxes": [
            {"block": "aurora:dirt", "from": [-30000000, -64, 5], "to": [30000000, 319, 5]},
            {"block": "aurora:air", "from": [-3, 60, -3], "to": [-1, 63, -1]}
        ]
    })",
                                                   *registry);
    INFO(describeIssues(result.issues));
    REQUIRE(result.preset);
    CHECK(result.issues.empty());
    const std::vector<FlatBox>& boxes = result.preset->boxes;
    REQUIRE(boxes.size() == 2);
    CHECK(registry->stateToString(boxes[0].state) == "aurora:dirt");
    CHECK(boxes[0].from == std::array<std::int32_t, 3>{-30'000'000, -64, 5});
    CHECK(boxes[0].to == std::array<std::int32_t, 3>{30'000'000, 319, 5});
    CHECK(boxes[1].state == kAirState);
    CHECK(boxes[1].from == std::array<std::int32_t, 3>{-3, 60, -3});
}

TEST_CASE("Flat preset box problems are errors", "[data][flat]")
{
    const aurora::test::QuietLog quiet;
    const auto registry = aurora::test::makeTestRegistry();
    TempGame game("flat_bad_boxes");
    const auto load = [&](std::string_view box) {
        return loadPreset(game,
                          std::string(R"({"layers": [{"block": "aurora:stone", "height": 5}], "boxes": [)") +
                              std::string(box) + "]}",
                          *registry);
    };
    struct Case {
        std::string_view box;
        std::string_view pointer;
        std::string_view text;
    };
    const Case cases[] = {
        {R"({"block": "aurora:stone", "from": [2.0, 0, 0], "to": [3, 0, 0]})", "/boxes/0/from/0", "whole number"},
        {R"({"block": "aurora:stone", "from": [0, 0, 0], "to": [1e3, 0, 0]})", "/boxes/0/to/0", "whole number"},
        {R"({"block": "aurora:stone", "from": [0, -65, 0], "to": [0, 0, 0]})", "/boxes/0/from/1", "-64 to 319"},
        {R"({"block": "aurora:stone", "from": [0, 0, 0], "to": [0, 320, 0]})", "/boxes/0/to/1", "-64 to 319"},
        {R"({"block": "aurora:stone", "from": [30000001, 0, 0], "to": [30000001, 0, 0]})", "/boxes/0/from/0",
         "-30000000 to 30000000"},
        {R"({"block": "aurora:stone", "from": [0, 0, -99999999999999999999], "to": [0, 0, 0]})", "/boxes/0/from/2",
         "whole number"},
        {R"({"block": "aurora:stone", "from": [0, 0, 18446744073709551615], "to": [0, 0, 0]})", "/boxes/0/from/2",
         "whole number"},
        {R"({"block": "aurora:stone", "from": [5, 0, 0], "to": [4, 0, 0]})", "/boxes/0/to/0", "must not be below"},
        {R"({"block": "aurora:stone", "from": [0, 0], "to": [0, 0, 0]})", "/boxes/0/from", "[x, y, z]"},
        {R"({"block": "aurora:stone", "to": [0, 0, 0]})", "/boxes/0", "missing required field 'from'"},
        {R"({"block": "aurora:bedrock", "from": [0, 0, 0], "to": [0, 0, 0]})", "/boxes/0/block", "bedrock"},
        {R"({"from": [0, 0, 0], "to": [0, 0, 0]})", "/boxes/0", "missing required field 'block'"},
        {R"("stone")", "/boxes/0", "expected an object"},
    };
    for (const Case& c : cases) {
        INFO(c.box);
        const FlatPresetLoadResult result = load(c.box);
        INFO(describeIssues(result.issues));
        CHECK_FALSE(result.preset);
        CHECK(hasError(result, c.pointer, c.text));
    }

    SECTION("Not an array")
    {
        const FlatPresetLoadResult result = loadPreset(
            game, R"({"layers": [{"block": "aurora:stone", "height": 5}], "boxes": {"a": 1}})", *registry);
        CHECK(hasError(result, "/boxes", "expected an array"));
    }
    SECTION("Too many boxes")
    {
        constexpr std::string_view kBox = R"({"block": "aurora:stone", "from": [0, 0, 0], "to": [0, 0, 0]})";
        std::string boxes;
        for (std::size_t i = 0; i <= kMaxFlatBoxes; ++i) {
            boxes += std::string(i > 0 ? ", " : "") + std::string(kBox);
        }
        const FlatPresetLoadResult result = load(boxes);
        CHECK_FALSE(result.preset);
        CHECK(hasError(result, "/boxes", "at most 256 boxes"));
    }
}
