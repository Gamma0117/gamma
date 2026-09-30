#pragma once

#include "data/block_registry.h"
#include "data/flat_preset.h"

#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace aurora::test {

// A state from the registry by its state string; fails the test if it does not parse.
inline data::BlockStateId stateOf(const data::BlockRegistry& registry, std::string_view text)
{
    const data::BlockRegistry::ParseResult parsed = registry.parseState(text);
    if (!parsed.state) {
        throw std::runtime_error("test state does not parse: " + parsed.error);
    }
    return *parsed.state;
}

// The shipped layout: stone y -64..59, dirt 60..62, grass 63.
inline std::shared_ptr<const data::FlatPreset> makeStandardFlatPreset(const data::BlockRegistry& registry)
{
    auto preset = std::make_shared<data::FlatPreset>();
    preset->layers = {
        {stateOf(registry, "aurora:stone"), 124},
        {stateOf(registry, "aurora:dirt"), 3},
        {stateOf(registry, "aurora:grass_block"), 1},
    };
    return preset;
}

} // namespace aurora::test
