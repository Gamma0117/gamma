#include "data/player_movement.h"

#include "../entity/entity_test_support.h"
#include "data_test_support.h"

#include <catch2/catch_test_macros.hpp>

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

constexpr std::string_view kMovementPath = "data/aurora/player/movement.json";

// The shipped values with one field replaced by `value` (raw JSON), or removed when `value` is empty.
std::string movementJson(std::string_view field = {}, std::string_view value = {})
{
    const std::pair<std::string_view, std::string_view> fields[] = {
        {"width", "0.6"},          {"height", "1.8"},
        {"eye_height", "1.62"},    {"sneak_eye_height", "1.27"},
        {"step_height", "0.6"},    {"gravity", "32"},
        {"terminal_velocity", "78"}, {"jump_velocity", "9.8"},
        {"walk_speed", "4.3"},     {"sprint_speed", "5.6"},
        {"sneak_speed", "1.3"},    {"ground_acceleration", "0.5"},
        {"air_acceleration", "0.05"},
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

PlayerMovementLoadResult loadMovement(const TempGame& game, std::string_view json)
{
    game.write(std::filesystem::path("base") / kMovementPath, json);
    const std::vector<DataPack> packs{game.pack("base", true)};
    return loadPlayerMovement(packs);
}

bool hasError(const PlayerMovementLoadResult& result, std::string_view pointer, std::string_view text)
{
    return hasIssue(result.issues, IssueSeverity::Error, "movement.json", pointer, text);
}

} // namespace

TEST_CASE("Player movement settings load every field", "[data][movement]")
{
    TempGame game("movement_ok");
    const PlayerMovementLoadResult result = loadMovement(game, movementJson());
    INFO(describeIssues(result.issues));
    REQUIRE(result.tuning);
    CHECK(result.issues.empty());
    CHECK(result.tuning->width == 0.6);
    CHECK(result.tuning->sneakEyeHeight == 1.27);
    CHECK(result.tuning->airAcceleration == 0.05);
    CHECK(result.tuning->terminalVelocity == 78.0); // Whole numbers are fine.
}

TEST_CASE("Player movement fields out of range are errors", "[data][movement]")
{
    const aurora::test::QuietLog quiet;
    TempGame game("movement_ranges");
    struct Case {
        std::string_view field;
        std::string_view value;
        bool valid;
    };
    const Case cases[] = {
        {"width", "0.0999", false},
        {"width", "1e-8", false},
        {"width", "0.1", true},
        {"width", "4", true},
        {"width", "4.0001", false},
        {"height", "0.0999", false},
        {"height", "4.0001", false},
        {"eye_height", "0", false},
        {"eye_height", "1.81", false},       // Above the height.
        {"sneak_eye_height", "1.63", false}, // Above the eye height.
        {"step_height", "1.8", false},       // Not below the height.
        {"step_height", "0", true},
        {"gravity", "-1", false},
        {"gravity", "0", true},
        {"gravity", "200.5", false},
        {"terminal_velocity", "0", false},
        {"terminal_velocity", "1e999", false}, // Too large to parse (the file is rejected as a whole).
        {"jump_velocity", "51", false},
        {"walk_speed", "\"fast\"", false},
        {"sprint_speed", "null", false},
        {"ground_acceleration", "0", false},
        {"ground_acceleration", "1", true},
        {"air_acceleration", "1.5", false},
    };
    for (const Case& c : cases) {
        INFO(c.field << " = " << c.value);
        const PlayerMovementLoadResult result = loadMovement(game, movementJson(c.field, c.value));
        INFO(describeIssues(result.issues));
        CHECK(static_cast<bool>(result.tuning) == c.valid);
        if (!c.valid) {
            const bool unparsable = c.value == "1e999";
            CHECK(hasError(result, unparsable ? "" : std::format("/{}", c.field), unparsable ? "overflow" : ""));
        }
    }
}

TEST_CASE("Player movement file problems are reported", "[data][movement]")
{
    const aurora::test::QuietLog quiet;
    TempGame game("movement_file");
    SECTION("A missing field")
    {
        const PlayerMovementLoadResult result = loadMovement(game, movementJson("gravity"));
        CHECK_FALSE(result.tuning);
        CHECK(hasError(result, "", "missing required field 'gravity'"));
    }
    SECTION("An unknown field is only a warning")
    {
        std::string json = movementJson();
        json.insert(1, "\"speeed\": 3, ");
        const PlayerMovementLoadResult result = loadMovement(game, json);
        CHECK(result.tuning);
        CHECK(hasIssue(result.issues, IssueSeverity::Warning, "movement.json", "/speeed", "unknown field"));
    }
    SECTION("Not an object")
    {
        const PlayerMovementLoadResult result = loadMovement(game, "[1, 2]");
        CHECK_FALSE(result.tuning);
        CHECK(hasError(result, "", "one JSON object"));
    }
    SECTION("No pack has the file")
    {
        const std::vector<DataPack> packs{game.pack("base", true)};
        const PlayerMovementLoadResult result = loadPlayerMovement(packs);
        CHECK_FALSE(result.tuning);
        CHECK(hasError(result, "", "missing required file"));
    }
    SECTION("A later pack replaces the file as a whole")
    {
        game.write(std::filesystem::path("base") / kMovementPath, movementJson());
        game.write(std::filesystem::path("mod") / kMovementPath, movementJson("walk_speed", "7"));
        const std::vector<DataPack> packs{game.pack("base", true), game.pack("mod")};
        const PlayerMovementLoadResult result = loadPlayerMovement(packs);
        REQUIRE(result.tuning);
        CHECK(result.tuning->walkSpeed == 7.0);
        CHECK(result.file.parent_path().parent_path().parent_path().parent_path().filename() == "mod");
    }
}

TEST_CASE("The shipped player movement settings are the tested ones", "[data][movement]")
{
    const std::vector<DataPack> packs{{"aurora", std::filesystem::path(AURORA_SOURCE_DIR) / "game", true}};
    const PlayerMovementLoadResult result = loadPlayerMovement(packs);
    INFO(describeIssues(result.issues));
    REQUIRE(result.tuning);
    CHECK(result.issues.empty());
    const PlayerMovementTuning expected = aurora::test::standardTuning();
    CHECK(result.tuning->width == expected.width);
    CHECK(result.tuning->height == expected.height);
    CHECK(result.tuning->eyeHeight == expected.eyeHeight);
    CHECK(result.tuning->sneakEyeHeight == expected.sneakEyeHeight);
    CHECK(result.tuning->stepHeight == expected.stepHeight);
    CHECK(result.tuning->gravity == expected.gravity);
    CHECK(result.tuning->terminalVelocity == expected.terminalVelocity);
    CHECK(result.tuning->jumpVelocity == expected.jumpVelocity);
    CHECK(result.tuning->walkSpeed == expected.walkSpeed);
    CHECK(result.tuning->sprintSpeed == expected.sprintSpeed);
    CHECK(result.tuning->sneakSpeed == expected.sneakSpeed);
    CHECK(result.tuning->groundAcceleration == expected.groundAcceleration);
    CHECK(result.tuning->airAcceleration == expected.airAcceleration);
}
