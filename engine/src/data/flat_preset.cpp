#include "data/flat_preset.h"

#include "core/constants.h"
#include "core/log.h"
#include "core/utf8.h"
#include "data/json_support.h"
#include "data/path_probe.h"

#include <format>
#include <optional>
#include <string>

namespace aurora::data {

namespace {

using namespace json;
namespace fs = std::filesystem;

constexpr std::array<std::string_view, 1> kRootFields{"layers"};
constexpr std::array<std::string_view, 2> kLayerFields{"block", "height"};

fs::path presetPath(const DataPack& pack)
{
    return pack.root / "data" / std::string(kBaseNamespace) / "worldgen" / "flat.json";
}

// The file from the last pack that has one. Anything unusable in its place (broken link, a folder) is an error
// rather than a reason to fall back to an earlier pack.
std::optional<fs::path> findPresetFile(std::span<const DataPack> packs, std::vector<LoadIssue>& issues)
{
    for (auto pack = packs.rbegin(); pack != packs.rend(); ++pack) {
        const fs::path candidate = presetPath(*pack);
        const PathProbe probe = probePath(candidate);
        switch (probe.kind) {
        case PathKind::File:
            return candidate;
        case PathKind::Missing:
            continue;
        case PathKind::Failed:
            issues.push_back({IssueSeverity::Error, candidate, {}, std::format("cannot read: {}", probe.failure)});
            return std::nullopt;
        case PathKind::Directory:
        case PathKind::Other:
            issues.push_back({IssueSeverity::Error, candidate, {}, "expected a file"});
            return std::nullopt;
        }
    }
    const fs::path expected = packs.empty() ? fs::path("data/aurora/worldgen/flat.json") : presetPath(packs.front());
    issues.push_back({IssueSeverity::Error, expected, {}, "missing required file (the flat world preset)"});
    return std::nullopt;
}

void warnUnknownFields(const Json& object, std::span<const std::string_view> known, const std::string& pointer,
                       FileIssues& issues)
{
    for (const auto& [key, value] : object.items()) {
        if (std::ranges::find(known, key) == known.end()) {
            issues.warning(childPointer(pointer, key), std::format("unknown field '{}' (ignored)", key));
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
    const std::optional<fs::path> file = findPresetFile(packs, result.issues);
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

        std::optional<BlockStateId> state;
        if (const Json* block = findField(layer, "block"); block == nullptr) {
            issues.error(pointer, "missing required field 'block'");
        } else if (!block->is_string()) {
            issues.error(pointer + "/block", std::format("expected a block state string, got {}", describe(*block)));
        } else {
            const BlockRegistry::ParseResult parsed = registry.parseState(block->get_ref<const std::string&>());
            if (!parsed.state) {
                issues.error(pointer + "/block", parsed.error);
            }
            state = parsed.state;
        }

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

    if (!issues.hasErrors()) {
        result.preset = std::move(preset);
    }
    return result;
}

void logFlatPresetLoadResult(const FlatPresetLoadResult& result)
{
    logIssues(result.issues);
    if (result.preset) {
        core::logInfo("data", "Flat world preset: {} layers, {} blocks high (top layer ends at y {})",
                      result.preset->layers.size(), result.preset->totalHeight(),
                      core::kWorldMinY + static_cast<std::int32_t>(result.preset->totalHeight()) - 1);
    } else {
        core::logError("data", "The flat world preset has {} error(s); fix the lines above",
                       countIssues(result.issues, IssueSeverity::Error));
    }
}

} // namespace aurora::data
