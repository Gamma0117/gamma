#pragma once

#include "world/coordinates.h"

#include <functional>
#include <memory>

namespace aurora::world {

class Chunk;

// Makes the chunk at a position. Runs on worker threads, several at once, so it may only read immutable data it
// owns (e.g. a shared_ptr to a preset); never the World or anything the server thread writes. Returns the new
// chunk; nullptr or an exception marks the position as failed.
using ChunkGenerator = std::function<std::unique_ptr<Chunk>(ChunkPos)>;

} // namespace aurora::world
