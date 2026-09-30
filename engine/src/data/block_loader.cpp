#include "data/block_loader.h"

#include "core/log.h"
#include "core/profiler.h"
#include "core/utf8.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace aurora::data {

namespace {

using Json = nlohmann::json;
namespace fs = std::filesystem;

// Fields a block file may have in this stage.
constexpr std::array<std::string_view, 10> kKnownFields{
    "default_state", "explosion_resistance", "hardness", "id", "light",
    "render",        "solid",                "states",   "textures", "unbreakable",
};
// Designed fields that later stages will read (tools and loot in P1, generation in P0-8). Accepted without checks
// until then, so data can be written ahead.
constexpr std::array<std::string_view, 4> kDeferredFields{"drops", "generation", "min_tool_rank", "tool"};
constexpr std::array<std::string_view, 9> kTextureKeys{
    "all", "bottom", "east", "emissive", "north", "side", "south", "top", "west",
};
constexpr std::size_t kMaxQuotedLength = 40;

template <std::size_t N>
bool contains(const std::array<std::string_view, N>& list, std::string_view value)
{
    return std::ranges::find(list, value) != list.end();
}

// RFC 6901 token escaping, so reported pointers stay unambiguous.
std::string pointerToken(std::string_view token)
{
    std::string escaped;
    for (const char c : token) {
        if (c == '~') {
            escaped += "~0";
        } else if (c == '/') {
            escaped += "~1";
        } else {
            escaped += c;
        }
    }
    return escaped;
}

std::string childPointer(std::string_view parent, std::string_view key)
{
    return std::format("{}/{}", parent, pointerToken(key));
}

// "string \"abc\"", "number 7.5", "array", ... for messages.
std::string describe(const Json& value)
{
    if (value.is_object() || value.is_array()) {
        return value.type_name();
    }
    std::string text = value.dump(-1, ' ', false, Json::error_handler_t::replace);
    if (text.size() > kMaxQuotedLength) {
        text = text.substr(0, kMaxQuotedLength) + "...";
    }
    return std::format("{} {}", value.type_name(), text);
}

const Json* findField(const Json& object, std::string_view key)
{
    const auto it = object.find(key);
    return it == object.end() ? nullptr : &*it;
}

// Issues of one file.
class FileIssues {
public:
    FileIssues(std::vector<LoadIssue>& issues, fs::path file)
        : m_issues(issues)
        , m_file(std::move(file))
    {
    }

    void error(std::string pointer, std::string message)
    {
        m_issues.push_back({IssueSeverity::Error, m_file, std::move(pointer), std::move(message)});
        m_hasErrors = true;
    }
    void warning(std::string pointer, std::string message)
    {
        m_issues.push_back({IssueSeverity::Warning, m_file, std::move(pointer), std::move(message)});
    }
    void info(std::string pointer, std::string message)
    {
        m_issues.push_back({IssueSeverity::Info, m_file, std::move(pointer), std::move(message)});
    }

    bool hasErrors() const { return m_hasErrors; }
    const fs::path& file() const { return m_file; }

private:
    std::vector<LoadIssue>& m_issues;
    fs::path m_file;
    bool m_hasErrors = false;
};

// Parser callback that reports a key repeated within one object, at any depth. nlohmann would silently keep
// only the last value.
class DuplicateKeyCheck {
public:
    explicit DuplicateKeyCheck(FileIssues& issues)
        : m_issues(&issues)
    {
    }

    bool operator()(int /*depth*/, Json::parse_event_t event, Json& parsed)
    {
        switch (event) {
        case Json::parse_event_t::object_start:
        case Json::parse_event_t::array_start:
            m_frames.emplace_back();
            m_frames.back().isArray = event == Json::parse_event_t::array_start;
            break;
        case Json::parse_event_t::key:
            if (!m_frames.empty() && parsed.is_string()) {
                Frame& frame = m_frames.back();
                frame.key = parsed.get<std::string>();
                if (!frame.keys.insert(frame.key).second) {
                    m_issues->error(currentPointer(), std::format("duplicate key '{}'", frame.key));
                }
            }
            break;
        case Json::parse_event_t::value:
            elementDone();
            break;
        case Json::parse_event_t::object_end:
        case Json::parse_event_t::array_end:
            if (!m_frames.empty()) {
                m_frames.pop_back();
            }
            elementDone();
            break;
        }
        return true;
    }

private:
    struct Frame {
        bool isArray = false;
        std::size_t index = 0;
        std::string key;
        std::set<std::string> keys;
    };

    void elementDone()
    {
        if (!m_frames.empty() && m_frames.back().isArray) {
            ++m_frames.back().index;
        }
    }

    std::string currentPointer() const
    {
        std::string pointer;
        for (const Frame& frame : m_frames) {
            pointer += '/';
            pointer += frame.isArray ? std::to_string(frame.index) : pointerToken(frame.key);
        }
        return pointer;
    }

    FileIssues* m_issues;
    std::vector<Frame> m_frames;
};

std::optional<std::string> readFile(const fs::path& file, FileIssues& issues)
{
    std::ifstream stream(file, std::ios::binary);
    if (!stream) {
        issues.error({}, "cannot open the file");
        return std::nullopt;
    }
    std::string text{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    if (stream.bad()) {
        issues.error({}, "cannot read the file");
        return std::nullopt;
    }
    return text;
}

std::optional<Json> parseJson(const std::string& text, FileIssues& issues)
{
    // Library exceptions stop at this boundary and become issues of this file.
    try {
        return Json::parse(text, DuplicateKeyCheck(issues));
    } catch (const Json::exception& e) {
        std::string_view message = e.what();
        if (const std::size_t tagEnd = message.find("] "); message.starts_with("[json.") && tagEnd != message.npos) {
            message.remove_prefix(tagEnd + 2);
        }
        issues.error({}, std::string(message));
        return std::nullopt;
    }
}

// A finite number >= 0 that fits in a float.
std::optional<float> readNonNegative(const Json& value, const std::string& pointer, FileIssues& issues)
{
    if (!value.is_number()) {
        issues.error(pointer, std::format("expected a number >= 0, got {}", describe(value)));
        return std::nullopt;
    }
    const double number = value.get<double>();
    if (!std::isfinite(number) || number < 0.0 || number > std::numeric_limits<float>::max()) {
        issues.error(pointer, std::format("expected a finite number >= 0, got {}", describe(value)));
        return std::nullopt;
    }
    return static_cast<float>(number);
}

std::optional<bool> readBool(const Json& value, const std::string& pointer, FileIssues& issues)
{
    if (!value.is_boolean()) {
        issues.error(pointer, std::format("expected true or false, got {}", describe(value)));
        return std::nullopt;
    }
    return value.get<bool>();
}

void readId(const Json& root, BlockDefinition& block, FileIssues& issues)
{
    const Json* value = findField(root, "id");
    if (value == nullptr) {
        issues.error({}, "missing required field 'id'");
        return;
    }
    if (!value->is_string()) {
        issues.error("/id", std::format("expected a string, got {}", describe(*value)));
        return;
    }
    const std::string& text = value->get_ref<const std::string&>();
    const std::optional<ResourceId> id = ResourceId::parse(text);
    if (!id) {
        issues.error("/id", std::format("'{}' is not a valid id: use namespace:name with [a-z0-9_] only", text));
    } else if (!id->isSingleSegment()) {
        issues.error("/id", std::format("block id '{}' must not contain '/'", text));
    } else if (BlockRegistry::isReservedId(text)) {
        issues.error("/id", std::format("'{}' is built into the engine and cannot be defined in data", text));
    } else {
        block.id = *id;
    }
}

void readBreaking(const Json& root, BlockDefinition& block, FileIssues& issues)
{
    if (const Json* value = findField(root, "unbreakable")) {
        block.unbreakable = readBool(*value, "/unbreakable", issues).value_or(false);
    }
    const Json* hardness = findField(root, "hardness");
    if (block.unbreakable && hardness != nullptr) {
        issues.error("/hardness", "an unbreakable block has no hardness; remove one of the two fields");
    } else if (!block.unbreakable && hardness == nullptr) {
        issues.error({}, "missing required field 'hardness' (or set \"unbreakable\": true)");
    } else if (hardness != nullptr) {
        block.hardness = readNonNegative(*hardness, "/hardness", issues).value_or(0.0f);
    }
    if (const Json* value = findField(root, "explosion_resistance")) {
        block.explosionResistance = readNonNegative(*value, "/explosion_resistance", issues);
    }
}

void readLook(const Json& root, BlockDefinition& block, FileIssues& issues)
{
    if (const Json* value = findField(root, "light")) {
        if (!value->is_number_unsigned() || value->get<std::uint64_t>() > 15) {
            issues.error("/light", std::format("expected a whole number 0-15, got {}", describe(*value)));
        } else {
            block.light = static_cast<std::uint8_t>(value->get<std::uint64_t>());
        }
    }
    if (const Json* value = findField(root, "render")) {
        const std::string text = value->is_string() ? value->get<std::string>() : std::string{};
        if (text == "opaque") {
            block.render = RenderLayer::Opaque;
        } else if (text == "cutout") {
            block.render = RenderLayer::Cutout;
        } else if (text == "translucent") {
            block.render = RenderLayer::Translucent;
        } else {
            issues.error("/render",
                         std::format("expected \"opaque\", \"cutout\" or \"translucent\", got {}", describe(*value)));
        }
    }
    if (const Json* value = findField(root, "solid")) {
        block.solid = readBool(*value, "/solid", issues).value_or(true);
    }
}

std::optional<fs::path> findTextureFile(std::span<const DataPack> packs, const ResourceId& texture)
{
    const fs::path relative = fs::path("assets") / std::string(texture.nameSpace()) / "textures" /
                              (std::string(texture.path()) + ".png");
    for (auto pack = packs.rbegin(); pack != packs.rend(); ++pack) {
        std::error_code error;
        const fs::path candidate = pack->root / relative;
        if (fs::is_regular_file(candidate, error)) {
            return candidate;
        }
    }
    return std::nullopt;
}

void readTextures(const Json& root, std::string_view dataNamespace, std::span<const DataPack> packs,
                  BlockDefinition& block, FileIssues& issues)
{
    const Json* textures = findField(root, "textures");
    if (textures == nullptr) {
        issues.error({}, "missing required field 'textures'");
        return;
    }
    if (!textures->is_object()) {
        issues.error("/textures", std::format("expected an object of face -> texture, got {}", describe(*textures)));
        return;
    }

    std::map<std::string, ResourceId, std::less<>> refs;
    bool valid = true;
    for (const auto& [key, value] : textures->items()) {
        const std::string pointer = childPointer("/textures", key);
        if (!contains(kTextureKeys, key)) {
            issues.warning(pointer, std::format("unknown texture slot '{}' (ignored); use all, side, top, bottom, "
                                                "north, south, east, west or emissive",
                                                key));
            continue;
        }
        const std::optional<ResourceId> ref =
            value.is_string() ? ResourceId::parse(value.get<std::string>(), dataNamespace) : std::nullopt;
        if (!ref) {
            issues.error(pointer, std::format("expected a texture like \"block/stone\" or \"aurora:block/stone\" "
                                              "([a-z0-9_] and '/'), got {}",
                                              describe(value)));
            valid = false;
            continue;
        }
        refs.emplace(key, *ref);
    }
    if (!valid) {
        return;
    }

    const auto pick = [&refs](std::string_view key) -> const ResourceId* {
        const auto it = refs.find(key);
        return it == refs.end() ? nullptr : &it->second;
    };
    // Each face takes its own key, then 'side' for the four side faces, then 'all'.
    struct FaceRule {
        BlockFace face;
        std::string_view key;
        bool isSide;
    };
    constexpr std::array<FaceRule, kBlockFaceCount> kFaceRules{{
        {BlockFace::Down, "bottom", false},
        {BlockFace::Up, "top", false},
        {BlockFace::North, "north", true},
        {BlockFace::South, "south", true},
        {BlockFace::West, "west", true},
        {BlockFace::East, "east", true},
    }};
    for (const FaceRule& rule : kFaceRules) {
        const ResourceId* texture = pick(rule.key);
        if (texture == nullptr && rule.isSide) {
            texture = pick("side");
        }
        if (texture == nullptr) {
            texture = pick("all");
        }
        if (texture == nullptr) {
            issues.error("/textures", std::format("no texture for the {} face; set '{}', {}'all'", rule.key, rule.key,
                                                  rule.isSide ? "'side' or " : ""));
            continue;
        }
        block.faceTextures[static_cast<std::size_t>(rule.face)] = *texture;
    }
    if (const ResourceId* emissive = pick("emissive")) {
        block.emissiveTexture = *emissive;
    }

    std::set<std::string> checked;
    for (const auto& [key, texture] : refs) {
        if (!checked.insert(texture.str()).second || findTextureFile(packs, texture)) {
            continue;
        }
        issues.error(childPointer("/textures", key),
                     std::format("texture {} not found: no data pack has assets/{}/textures/{}.png", texture.str(),
                                 texture.nameSpace(), texture.path()));
    }
}

void readStates(const Json& root, BlockDefinition& block, FileIssues& issues)
{
    const Json* states = findField(root, "states");
    const Json* defaults = findField(root, "default_state");
    bool valid = true;

    if (states != nullptr) {
        if (!states->is_object()) {
            issues.error("/states", std::format("expected an object of property -> list of values, got {}",
                                                describe(*states)));
            return;
        }
        for (const auto& [name, values] : states->items()) {
            const std::string pointer = childPointer("/states", name);
            if (!isValidName(name)) {
                issues.error(pointer, std::format("property name '{}' must use [a-z0-9_] only", name));
                valid = false;
                continue;
            }
            if (!values.is_array() || values.empty()) {
                issues.error(pointer, std::format("expected a non-empty array of values, got {}", describe(values)));
                valid = false;
                continue;
            }
            BlockProperty property{name, {}};
            for (std::size_t i = 0; i < values.size(); ++i) {
                const Json& value = values[i];
                const std::string itemPointer = std::format("{}/{}", pointer, i);
                if (!value.is_string() || !isValidName(value.get<std::string>())) {
                    issues.error(itemPointer, std::format("values must be [a-z0-9_] strings, got {}", describe(value)));
                    valid = false;
                    continue;
                }
                const std::string text = value.get<std::string>();
                if (std::ranges::find(property.values, text) != property.values.end()) {
                    issues.error(itemPointer, std::format("value '{}' is listed twice", text));
                    valid = false;
                    continue;
                }
                property.values.push_back(text);
            }
            block.properties.push_back(std::move(property));
        }
        std::ranges::sort(block.properties, {}, &BlockProperty::name);
        if (valid && !countStates(block.properties, kMaxDataBlockStates)) {
            issues.error("/states", std::format("more than {} state combinations; that is the limit for all data "
                                                "blocks together",
                                                kMaxDataBlockStates));
            return;
        }
    }
    if (!valid) {
        return;
    }

    block.defaultValues.assign(block.properties.size(), 0);
    if (defaults == nullptr) {
        return;
    }
    if (!defaults->is_object()) {
        issues.error("/default_state",
                     std::format("expected an object of property -> value, got {}", describe(*defaults)));
        return;
    }
    for (const auto& [name, value] : defaults->items()) {
        const std::string pointer = childPointer("/default_state", name);
        const auto property = std::ranges::find(block.properties, name, &BlockProperty::name);
        if (property == block.properties.end()) {
            issues.error(pointer, std::format("'{}' is not a property listed in 'states'", name));
            continue;
        }
        const std::string text = value.is_string() ? value.get<std::string>() : std::string{};
        const auto match = std::ranges::find(property->values, text);
        if (match == property->values.end()) {
            std::string allowed;
            for (const std::string& option : property->values) {
                allowed += allowed.empty() ? option : ", " + option;
            }
            issues.error(pointer, std::format("expected one of {}, got {}", allowed, describe(value)));
            continue;
        }
        block.defaultValues[static_cast<std::size_t>(property - block.properties.begin())] =
            static_cast<std::uint32_t>(match - property->values.begin());
    }
}

// Checks one block file and builds its definition; nullopt if the file has any error (all of them reported).
std::optional<BlockDefinition> readBlock(const Json& root, std::string_view dataNamespace,
                                         std::span<const DataPack> packs, const DataPack& pack, FileIssues& issues)
{
    if (!root.is_object()) {
        issues.error({}, std::format("a block file must hold one JSON object, got {}", describe(root)));
        return std::nullopt;
    }
    for (const auto& [key, value] : root.items()) {
        if (!contains(kKnownFields, key) && !contains(kDeferredFields, key)) {
            issues.warning(childPointer("", key), std::format("unknown field '{}' (ignored)", key));
        }
    }

    BlockDefinition block;
    block.sourceFile = issues.file();
    block.sourcePack = pack.name;
    readId(root, block, issues);
    readBreaking(root, block, issues);
    readLook(root, block, issues);
    readTextures(root, dataNamespace, packs, block, issues);
    readStates(root, block, issues);
    if (issues.hasErrors()) {
        return std::nullopt;
    }
    return block;
}

// Sorted entries of `folder` that are sub-folders, or regular files with `extension`. A missing folder is empty.
std::vector<fs::path> listFolder(const fs::path& folder, bool wantFolders, std::string_view extension,
                                 std::vector<LoadIssue>& issues)
{
    std::vector<fs::path> entries;
    std::error_code error;
    if (!fs::is_directory(folder, error)) {
        return entries;
    }
    fs::directory_iterator it(folder, error);
    for (; !error && it != fs::directory_iterator(); it.increment(error)) {
        const bool isFolder = it->is_directory(error);
        if (wantFolders ? isFolder : (!isFolder && it->is_regular_file(error) && it->path().extension() == extension)) {
            entries.push_back(it->path());
        }
    }
    if (error) {
        issues.push_back(
            {IssueSeverity::Error, folder, {}, std::format("cannot read the folder: {}", error.message())});
    }
    std::ranges::sort(entries);
    return entries;
}

} // namespace

BlockLoadResult loadBlocks(std::span<const DataPack> packs)
{
    AURORA_PROFILE_ZONE();
    BlockLoadResult result;

    struct Loaded {
        BlockDefinition definition;
        std::size_t packIndex = 0;
    };
    std::map<std::string, Loaded, std::less<>> finalBlocks; // By id, after replacements.

    for (std::size_t packIndex = 0; packIndex < packs.size(); ++packIndex) {
        const DataPack& pack = packs[packIndex];
        std::error_code error;
        if (!fs::is_directory(pack.root, error)) {
            result.issues.push_back(
                {IssueSeverity::Error, pack.root, {}, std::format("data pack '{}': folder not found", pack.name)});
            continue;
        }
        const fs::path dataFolder = pack.root / "data";
        const fs::path baseBlocks = dataFolder / std::string(kBaseNamespace) / "blocks";
        if (pack.isBase && !fs::is_directory(baseBlocks, error)) {
            result.issues.push_back({IssueSeverity::Error, baseBlocks, {},
                                     std::format("the base data pack '{}' must have this folder", pack.name)});
        }

        std::map<std::string, fs::path, std::less<>> idsInPack;
        for (const fs::path& namespaceFolder : listFolder(dataFolder, true, {}, result.issues)) {
            const std::string dataNamespace = core::pathToUtf8(namespaceFolder.filename());
            if (!isValidName(dataNamespace)) {
                result.issues.push_back({IssueSeverity::Error, namespaceFolder, {},
                                         "folder name is not a valid namespace ([a-z0-9_] only)"});
                continue;
            }
            const fs::path blocksFolder = namespaceFolder / "blocks";
            const std::vector<fs::path> files = listFolder(blocksFolder, false, ".json", result.issues);
            const bool isBaseBlocks = pack.isBase && dataNamespace == kBaseNamespace;
            if (isBaseBlocks && files.empty() && fs::is_directory(blocksFolder, error)) {
                result.issues.push_back({IssueSeverity::Warning, blocksFolder, {}, "no block files"});
            }

            for (const fs::path& file : files) {
                ++result.filesRead;
                FileIssues issues(result.issues, file);
                const std::optional<std::string> text = readFile(file, issues);
                const std::optional<Json> root = text ? parseJson(*text, issues) : std::nullopt;
                std::optional<BlockDefinition> block =
                    root ? readBlock(*root, dataNamespace, packs, pack, issues) : std::nullopt;
                if (!block || issues.hasErrors()) {
                    continue;
                }

                // A copy: the definition is moved into the map below.
                const std::string id = block->id.str();
                if (const auto [first, added] = idsInPack.emplace(id, file); !added) {
                    issues.error("/id", std::format("{} is already defined in {} (same data pack '{}')", id,
                                                    core::pathToUtf8(first->second), pack.name));
                    continue;
                }
                if (const auto existing = finalBlocks.find(id); existing != finalBlocks.end()) {
                    issues.info("/id", std::format("replaces {} from {} (data pack '{}')", id,
                                                   core::pathToUtf8(existing->second.definition.sourceFile),
                                                   packs[existing->second.packIndex].name));
                    existing->second = Loaded{std::move(*block), packIndex};
                } else {
                    finalBlocks.emplace(id, Loaded{std::move(*block), packIndex});
                }
            }
        }
    }

    if (countIssues(result.issues, IssueSeverity::Error) > 0) {
        return result;
    }
    // Total state count and numbering apply to the final set only: replaced definitions are gone by now.
    std::vector<BlockDefinition> blocks;
    blocks.reserve(finalBlocks.size());
    for (auto& [id, loaded] : finalBlocks) {
        blocks.push_back(std::move(loaded.definition));
    }
    result.registry = BlockRegistry::create(std::move(blocks), result.issues);
    return result;
}

void logBlockLoadResult(const BlockLoadResult& result, std::size_t packCount)
{
    logIssues(result.issues);
    const std::size_t errors = countIssues(result.issues, IssueSeverity::Error);
    const std::size_t warnings = countIssues(result.issues, IssueSeverity::Warning);
    if (result.registry) {
        core::logInfo("data", "Loaded {} blocks ({} block states) from {} files in {} data pack(s), {} warning(s)",
                      result.registry->blockCount(), result.registry->stateCount(), result.filesRead, packCount,
                      warnings);
    } else {
        core::logError("data", "Block data has {} error(s) and {} warning(s) ({} files read); fix the lines above",
                       errors, warnings, result.filesRead);
    }
}

} // namespace aurora::data
