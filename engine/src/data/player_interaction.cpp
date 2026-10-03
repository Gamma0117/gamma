#include "data/player_interaction.h"

#include "core/constants.h"
#include "core/log.h"
#include "data/json_support.h"
#include "data/pack_files.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <format>
#include <limits>
#include <map>
#include <string>
#include <string_view>

namespace aurora::data {

namespace {

using namespace json;
namespace fs = std::filesystem;

constexpr std::array<std::string_view, 9> kFieldNames{
    "reach",          "mining_seconds_per_hardness", "palette",       "particle_count", "particle_lifetime",
    "particle_gravity", "particle_speed",            "particle_size", "max_particles",
};

struct NumberRange {
    double low;
    bool lowInclusive;
    double high;
};

std::string describeRange(const NumberRange& range)
{
    return std::format("{}{}, {}]", range.lowInclusive ? "[" : "(", range.low, range.high);
}

const Json* requireField(const Json& root, std::string_view name, FileIssues& issues)
{
    const Json* value = findField(root, name);
    if (value == nullptr) {
        issues.error({}, std::format("missing required field '{}'", name));
    }
    return value;
}

// A finite number within the range, or an error.
std::optional<double> readNumber(const Json& root, std::string_view name, const NumberRange& range,
                                 FileIssues& issues)
{
    const Json* value = requireField(root, name, issues);
    if (value == nullptr) {
        return std::nullopt;
    }
    const double number = value->is_number() ? value->get<double>() : std::numeric_limits<double>::quiet_NaN();
    const bool aboveLow = range.lowInclusive ? number >= range.low : number > range.low;
    if (!value->is_number() || !std::isfinite(number) || !aboveLow || number > range.high) {
        issues.error(childPointer("", name),
                     std::format("expected a number in {}, got {}", describeRange(range), describe(*value)));
        return std::nullopt;
    }
    return number;
}

// A whole number within [low, high], or an error.
std::optional<std::uint32_t> readWholeNumber(const Json& root, std::string_view name, std::uint32_t low,
                                             std::uint32_t high, FileIssues& issues)
{
    const Json* value = requireField(root, name, issues);
    if (value == nullptr) {
        return std::nullopt;
    }
    if (value->is_number_integer()) {
        const auto number = value->get<std::int64_t>();
        if (number >= low && number <= high) {
            return static_cast<std::uint32_t>(number);
        }
    } else if (value->is_number_unsigned()) {
        const auto number = value->get<std::uint64_t>();
        if (number >= low && number <= high) {
            return static_cast<std::uint32_t>(number);
        }
    }
    issues.error(childPointer("", name),
                 std::format("expected a whole number from {} to {}, got {}", low, high, describe(*value)));
    return std::nullopt;
}

std::optional<std::vector<ResourceId>> readPalette(const Json& root, const BlockRegistry& registry,
                                                   FileIssues& issues)
{
    const Json* value = requireField(root, "palette", issues);
    if (value == nullptr) {
        return std::nullopt;
    }
    if (!value->is_array() || value->empty() || value->size() > kMaxPaletteSize) {
        issues.error("/palette", std::format("expected a list of 1 to {} block ids, got {}", kMaxPaletteSize,
                                             value->is_array() ? std::format("{} entries", value->size())
                                                               : describe(*value)));
        return std::nullopt;
    }
    std::vector<ResourceId> palette;
    bool valid = true;
    for (std::size_t index = 0; index < value->size(); ++index) {
        const Json& entry = (*value)[index];
        const std::string pointer = std::format("/palette/{}", index);
        const std::optional<ResourceId> id =
            entry.is_string() ? ResourceId::parse(entry.get_ref<const std::string&>()) : std::nullopt;
        if (!id || !id->isSingleSegment()) {
            issues.error(pointer, std::format("expected a block id like \"aurora:stone\", got {}", describe(entry)));
            valid = false;
            continue;
        }
        if (BlockRegistry::isReservedId(id->str())) {
            issues.error(pointer, std::format("{} cannot be placed", id->str()));
            valid = false;
            continue;
        }
        if (registry.findBlock(id->str()) == nullptr) {
            issues.error(pointer, std::format("unknown block {}", id->str()));
            valid = false;
            continue;
        }
        if (std::ranges::find(palette, *id) != palette.end()) {
            issues.error(pointer, std::format("{} is already in the palette", id->str()));
            valid = false;
            continue;
        }
        palette.push_back(*id);
    }
    if (!valid) {
        return std::nullopt;
    }
    return palette;
}

} // namespace

std::optional<std::uint32_t> miningTicks(float hardness, double secondsPerHardness)
{
    const double h = hardness;
    const double scale = secondsPerHardness * core::kTicksPerSecond;
    const double raw = h * scale;
    if (!std::isfinite(raw)) {
        return std::nullopt;
    }
    // Half the gap to the farther finite neighbouring float: how far the stored hardness may be from the number
    // that was written.
    double gap = 0.0;
    for (const float neighbour : {std::nextafter(hardness, -std::numeric_limits<float>::infinity()),
                                  std::nextafter(hardness, std::numeric_limits<float>::infinity())}) {
        if (std::isfinite(neighbour)) {
            gap = std::max(gap, std::abs(static_cast<double>(neighbour) - h));
        }
    }
    const double tolerance = 0.5 * gap * scale + 8.0 * DBL_EPSILON * std::max(1.0, std::abs(raw));
    const double ticks = std::max(1.0, std::ceil(raw - tolerance));
    if (!(ticks <= static_cast<double>(kMaxMiningTicks))) {
        return std::nullopt; // Checked before converting, so a huge value never overflows the integer.
    }
    return static_cast<std::uint32_t>(ticks);
}

PlayerInteractionLoadResult loadPlayerInteraction(std::span<const DataPack> packs, const BlockRegistry& registry)
{
    PlayerInteractionLoadResult result;
    const std::optional<fs::path> file =
        findLastPackFile(packs, fs::path("data") / std::string(kBaseNamespace) / "player" / "interaction.json",
                         "the player interaction settings", result.issues);
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
        issues.error({}, std::format("the player interaction file must hold one JSON object, got {}", describe(*root)));
        return result;
    }
    warnUnknownFields(*root, kFieldNames, "", issues);

    PlayerInteraction interaction;
    PlayerInteractionTuning& tuning = interaction.tuning;
    const auto assign = [](auto& target, const auto& value) {
        if (value) {
            target = *value;
        }
    };
    assign(tuning.reach, readNumber(*root, "reach", {kMinReach, true, kMaxReach}, issues));
    const std::optional<double> secondsPerHardness =
        readNumber(*root, "mining_seconds_per_hardness", {0.0, false, 30.0}, issues);
    assign(tuning.miningSecondsPerHardness, secondsPerHardness);
    assign(tuning.palette, readPalette(*root, registry, issues));
    assign(tuning.particleCount, readWholeNumber(*root, "particle_count", 8, 16, issues));
    assign(tuning.particleLifetime, readNumber(*root, "particle_lifetime", {0.0, false, 5.0}, issues));
    assign(tuning.particleGravity, readNumber(*root, "particle_gravity", {0.0, true, 200.0}, issues));
    assign(tuning.particleSpeed, readNumber(*root, "particle_speed", {0.0, true, 20.0}, issues));
    assign(tuning.particleSize, readNumber(*root, "particle_size", {0.0, false, 0.25}, issues));
    assign(tuning.maxParticles, readWholeNumber(*root, "max_particles", 16, 4096, issues));
    if (issues.hasErrors()) {
        return result;
    }

    for (const ResourceId& id : tuning.palette) {
        interaction.paletteStates.push_back(registry.findBlock(id.str())->defaultState);
    }
    interaction.miningTicks.assign(registry.stateCount(), 0);
    for (const BlockDefinition& block : registry.blocks()) {
        if (block.firstState == kAirState || block.firstState == kUnknownState || block.unbreakable) {
            continue;
        }
        const std::optional<std::uint32_t> ticks = miningTicks(block.hardness, *secondsPerHardness);
        if (!ticks) {
            issues.warning("/mining_seconds_per_hardness",
                           std::format("{} (hardness {}) would take more than {} ticks to break, so it cannot be "
                                       "broken",
                                       block.id.str(), block.hardness, kMaxMiningTicks));
            continue;
        }
        for (std::uint32_t state = 0; state < block.stateCount; ++state) {
            interaction.miningTicks[block.firstState + state] = *ticks;
        }
    }
    result.interaction = std::make_shared<const PlayerInteraction>(std::move(interaction));
    return result;
}

void logPlayerInteractionLoadResult(const PlayerInteractionLoadResult& result)
{
    logIssues(result.issues);
    if (result.interaction) {
        const PlayerInteractionTuning& tuning = result.interaction->tuning;
        core::logInfo("data", "Player interaction: reach {} blocks, {} s of mining per hardness, palette of {}",
                      tuning.reach, tuning.miningSecondsPerHardness, tuning.palette.size());
    } else {
        core::logError("data", "The player interaction settings have {} error(s); fix the lines above",
                       countIssues(result.issues, IssueSeverity::Error));
    }
}

CrackTextureLoadResult loadCrackTextures(std::span<const DataPack> packs)
{
    CrackTextureLoadResult result;
    std::vector<RgbaImage> stages;
    for (std::size_t stage = 0; stage < kCrackStageCount; ++stage) {
        const fs::path relative = fs::path("assets") / std::string(kBaseNamespace) / "textures" / "block" /
                                  std::format("destroy_stage_{}.png", stage);
        const std::optional<fs::path> file =
            findLastPackFile(packs, relative, std::format("crack stage {}", stage), result.issues);
        if (!file) {
            continue;
        }
        if (std::optional<RgbaImage> image = readBlockTextureFile(*file, result.issues)) {
            stages.push_back(std::move(*image));
        }
    }
    if (countIssues(result.issues, IssueSeverity::Error) == 0) {
        result.stages = std::move(stages);
    }
    return result;
}

void logCrackTextureLoadResult(const CrackTextureLoadResult& result)
{
    logIssues(result.issues);
    const std::size_t errors = countIssues(result.issues, IssueSeverity::Error);
    if (errors == 0) {
        core::logInfo("data", "Loaded {} crack textures", result.stages.size());
    } else {
        core::logError("data", "Crack textures have {} error(s); fix the lines above", errors);
    }
}

Rgb meanOpaqueColour(const RgbaImage& image)
{
    std::array<std::uint64_t, 3> sum{};
    std::uint64_t count = 0;
    const std::size_t pixels = static_cast<std::size_t>(image.width) * image.height;
    for (std::size_t i = 0; i < pixels && (i + 1) * 4 <= image.pixels.size(); ++i) {
        const std::uint8_t* texel = &image.pixels[i * 4];
        if (texel[3] < 128) {
            continue;
        }
        for (std::size_t channel = 0; channel < 3; ++channel) {
            sum[channel] += texel[channel];
        }
        ++count;
    }
    if (count == 0) {
        return kMissingParticleColour;
    }
    Rgb colour{};
    for (std::size_t channel = 0; channel < 3; ++channel) {
        colour[channel] = static_cast<std::uint8_t>((sum[channel] + count / 2) / count);
    }
    return colour;
}

std::vector<Rgb> blockParticleColours(const BlockRegistry& registry, std::span<const BlockTexture> textures)
{
    std::map<std::string_view, Rgb> byTexture;
    for (const BlockTexture& texture : textures) {
        byTexture.emplace(texture.id.str(), meanOpaqueColour(texture.image));
    }
    std::vector<Rgb> colours(registry.stateCount(), kMissingParticleColour);
    for (const BlockDefinition& block : registry.blocks()) {
        const ResourceId& north = block.faceTextures[static_cast<std::size_t>(BlockFace::North)];
        const auto found = byTexture.find(north.str());
        if (north.empty() || found == byTexture.end()) {
            continue;
        }
        for (std::uint32_t state = 0; state < block.stateCount; ++state) {
            colours[block.firstState + state] = found->second;
        }
    }
    return colours;
}

} // namespace aurora::data
