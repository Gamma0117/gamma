#include "data/player_movement.h"

#include "core/log.h"
#include "data/json_support.h"
#include "data/pack_files.h"

#include <array>
#include <cmath>
#include <format>
#include <optional>
#include <string>
#include <string_view>

namespace aurora::data {

namespace {

using namespace json;
namespace fs = std::filesystem;

// The fields and their own ranges; the ranges that depend on another field are checked afterwards.
struct Field {
    std::string_view name;
    double PlayerMovementTuning::*member;
    double low;
    bool lowInclusive;
    double high;
};

constexpr std::array kFields{
    Field{"width", &PlayerMovementTuning::width, kMinPlayerSize, true, kMaxPlayerSize},
    Field{"height", &PlayerMovementTuning::height, kMinPlayerSize, true, kMaxPlayerSize},
    Field{"eye_height", &PlayerMovementTuning::eyeHeight, 0.0, false, kMaxPlayerSize},
    Field{"sneak_eye_height", &PlayerMovementTuning::sneakEyeHeight, 0.0, false, kMaxPlayerSize},
    Field{"step_height", &PlayerMovementTuning::stepHeight, 0.0, true, kMaxPlayerSize},
    Field{"gravity", &PlayerMovementTuning::gravity, 0.0, true, 200.0},
    Field{"terminal_velocity", &PlayerMovementTuning::terminalVelocity, 0.0, false, 200.0},
    Field{"jump_velocity", &PlayerMovementTuning::jumpVelocity, 0.0, true, 50.0},
    Field{"walk_speed", &PlayerMovementTuning::walkSpeed, 0.0, true, 50.0},
    Field{"sprint_speed", &PlayerMovementTuning::sprintSpeed, 0.0, true, 50.0},
    Field{"sneak_speed", &PlayerMovementTuning::sneakSpeed, 0.0, true, 50.0},
    Field{"ground_acceleration", &PlayerMovementTuning::groundAcceleration, 0.0, false, 1.0},
    Field{"air_acceleration", &PlayerMovementTuning::airAcceleration, 0.0, false, 1.0},
};

constexpr std::array<std::string_view, kFields.size()> fieldNames()
{
    std::array<std::string_view, kFields.size()> names{};
    for (std::size_t i = 0; i < kFields.size(); ++i) {
        names[i] = kFields[i].name;
    }
    return names;
}

constexpr std::array kFieldNames = fieldNames();

std::string describeRange(const Field& field)
{
    return std::format("{}{}, {}]", field.lowInclusive ? "[" : "(", field.low, field.high);
}

// A finite number within the field's range, or an error.
std::optional<double> readField(const Json& root, const Field& field, FileIssues& issues)
{
    const Json* value = findField(root, field.name);
    if (value == nullptr) {
        issues.error({}, std::format("missing required field '{}'", field.name));
        return std::nullopt;
    }
    const std::string pointer = childPointer("", field.name);
    if (!value->is_number()) {
        issues.error(pointer, std::format("expected a number in {}, got {}", describeRange(field), describe(*value)));
        return std::nullopt;
    }
    const double number = value->get<double>();
    const bool aboveLow = field.lowInclusive ? number >= field.low : number > field.low;
    if (!std::isfinite(number) || !aboveLow || number > field.high) {
        issues.error(pointer, std::format("expected a number in {}, got {}", describeRange(field), describe(*value)));
        return std::nullopt;
    }
    return number;
}

} // namespace

PlayerMovementLoadResult loadPlayerMovement(std::span<const DataPack> packs)
{
    PlayerMovementLoadResult result;
    const std::optional<fs::path> file =
        findLastPackFile(packs, fs::path("data") / std::string(kBaseNamespace) / "player" / "movement.json",
                         "the player movement settings", result.issues);
    if (!file) {
        return result;
    }
    result.file = *file;

    FileIssues issues(result.issues, *file);
    const std::optional<std::string> text = readFile(*file, issues);
    const std::optional<Json> root = text ? parseJson(*text, issues) : std::nullopt;
    if (!root) {
        return result;
    }
    if (!root->is_object()) {
        issues.error({}, std::format("the player movement file must hold one JSON object, got {}", describe(*root)));
        return result;
    }
    warnUnknownFields(*root, kFieldNames, "", issues);

    PlayerMovementTuning tuning;
    std::array<bool, kFields.size()> valid{};
    for (std::size_t i = 0; i < kFields.size(); ++i) {
        if (const std::optional<double> value = readField(*root, kFields[i], issues)) {
            tuning.*kFields[i].member = *value;
            valid[i] = true;
        }
    }
    const auto validField = [&](std::string_view name) {
        for (std::size_t i = 0; i < kFields.size(); ++i) {
            if (kFields[i].name == name) {
                return valid[i];
            }
        }
        return false;
    };

    // Ranges that depend on another field, checked when both are valid on their own.
    if (validField("eye_height") && validField("height") && tuning.eyeHeight > tuning.height) {
        issues.error("/eye_height",
                     std::format("must not be above height ({}): got {}", tuning.height, tuning.eyeHeight));
    }
    if (validField("sneak_eye_height") && validField("eye_height") && tuning.sneakEyeHeight > tuning.eyeHeight) {
        issues.error("/sneak_eye_height",
                     std::format("must not be above eye_height ({}): got {}", tuning.eyeHeight, tuning.sneakEyeHeight));
    }
    if (validField("step_height") && validField("height") && tuning.stepHeight >= tuning.height) {
        issues.error("/step_height",
                     std::format("must be below height ({}): got {}", tuning.height, tuning.stepHeight));
    }

    if (!issues.hasErrors()) {
        result.tuning = std::make_shared<const PlayerMovementTuning>(tuning);
    }
    return result;
}

void logPlayerMovementLoadResult(const PlayerMovementLoadResult& result)
{
    logIssues(result.issues);
    if (result.tuning) {
        const PlayerMovementTuning& tuning = *result.tuning;
        core::logInfo("data", "Player movement: {} x {} blocks, walk {} / sprint {} / sneak {} blocks per second",
                      tuning.width, tuning.height, tuning.walkSpeed, tuning.sprintSpeed, tuning.sneakSpeed);
    } else {
        core::logError("data", "The player movement settings have {} error(s); fix the lines above",
                       countIssues(result.issues, IssueSeverity::Error));
    }
}

} // namespace aurora::data
