#pragma once

#include "data/block_registry.h"
#include "data/load_issue.h"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace aurora::data {

// A folder laid out as data/<namespace>/... and assets/<namespace>/...: the base game or one mod.
struct DataPack {
    std::string name;
    std::filesystem::path root;
    // The base game: its data/aurora/blocks folder must exist.
    bool isBase = false;
};

struct BlockLoadResult {
    // Set only when loading found no errors. A failed load never hands out a partial registry.
    std::shared_ptr<const BlockRegistry> registry;
    std::vector<LoadIssue> issues;
    std::size_t filesRead = 0;
    // With the registry: the file every texture of the final blocks resolved to, by texture id
    // ("aurora:block/stone"). Image loading reads exactly these files and never searches the packs again.
    std::map<std::string, std::filesystem::path, std::less<>> textureFiles;
};

// Reads every data/<ns>/blocks/*.json of the packs, in the given order (base game first, then mods by name).
// - Every file is checked and every problem collected; nothing stops at the first error.
// - The same block id twice within one pack is an error. A later pack defining an id replaces the earlier
//   definition (an Info issue).
// - Per-block state limits are checked per file; the total state limit and state numbering apply to the final
//   set after replacements.
// - Texture references are "[ns:]path"; without ns they use the <ns> folder the block file is in. The file
//   must exist as assets/<ns>/textures/<path>.png in some pack, later packs searched first.
BlockLoadResult loadBlocks(std::span<const DataPack> packs);

// Logs every issue and a summary line.
void logBlockLoadResult(const BlockLoadResult& result, std::size_t packCount);

} // namespace aurora::data
