#include "data/flat_preset.h"

#include "core/constants.h"
#include "core/log.h"
#include "core/utf8.h"
#include "data/json_support.h"
#include "data/pack_files.h"

#include <format>
#include <optional>
#include <string>

namespace aurora::data {

namespace {

using namespace json;
namespace fs = std::filesystem;

constexpr std::array<std::string_view, 2> kRootFields{"layers", "boxes"};
constexpr std::array<std::string_view, 2> kLayerFields{"block", "height"};
constexpr std::array<std::string_view, 3> kBoxFields{"block", "from", "to"};
constexpr std::array<char, 3> kAxisNames{'x', 'y', 'z'};

std::optional<BlockStateId> readState(const Json& object, const std::string& pointer, const BlockRegistry& registry,
                                      FileIssues& issues)
{
    const Json* block = findField(object, "block");
    if (block == nullptr) {
        issues.error(pointer, "missing required field 'block'");
        return std::nullopt;
    }
    if (!block->is_string()) {
        issues.error(pointer + "/block", std::format("expected a block state string, got {}", describe(*block)));
        return std::nullopt;
    }
    const BlockRegistry::ParseResult parsed = registry.parseState(block->get_ref<const std::string&>());
    if (!parsed.state) {
        issues.error(pointer + "/block", parsed.error);
    }
    return parsed.state;
}

// A whole number within [low, high]. Floats (2.0, 1e3) are errors even when whole.
std::optional<std::int32_t> readCoordinate(const Json& value, const std::string& pointer, std::int64_t low,
                                           std::int64_t high, FileIssues& issues)
{
    std::optional<std::int64_t> number;
    if (value.is_number_unsigned()) {
        const std::uint64_t unsignedValue = value.get<std::uint64_t>();
        number = unsignedValue > static_cast<std::uint64_t>(high) ? high + 1 : static_cast<std::int64_t>(unsignedValue);
    } else if (value.is_number_integer()) {
        number = value.get<std::int64_t>();
    }
    if (!number || *number < low || *number > high) {
        issues.error(pointer, std::format("expected a whole number from {} to {}, got {}", low, high, describe(value)));
        return std::nullopt;
    }
    return static_cast<std::int32_t>(*number);
}

std::optional<std::array<std::int32_t, 3>> readCorner(const Json& box, std::string_view name,
                                                      const std::string& pointer, FileIssues& issues)
{
    const Json* corner = findField(box, name);
    const std::string cornerPointer = childPointer(pointer, name);
    if (corner == nullptr) {
        issues.error(pointer, std::format("missing required field '{}'", name));
        return std::nullopt;
    }
    if (!corner->is_array() || corner->size() != 3) {
        issues.error(cornerPointer, std::format("expected [x, y, z], got {}", describe(*corner)));
        return std::nullopt;
    }
    std::array<std::int32_t, 3> result{};
    bool valid = true;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const bool vertical = axis == 1;
        const std::int64_t low = vertical ? core::kWorldMinY : -std::int64_t{kMaxFlatBoxCoordinate};
        const std::int64_t high = vertical ? core::kWorldMaxY - 1 : std::int64_t{kMaxFlatBoxCoordinate};
        const std::optional<std::int32_t> value =
            readCoordinate((*corner)[axis], std::format("{}/{}", cornerPointer, axis), low, high, issues);
        valid = valid && value.has_value();
        result[axis] = value.value_or(0);
    }
    return valid ? std::optional(result) : std::nullopt;
}

void readBoxes(const Json& boxes, const BlockRegistry& registry, FlatPreset& preset, FileIssues& issues)
{
    if (!boxes.is_array()) {
        issues.error("/boxes", std::format("expected an array of boxes, got {}", describe(boxes)));
        return;
    }
    if (boxes.size() > kMaxFlatBoxes) {
        issues.error("/boxes", std::format("at most {} boxes, got {}", kMaxFlatBoxes, boxes.size()));
        return;
    }
    for (std::size_t i = 0; i < boxes.size(); ++i) {
        const Json& box = boxes[i];
        const std::string pointer = std::format("/boxes/{}", i);
        if (!box.is_object()) {
            issues.error(pointer, std::format("expected an object {{\"block\": ..., \"from\": ..., \"to\": ...}}, "
                                              "got {}",
                                              describe(box)));
            continue;
        }
        warnUnknownFields(box, kBoxFields, pointer, issues);
        const std::optional<BlockStateId> state = readState(box, pointer, registry, issues);
        const auto from = readCorner(box, "from", pointer, issues);
        const auto to = readCorner(box, "to", pointer, issues);
        bool ordered = true;
        if (from && to) {
            for (std::size_t axis = 0; axis < 3; ++axis) {
                if ((*from)[axis] > (*to)[axis]) {
                    issues.error(std::format("{}/to/{}", pointer, axis),
                                 std::format("{} must not be below from ({}): got {}", kAxisNames[axis],
                                             (*from)[axis], (*to)[axis]));
                    ordered = false;
                }
            }
        }
        if (state && from && to && ordered) {
            preset.boxes.push_back({*state, *from, *to});
        }
    }
}

} // namespace

std::uint32_t FlatPreset::totalHeight() const
{
    std::uint32_t total = 0;
    for (const FlatLayer& layer : layers) {
        total += layer.height;
    }
    return total;
}

FlatPresetLoadResult loadFlatPreset(std::span<const DataPack> packs, const BlockRegistry& registry)
{
    FlatPresetLoadResult result;
    const std::optional<fs::path> file =
        findLastPackFile(packs, fs::path("data") / std::string(kBaseNamespace) / "worldgen" / "flat.json",
                         "the flat world preset", result.issues);
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
        issues.error({}, std::format("a flat preset file must hold one JSON object, got {}", describe(*root)));
        return result;
    }
    warnUnknownFields(*root, kRootFields, "", issues);

    const Json* layers = findField(*root, "layers");
    if (layers == nullptr) {
        issues.error({}, "missing required field 'layers'");
        return result;
    }
    if (!layers->is_array() || layers->empty()) {
        issues.error("/layers", std::format("expected a non-empty array of layers, got {}", describe(*layers)));
        return result;
    }

    auto preset = std::make_shared<FlatPreset>();
    std::uint64_t remaining = core::kWorldHeight;
    for (std::size_t i = 0; i < layers->size(); ++i) {
        const Json& layer = (*layers)[i];
        const std::string pointer = std::format("/layers/{}", i);
        if (!layer.is_object()) {
            issues.error(pointer, std::format("expected an object {{\"block\": ..., \"height\": ...}}, got {}",
                                              describe(layer)));
            continue;
        }
        warnUnknownFields(layer, kLayerFields, pointer, issues);

        const std::optional<BlockStateId> state = readState(layer, pointer, registry, issues);

        std::optional<std::uint32_t> height;
        if (const Json* value = findField(layer, "height"); value == nullptr) {
            issues.error(pointer, "missing required field 'height'");
        } else if (!value->is_number_unsigned() || value->get<std::uint64_t>() == 0) {
            issues.error(pointer + "/height", std::format("expected a whole number >= 1, got {}", describe(*value)));
        } else if (const std::uint64_t wanted = value->get<std::uint64_t>(); wanted > remaining) {
            issues.error(pointer + "/height",
                         std::format("the layers would reach above the top of the world: this layer needs {}, only "
                                     "{} of {} are left",
                                     wanted, remaining, core::kWorldHeight));
            remaining = 0;
        } else {
            remaining -= wanted;
            height = static_cast<std::uint32_t>(wanted);
        }

        if (state && height) {
            preset->layers.push_back({*state, *height});
        }
    }

    if (const Json* boxes = findField(*root, "boxes")) {
        readBoxes(*boxes, registry, *preset, issues);
    }

    if (!issues.hasErrors()) {
        result.preset = std::move(preset);
    }
    return result;
}

void logFlatPresetLoadResult(const FlatPresetLoadResult& result)
{
    logIssues(result.issues);
    if (result.preset) {
        core::logInfo("data", "Flat world preset: {} layers, {} blocks high (top layer ends at y {}), {} boxes",
                      result.preset->layers.size(), result.preset->totalHeight(),
                      core::kWorldMinY + static_cast<std::int32_t>(result.preset->totalHeight()) - 1,
                      result.preset->boxes.size());
    } else {
        core::logError("data", "The flat world preset has {} error(s); fix the lines above",
                       countIssues(result.issues, IssueSeverity::Error));
    }
}

} // namespace aurora::data
