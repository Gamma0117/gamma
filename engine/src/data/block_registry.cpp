#include "data/block_registry.h"

#include "core/log.h"
#include "core/utf8.h"

#include <algorithm>
#include <format>
#include <utility>

namespace aurora::data {

namespace {

constexpr std::string_view kAirId = "aurora:air";
constexpr std::string_view kUnknownId = "aurora:unknown";
// How many of the largest blocks a "too many states" error lists.
constexpr std::size_t kLargestBlocksReported = 5;

BlockDefinition makeBuiltIn(std::string_view id)
{
    BlockDefinition block;
    block.id = *ResourceId::parse(id);
    block.sourcePack = "built-in";
    return block;
}

BlockDefinition makeAir()
{
    BlockDefinition air = makeBuiltIn(kAirId);
    air.render = RenderLayer::Invisible;
    air.solid = false;
    return air;
}

// Placeholder for a block a save refers to but the loaded data lacks. Unbreakable, so the spot is kept until the
// data that defines the block is back.
BlockDefinition makeUnknown()
{
    BlockDefinition unknown = makeBuiltIn(kUnknownId);
    unknown.unbreakable = true;
    return unknown;
}

std::string describeSource(const BlockDefinition& block)
{
    return block.sourceFile.empty() ? block.sourcePack : core::pathToUtf8(block.sourceFile);
}

} // namespace

std::optional<std::uint64_t> countStates(const std::vector<BlockProperty>& properties, std::uint64_t limit)
{
    std::uint64_t count = 1;
    for (const BlockProperty& property : properties) {
        const std::uint64_t values = property.values.size();
        if (values == 0 || count > limit / values) {
            return std::nullopt;
        }
        count *= values;
    }
    if (count > limit) {
        return std::nullopt;
    }
    return count;
}

bool BlockRegistry::isReservedId(std::string_view id)
{
    return id == kAirId || id == kUnknownId;
}

std::shared_ptr<const BlockRegistry> BlockRegistry::create(std::vector<BlockDefinition> blocks,
                                                           std::vector<LoadIssue>& issues)
{
    std::ranges::sort(blocks, {}, &BlockDefinition::id);
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        if (isReservedId(blocks[i].id.str()) || (i > 0 && blocks[i].id == blocks[i - 1].id)) {
            issues.push_back({IssueSeverity::Error, blocks[i].sourceFile, "/id",
                              std::format("{} is reserved or defined twice", blocks[i].id.str())});
            return nullptr;
        }
    }

    std::shared_ptr<BlockRegistry> registry(new BlockRegistry());
    std::vector<BlockDefinition>& all = registry->m_blocks;
    all.reserve(blocks.size() + 2);
    all.push_back(makeAir());
    all.push_back(makeUnknown());
    std::ranges::move(blocks, std::back_inserter(all));

    // Count in 64 bits over the final set only; convert to 16-bit ids after the range check.
    std::uint64_t total = 0;
    for (BlockDefinition& block : all) {
        const std::optional<std::uint64_t> count = countStates(block.properties, kMaxDataBlockStates);
        if (!count) {
            issues.push_back({IssueSeverity::Error, block.sourceFile, "/states",
                              std::format("{} has more than {} state combinations", block.id.str(),
                                          kMaxDataBlockStates)});
            return nullptr;
        }
        block.stateCount = static_cast<std::uint32_t>(*count);
        total += *count;
    }
    if (total > core::kMaxBlockStates) {
        std::vector<const BlockDefinition*> largest;
        for (const BlockDefinition& block : all) {
            largest.push_back(&block);
        }
        std::ranges::stable_sort(largest, std::ranges::greater{}, &BlockDefinition::stateCount);
        std::string list;
        for (std::size_t i = 0; i < std::min(kLargestBlocksReported, largest.size()); ++i) {
            list += std::format("{}{} ({} states, {})", i == 0 ? "" : "; ", largest[i]->id.str(),
                                largest[i]->stateCount, describeSource(*largest[i]));
        }
        issues.push_back({IssueSeverity::Error, {}, {},
                          std::format("Too many block states: {} (at most {} including the 2 built-in states). "
                                      "Largest: {}",
                                      total, core::kMaxBlockStates, list)});
        return nullptr;
    }

    std::uint32_t nextState = 0;
    registry->m_stateToBlock.reserve(total);
    for (std::size_t index = 0; index < all.size(); ++index) {
        BlockDefinition& block = all[index];
        block.firstState = static_cast<BlockStateId>(nextState);

        block.propertyStrides.assign(block.properties.size(), 1);
        for (std::size_t i = block.properties.size(); i-- > 1;) {
            block.propertyStrides[i - 1] =
                block.propertyStrides[i] * static_cast<std::uint32_t>(block.properties[i].values.size());
        }
        std::uint32_t defaultOffset = 0;
        for (std::size_t i = 0; i < block.properties.size(); ++i) {
            defaultOffset += block.defaultValues[i] * block.propertyStrides[i];
        }
        block.defaultState = static_cast<BlockStateId>(nextState + defaultOffset);

        registry->m_stateToBlock.insert(registry->m_stateToBlock.end(), block.stateCount,
                                        static_cast<std::uint16_t>(index));
        registry->m_blockById.emplace(block.id.str(), index);
        nextState += block.stateCount;
    }
    registry->m_stateCount = nextState;
    return registry;
}

const BlockDefinition* BlockRegistry::findBlock(std::string_view id) const
{
    const auto it = m_blockById.find(id);
    return it == m_blockById.end() ? nullptr : &m_blocks[it->second];
}

const BlockDefinition& BlockRegistry::blockOf(BlockStateId state) const
{
    return m_blocks[m_stateToBlock[state]];
}

std::uint32_t BlockRegistry::propertyValue(BlockStateId state, std::size_t propertyIndex) const
{
    const BlockDefinition& block = blockOf(state);
    const std::uint32_t local = static_cast<std::uint32_t>(state - block.firstState);
    const auto valueCount = static_cast<std::uint32_t>(block.properties[propertyIndex].values.size());
    return (local / block.propertyStrides[propertyIndex]) % valueCount;
}

std::string BlockRegistry::stateToString(BlockStateId state) const
{
    const BlockDefinition& block = blockOf(state);
    std::string text = block.id.str();
    if (block.properties.empty()) {
        return text;
    }
    text += '[';
    for (std::size_t i = 0; i < block.properties.size(); ++i) {
        const BlockProperty& property = block.properties[i];
        if (i > 0) {
            text += ',';
        }
        text += property.name;
        text += '=';
        text += property.values[propertyValue(state, i)];
    }
    text += ']';
    return text;
}

BlockRegistry::ParseResult BlockRegistry::parseState(std::string_view text) const
{
    const auto fail = [](std::string error) { return ParseResult{std::nullopt, std::move(error)}; };

    const std::size_t open = text.find('[');
    const std::string_view idText = text.substr(0, open);
    const BlockDefinition* block = findBlock(idText);
    if (block == nullptr) {
        if (!ResourceId::parse(idText)) {
            return fail(std::format("'{}' is not a block id (namespace:name)", idText));
        }
        return fail(std::format("unknown block '{}'", idText));
    }

    std::vector<std::uint32_t> values = block->defaultValues;
    if (open != std::string_view::npos) {
        if (text.back() != ']') {
            return fail("the property list must end with ']'");
        }
        const std::string_view list = text.substr(open + 1, text.size() - open - 2);
        if (list.empty()) {
            return fail("empty property list '[]'");
        }

        std::vector<bool> given(block->properties.size(), false);
        std::size_t start = 0;
        for (;;) {
            const std::size_t comma = list.find(',', start);
            const std::string_view item = list.substr(start, comma - start);
            const std::size_t equals = item.find('=');
            const std::string_view name = item.substr(0, equals);
            const std::string_view value =
                equals == std::string_view::npos ? std::string_view{} : item.substr(equals + 1);
            if (equals == std::string_view::npos || !isValidName(name) || !isValidName(value)) {
                return fail(std::format("malformed property '{}' (expected name=value)", item));
            }

            const auto property = std::ranges::find(block->properties, name, &BlockProperty::name);
            if (property == block->properties.end()) {
                return fail(std::format("{} has no property '{}'", block->id.str(), name));
            }
            const auto propertyIndex = static_cast<std::size_t>(property - block->properties.begin());
            if (given[propertyIndex]) {
                return fail(std::format("property '{}' is given twice", name));
            }
            given[propertyIndex] = true;

            const auto match = std::ranges::find(property->values, value);
            if (match == property->values.end()) {
                return fail(std::format("'{}' is not a value of {}[{}]", value, block->id.str(), name));
            }
            values[propertyIndex] = static_cast<std::uint32_t>(match - property->values.begin());

            if (comma == std::string_view::npos) {
                break;
            }
            start = comma + 1;
        }
    }

    std::uint32_t offset = 0;
    for (std::size_t i = 0; i < values.size(); ++i) {
        offset += values[i] * block->propertyStrides[i];
    }
    return {static_cast<BlockStateId>(block->firstState + offset), {}};
}

BlockStateId BlockRegistry::resolveStateOrUnknown(std::string_view text) const
{
    ParseResult result = parseState(text);
    if (result.state) {
        return *result.state;
    }
    core::logWarn("data", "Block state '{}' cannot be resolved ({}); using {}", text, result.error, kUnknownId);
    return kUnknownState;
}

} // namespace aurora::data
