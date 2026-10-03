#include "data/player_interaction.h"

#include "../support/png_test_support.h"
#include "data_test_support.h"

#include <catch2/catch_test_macros.hpp>

#include <cfloat>
#include <cmath>
#include <filesystem>
#include <format>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace aurora::data;
using aurora::test::describeIssues;
using aurora::test::hasIssue;
using aurora::test::TempGame;

namespace {

constexpr std::string_view kInteractionPath = "data/aurora/player/interaction.json";

// stone 1.5, dirt 0.5, grass 0.6, oak_log 2.0 (three axis states), bedrock unbreakable, light_air invisible with
// hardness 0, adamant hardness 1e30. North textures: stone -> block/stone, oak_log -> block/oak_log_side.
std::shared_ptr<const BlockRegistry> makeRegistry()
{
    const auto block = [](std::string_view id, float hardness) {
        BlockDefinition definition;
        definition.id = *ResourceId::parse(id);
        definition.hardness = hardness;
        return definition;
    };
    std::vector<BlockDefinition> blocks{block("aurora:stone", 1.5f), block("aurora:dirt", 0.5f),
                                        block("aurora:grass_block", 0.6f), block("aurora:adamant", 1e30f)};
    blocks[0].faceTextures.fill(*ResourceId::parse("aurora:block/stone"));
    BlockDefinition log = block("aurora:oak_log", 2.0f);
    log.properties = {BlockProperty{"axis", {"x", "y", "z"}}};
    log.defaultValues = {1};
    log.faceTextures.fill(*ResourceId::parse("aurora:block/oak_log_side"));
    log.faceTextures[static_cast<std::size_t>(BlockFace::Up)] = *ResourceId::parse("aurora:block/oak_log_top");
    blocks.push_back(std::move(log));
    BlockDefinition bedrock = block("aurora:bedrock", 0.0f);
    bedrock.unbreakable = true;
    blocks.push_back(std::move(bedrock));
    BlockDefinition lightAir = block("aurora:light_air", 0.0f);
    lightAir.render = RenderLayer::Invisible;
    blocks.push_back(std::move(lightAir));
    std::vector<LoadIssue> issues;
    return BlockRegistry::create(std::move(blocks), issues);
}

// The shipped values with one field replaced by `value` (raw JSON), or removed when `value` is empty.
std::string interactionJson(std::string_view field = {}, std::string_view value = {})
{
    const std::pair<std::string_view, std::string_view> fields[] = {
        {"reach", "5.0"},
        {"mining_seconds_per_hardness", "0.5"},
        {"palette", R"(["aurora:stone", "aurora:dirt", "aurora:oak_log"])"},
        {"particle_count", "12"},
        {"particle_lifetime", "0.6"},
        {"particle_gravity", "32.0"},
        {"particle_speed", "2.0"},
        {"particle_size", "0.06"},
        {"max_particles", "512"},
    };
    std::string json = "{";
    for (const auto& [name, defaultValue] : fields) {
        if (name == field && value.empty()) {
            continue;
        }
        json += std::format("{}\"{}\": {}", json.size() > 1 ? ", " : "", name, name == field ? value : defaultValue);
    }
    return json + "}";
}

PlayerInteractionLoadResult loadInteraction(const TempGame& game, std::string_view json)
{
    game.write(std::filesystem::path("base") / kInteractionPath, json);
    const std::vector<DataPack> packs{game.pack("base", true)};
    return loadPlayerInteraction(packs, *makeRegistry());
}

bool hasError(const PlayerInteractionLoadResult& result, std::string_view pointer, std::string_view text)
{
    return hasIssue(result.issues, IssueSeverity::Error, "interaction.json", pointer, text);
}

BlockStateId stateOf(const BlockRegistry& registry, std::string_view text)
{
    return *registry.parseState(text).state;
}

} // namespace

TEST_CASE("Mining ticks round only the float error of the hardness", "[data][interaction]")
{
    // The shipped blocks with 0.5 s per hardness.
    CHECK(miningTicks(1.5f, 0.5) == 15u);
    CHECK(miningTicks(0.5f, 0.5) == 5u);
    CHECK(miningTicks(0.6f, 0.5) == 6u); // 0.6f is 0.6000000238: not 7.
    CHECK(miningTicks(2.0f, 0.5) == 20u);
    CHECK(miningTicks(0.0f, 0.5) == 1u);  // At least one tick.
    CHECK(miningTicks(0.3f, 0.5) == 3u);
    CHECK(miningTicks(1.1f, 0.5) == 11u);
    CHECK(miningTicks(1.00005f, 0.5) == 11u); // 10.0005 ticks: a real fraction, so 11 (a fixed 1e-3 gave 10).
    CHECK(miningTicks(10000.2f, 0.5) == 100002u); // Float error 0.002 ticks: 100002 (a fixed 1e-3 gave 100003).
    CHECK(miningTicks(7.0f, 0.15) == 21u);
    // The coefficient must be a double: as a float 0.15 is 0.150000006 and 7 x it rounds up past 21.
    CHECK(miningTicks(7.0f, static_cast<double>(0.15f)) == 22u);
    // The neighbouring floats of 0.6f are other numbers, not its error.
    CHECK(miningTicks(std::nextafter(0.6f, 1.0f), 0.5) == 7u);
    CHECK(miningTicks(std::nextafter(0.6f, 0.0f), 0.5) == 6u);
    CHECK(miningTicks(1e-30f, 0.5) == 1u);
    CHECK(miningTicks(std::numeric_limits<float>::denorm_min(), 0.5) == 1u);

    // The limit, checked before any conversion to an integer.
    CHECK(miningTicks(100000.0f, 0.5) == kMaxMiningTicks);
    CHECK_FALSE(miningTicks(100000.01f, 0.5));
    CHECK_FALSE(miningTicks(FLT_MAX, 0.5));
    CHECK_FALSE(miningTicks(FLT_MAX, 30.0));
    CHECK_FALSE(miningTicks(std::numeric_limits<float>::infinity(), 0.5));
    CHECK_FALSE(miningTicks(std::numeric_limits<float>::quiet_NaN(), 0.5));
}

TEST_CASE("Player interaction settings load with their tables", "[data][interaction]")
{
    const aurora::test::QuietLog quiet; // One warning on purpose.
    TempGame game("interaction_ok");
    game.write(std::filesystem::path("base") / kInteractionPath, interactionJson());
    const std::vector<DataPack> packs{game.pack("base", true)};
    const auto registry = makeRegistry();
    const PlayerInteractionLoadResult result = loadPlayerInteraction(packs, *registry);
    INFO(describeIssues(result.issues));
    REQUIRE(result.interaction);
    const PlayerInteraction& interaction = *result.interaction;
    CHECK(interaction.tuning.reach == 5.0);
    CHECK(interaction.tuning.miningSecondsPerHardness == 0.5);
    CHECK(interaction.tuning.particleCount == 12);
    CHECK(interaction.tuning.particleSize == 0.06);
    CHECK(interaction.tuning.maxParticles == 512);
    REQUIRE(interaction.tuning.palette.size() == 3);
    CHECK(interaction.tuning.palette[2].str() == "aurora:oak_log");
    CHECK(interaction.paletteStates ==
          std::vector<BlockStateId>{stateOf(*registry, "aurora:stone"), stateOf(*registry, "aurora:dirt"),
                                    stateOf(*registry, "aurora:oak_log[axis=y]")});

    REQUIRE(interaction.miningTicks.size() == registry->stateCount());
    CHECK(interaction.miningTicks[kAirState] == 0);
    CHECK(interaction.miningTicks[kUnknownState] == 0);
    CHECK(interaction.miningTicks[stateOf(*registry, "aurora:stone")] == 15);
    CHECK(interaction.miningTicks[stateOf(*registry, "aurora:grass_block")] == 6);
    for (const char* axis : {"x", "y", "z"}) {
        CHECK(interaction.miningTicks[stateOf(*registry, std::format("aurora:oak_log[axis={}]", axis))] == 20);
    }
    CHECK(interaction.miningTicks[stateOf(*registry, "aurora:bedrock")] == 0);
    CHECK(interaction.miningTicks[stateOf(*registry, "aurora:light_air")] == 1); // Breakable, if it could be picked.
    CHECK(interaction.miningTicks[stateOf(*registry, "aurora:adamant")] == 0);
    CHECK(hasIssue(result.issues, IssueSeverity::Warning, "interaction.json", "/mining_seconds_per_hardness",
                   "aurora:adamant (hardness 1e+30) would take more than 1000000 ticks"));
    CHECK(countIssues(result.issues, IssueSeverity::Error) == 0);
}

TEST_CASE("Player interaction fields out of range are errors", "[data][interaction]")
{
    const aurora::test::QuietLog quiet;
    TempGame game("interaction_ranges");
    struct Case {
        std::string_view field;
        std::string_view value;
        bool valid;
    };
    const Case cases[] = {
        {"reach", "0.0999", false},
        {"reach", "0.1", true},
        {"reach", "16", true},
        {"reach", "16.0001", false},
        {"reach", "\"far\"", false},
        {"mining_seconds_per_hardness", "0", false},
        {"mining_seconds_per_hardness", "30", true},
        {"mining_seconds_per_hardness", "30.001", false},
        {"particle_count", "7", false},
        {"particle_count", "8", true},
        {"particle_count", "16", true},
        {"particle_count", "17", false},
        {"particle_count", "12.5", false},
        {"particle_count", "-1", false},
        {"particle_lifetime", "0", false},
        {"particle_lifetime", "5", true},
        {"particle_lifetime", "5.01", false},
        {"particle_gravity", "-0.1", false},
        {"particle_gravity", "0", true},
        {"particle_gravity", "200.5", false},
        {"particle_speed", "20", true},
        {"particle_speed", "20.5", false},
        {"particle_size", "0", false},
        {"particle_size", "0.25", true},
        {"particle_size", "0.26", false},
        {"max_particles", "15", false},
        {"max_particles", "16", true},
        {"max_particles", "4096", true},
        {"max_particles", "4097", false},
        {"max_particles", "1e3", false},
    };
    for (const Case& c : cases) {
        INFO(c.field << " = " << c.value);
        const PlayerInteractionLoadResult result = loadInteraction(game, interactionJson(c.field, c.value));
        INFO(describeIssues(result.issues));
        CHECK(static_cast<bool>(result.interaction) == c.valid);
        if (!c.valid) {
            CHECK(hasError(result, std::format("/{}", c.field), "expected a"));
        }
    }
    for (const char* field : {"reach", "mining_seconds_per_hardness", "palette", "particle_count",
                              "particle_lifetime", "particle_gravity", "particle_speed", "particle_size",
                              "max_particles"}) {
        INFO("without " << field);
        const PlayerInteractionLoadResult result = loadInteraction(game, interactionJson(field));
        CHECK_FALSE(result.interaction);
        CHECK(hasError(result, "", std::format("missing required field '{}'", field)));
    }
}

TEST_CASE("The palette lists distinct placeable blocks", "[data][interaction]")
{
    const aurora::test::QuietLog quiet;
    TempGame game("interaction_palette");
    const auto palette = [&](std::string_view value) {
        return loadInteraction(game, interactionJson("palette", value));
    };

    CHECK(hasError(palette(R"("aurora:stone")"), "/palette", "expected a list of 1 to 9 block ids"));
    CHECK(hasError(palette("[]"), "/palette", "got 0 entries"));
    CHECK(hasError(palette(R"(["aurora:stone", "aurora:dirt", "aurora:grass_block", "aurora:oak_log",
                               "aurora:stone", "aurora:stone", "aurora:stone", "aurora:stone", "aurora:stone",
                               "aurora:stone"])"),
                   "/palette", "got 10 entries"));
    CHECK(hasError(palette(R"([7])"), "/palette/0", "expected a block id"));
    CHECK(hasError(palette(R"(["stone"])"), "/palette/0", "expected a block id")); // Namespace required.
    CHECK(hasError(palette(R"(["aurora:oak_log[axis=x]"])"), "/palette/0", "expected a block id"));
    CHECK(hasError(palette(R"(["aurora:block/stone"])"), "/palette/0", "expected a block id"));
    CHECK(hasError(palette(R"(["aurora:marble"])"), "/palette/0", "unknown block aurora:marble"));
    CHECK(hasError(palette(R"(["aurora:air"])"), "/palette/0", "aurora:air cannot be placed"));
    CHECK(hasError(palette(R"(["aurora:unknown"])"), "/palette/0", "aurora:unknown cannot be placed"));
    const PlayerInteractionLoadResult twice = palette(R"(["aurora:dirt", "aurora:stone", "aurora:dirt"])");
    CHECK_FALSE(twice.interaction);
    CHECK(hasError(twice, "/palette/2", "aurora:dirt is already in the palette"));
    const PlayerInteractionLoadResult nine = palette(R"(["aurora:stone", "aurora:dirt", "aurora:grass_block",
        "aurora:oak_log", "aurora:bedrock", "aurora:light_air", "aurora:adamant"])");
    CHECK(nine.interaction);
}

TEST_CASE("The interaction file follows the data rules for files", "[data][interaction]")
{
    const aurora::test::QuietLog quiet;
    TempGame game("interaction_file");
    const auto registry = makeRegistry();

    SECTION("Repeated keys are errors and unknown fields warnings")
    {
        std::string json = interactionJson();
        json.insert(1, R"("reach": 4.0, "colour": "red", )");
        const PlayerInteractionLoadResult result = loadInteraction(game, json);
        CHECK_FALSE(result.interaction);
        INFO(describeIssues(result.issues));
        CHECK(hasError(result, "/reach", "duplicate key 'reach'"));
        CHECK(hasIssue(result.issues, IssueSeverity::Warning, "interaction.json", "/colour", "unknown field"));
    }
    SECTION("A missing file is an error")
    {
        const std::vector<DataPack> packs{game.pack("base", true)};
        std::filesystem::create_directories(game.root() / "base");
        const PlayerInteractionLoadResult result = loadPlayerInteraction(packs, *registry);
        CHECK_FALSE(result.interaction);
        CHECK(hasIssue(result.issues, IssueSeverity::Error, "interaction.json", "",
                       "missing required file (the player interaction settings)"));
    }
    SECTION("The last pack's file replaces the base game's as a whole")
    {
        game.write(std::filesystem::path("base") / kInteractionPath, interactionJson());
        game.write(std::filesystem::path("mod") / kInteractionPath, interactionJson("reach", "3"));
        const std::vector<DataPack> packs{game.pack("base", true), game.pack("mod")};
        const PlayerInteractionLoadResult result = loadPlayerInteraction(packs, *registry);
        REQUIRE(result.interaction);
        CHECK(result.interaction->tuning.reach == 3.0);
        CHECK(result.file.parent_path().parent_path().parent_path().parent_path().filename() == "mod");
    }
    SECTION("Not an object")
    {
        CHECK(hasError(loadInteraction(game, "[1, 2]"), "", "must hold one JSON object"));
    }
}

TEST_CASE("Crack textures are ten 32x32 PNG files", "[data][interaction]")
{
    const aurora::test::QuietLog quiet;
    TempGame game("cracks");
    const auto writeStage = [&](std::string_view pack, std::size_t stage, const std::string& bytes) {
        game.write(std::filesystem::path(std::string(pack)) / "assets/aurora/textures/block" /
                       std::format("destroy_stage_{}.png", stage),
                   bytes);
    };
    for (std::size_t stage = 0; stage < kCrackStageCount; ++stage) {
        writeStage("base", stage,
                   aurora::test::solidPng(32, 32, {static_cast<std::uint8_t>(stage * 20), 0, 0, 200}));
    }
    const std::vector<DataPack> base{game.pack("base", true)};

    SECTION("All ten load in order")
    {
        const CrackTextureLoadResult result = loadCrackTextures(base);
        INFO(describeIssues(result.issues));
        REQUIRE(result.stages.size() == kCrackStageCount);
        CHECK(result.stages[7].pixels[0] == 140);
        CHECK(result.issues.empty());
    }
    SECTION("A later pack replaces one stage")
    {
        writeStage("mod", 3, aurora::test::solidPng(32, 32, {1, 2, 3, 255}));
        const std::vector<DataPack> packs{game.pack("base", true), game.pack("mod")};
        const CrackTextureLoadResult result = loadCrackTextures(packs);
        REQUIRE(result.stages.size() == kCrackStageCount);
        CHECK(result.stages[3].pixels[1] == 2);
        CHECK(result.stages[4].pixels[0] == 80);
    }
    SECTION("A wrong size, a non-PNG, a missing file and a broken link are errors")
    {
        writeStage("base", 2, aurora::test::solidPng(16, 16, {0, 0, 0, 255}));
        writeStage("base", 5, "not a png");
        std::filesystem::remove(game.root() / "base/assets/aurora/textures/block/destroy_stage_8.png");
        const std::filesystem::path link = game.root() / "base/assets/aurora/textures/block/destroy_stage_9.png";
        std::filesystem::remove(link);
        const bool linked = aurora::test::makeBrokenLink(link);
        const CrackTextureLoadResult result = loadCrackTextures(base);
        INFO(describeIssues(result.issues));
        CHECK(result.stages.empty());
        CHECK(hasIssue(result.issues, IssueSeverity::Error, "destroy_stage_2.png", "",
                       "a block texture must be 32x32 pixels, this one is 16x16"));
        CHECK(hasIssue(result.issues, IssueSeverity::Error, "destroy_stage_5.png", "", "not a PNG file"));
        CHECK(hasIssue(result.issues, IssueSeverity::Error, "destroy_stage_8.png", "",
                       "missing required file (crack stage 8)"));
        if (linked) {
            CHECK(hasIssue(result.issues, IssueSeverity::Error, "destroy_stage_9.png", "", "broken link"));
            CHECK(countIssues(result.issues, IssueSeverity::Error) == 4);
        }
    }
}

TEST_CASE("Fragment colours are the mean of each block's opaque north texels", "[data][interaction]")
{
    using aurora::test::solidImage;
    CHECK(meanOpaqueColour(solidImage(32, 32, {10, 20, 30, 255})) == Rgb{10, 20, 30});
    CHECK(meanOpaqueColour(solidImage(32, 32, {10, 20, 30, 127})) == kMissingParticleColour); // No opaque texel.
    CHECK(meanOpaqueColour(solidImage(32, 32, {10, 20, 30, 128})) == Rgb{10, 20, 30});

    RgbaImage mixed = solidImage(2, 2, {0, 0, 0, 0}); // Two opaque texels, two cut out.
    const auto setTexel = [&](std::size_t index, std::array<std::uint8_t, 4> rgba) {
        for (std::size_t channel = 0; channel < 4; ++channel) {
            mixed.pixels[index * 4 + channel] = rgba[channel];
        }
    };
    setTexel(0, {100, 0, 50, 255});
    setTexel(3, {201, 10, 50, 200});
    setTexel(1, {255, 255, 255, 100}); // Below 128: ignored.
    CHECK(meanOpaqueColour(mixed) == Rgb{151, 5, 50}); // (100 + 201) / 2 rounds to 151.

    RgbaImage single = solidImage(1, 1, {9, 8, 7, 255});
    CHECK(meanOpaqueColour(single) == Rgb{9, 8, 7});

    const auto registry = makeRegistry();
    const std::vector<BlockTexture> textures{
        {*ResourceId::parse("aurora:block/oak_log_side"), {}, solidImage(32, 32, {120, 80, 40, 255})},
        {*ResourceId::parse("aurora:block/oak_log_top"), {}, solidImage(32, 32, {200, 180, 120, 255})},
        {*ResourceId::parse("aurora:block/stone"), {}, solidImage(32, 32, {128, 128, 128, 255})},
    };
    const std::vector<Rgb> colours = blockParticleColours(*registry, textures);
    REQUIRE(colours.size() == registry->stateCount());
    CHECK(colours[stateOf(*registry, "aurora:stone")] == Rgb{128, 128, 128});
    for (const char* axis : {"x", "y", "z"}) {
        // Every axis uses the side (north) texture of the axis=y model, never the top.
        CHECK(colours[stateOf(*registry, std::format("aurora:oak_log[axis={}]", axis))] == Rgb{120, 80, 40});
    }
    CHECK(colours[stateOf(*registry, "aurora:dirt")] == kMissingParticleColour); // No texture.
    CHECK(colours[kAirState] == kMissingParticleColour);
}
