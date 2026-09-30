#pragma once

#include "core/constants.h"
#include "data/load_issue.h"
#include "data/resource_id.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace aurora::data {

// Runtime number of one block state (a block plus one value for each of its properties). Numbers are assigned at
// load time and change when data changes, so they are never persisted: saves store state strings
// (stateToString) and the network sends the server's table first.
using BlockStateId = std::uint16_t;
inline constexpr BlockStateId kAirState = 0;     // aurora:air, built in. A zero-filled section is all air.
inline constexpr BlockStateId kUnknownState = 1; // aurora:unknown, built in. Stands in for blocks missing from data.
// States available to data files: the 16-bit range minus the two built-in states.
inline constexpr std::uint32_t kMaxDataBlockStates = core::kMaxBlockStates - 2;
static_assert(core::kMaxBlockStates - 1 <= 0xffff, "BlockStateId must hold every state number");

enum class RenderLayer : std::uint8_t {
    Invisible,
    Opaque,
    Cutout,
    Translucent,
};

// Index into BlockDefinition::faceTextures.
enum class BlockFace : std::uint8_t {
    Down,
    Up,
    North,
    South,
    West,
    East,
};
inline constexpr std::size_t kBlockFaceCount = 6;

struct BlockProperty {
    std::string name;
    std::vector<std::string> values; // In data-file order.
};

struct BlockDefinition {
    ResourceId id;
    bool unbreakable = false;
    float hardness = 0.0f; // Unused when unbreakable.
    std::optional<float> explosionResistance;
    std::uint8_t light = 0;
    RenderLayer render = RenderLayer::Opaque;
    bool solid = true;
    std::array<ResourceId, kBlockFaceCount> faceTextures; // Empty ids: no texture (air, the unknown placeholder).
    ResourceId emissiveTexture;                           // Empty if none.
    std::vector<BlockProperty> properties;                // Sorted by name.
    std::vector<std::uint32_t> defaultValues;             // Value index per property.

    // Where the definition came from, for messages. Empty for built-ins.
    std::filesystem::path sourceFile;
    std::string sourcePack;

    // Filled in by BlockRegistry::create.
    BlockStateId firstState = 0;
    std::uint32_t stateCount = 1;
    BlockStateId defaultState = 0;
    std::vector<std::uint32_t> propertyStrides; // The last property (by name) varies fastest.
};

// Number of state combinations of `properties`, or nullopt if any property has no values or the count would
// exceed `limit`. Checks before every multiplication, so huge inputs fail fast without overflow.
std::optional<std::uint64_t> countStates(const std::vector<BlockProperty>& properties, std::uint64_t limit);

// Immutable table of every block and block state. Safe to read from any thread once created.
class BlockRegistry {
public:
    // Adds the built-in blocks, sorts `blocks` by id and numbers their states. `blocks` is the final set, after
    // data-pack overrides, already validated, without reserved or duplicate ids. Returns nullptr (and adds an
    // Error issue) if the states do not fit in 16 bits.
    static std::shared_ptr<const BlockRegistry> create(std::vector<BlockDefinition> blocks,
                                                       std::vector<LoadIssue>& issues);

    // aurora:air and aurora:unknown, which data files cannot define.
    static bool isReservedId(std::string_view id);

    std::size_t blockCount() const { return m_blocks.size(); }
    std::uint32_t stateCount() const { return m_stateCount; }
    // In state order: air, unknown, then data blocks sorted by id.
    const std::vector<BlockDefinition>& blocks() const { return m_blocks; }

    const BlockDefinition* findBlock(std::string_view id) const;
    // `state` must be below stateCount().
    const BlockDefinition& blockOf(BlockStateId state) const;
    std::uint32_t propertyValue(BlockStateId state, std::size_t propertyIndex) const;

    // "aurora:stone", or with every property in name order: "aurora:oak_log[axis=y]".
    std::string stateToString(BlockStateId state) const;

    struct ParseResult {
        std::optional<BlockStateId> state;
        std::string error; // Why parsing failed, when state is empty.
    };
    // Strict. Accepts "ns:block" (its default state) or "ns:block[name=value,...]" (listed properties set, the
    // others at their defaults). Rejects unknown blocks, properties and values, a property given twice, an empty
    // "[]", spaces and any other malformed text. Never substitutes the unknown block.
    ParseResult parseState(std::string_view text) const;

    // For reading saved data: parseState, or kUnknownState with a logged warning when that fails.
    BlockStateId resolveStateOrUnknown(std::string_view text) const;

private:
    BlockRegistry() = default;

    std::vector<BlockDefinition> m_blocks;
    std::vector<std::uint16_t> m_stateToBlock; // Index into m_blocks, per state.
    std::map<std::string, std::size_t, std::less<>> m_blockById;
    std::uint32_t m_stateCount = 0;
};

} // namespace aurora::data
