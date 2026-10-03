#pragma once

#include "core/timing_history.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace aurora::server {

// The local player on the server after the last tick (see ServerPlayer).
struct ServerPlayerStats {
    bool spawned = false;
    bool frozen = false;
    std::uint32_t lastInput = 0;
    std::size_t pendingInputs = 0;
    std::uint64_t starvedTicks = 0; // Ticks with no input waiting: neutral intent.
    std::uint64_t primingTicks = 0; // Ticks waiting for a second input after starving.
    std::uint64_t droppedInputs = 0; // Over kMaxPendingInputs; never applied.
    std::uint64_t staleInputs = 0;   // Not above the highest sequence received: late or duplicate.
    std::uint64_t preSpawnMessages = 0; // Inputs and Neutralize before the spawn (the client sends none then).
};

// The local player's block actions so far (see BlockInteraction).
struct BlockInteractionStats {
    std::uint64_t blocksBroken = 0;
    std::uint64_t placeAttempts = 0;
    std::uint64_t blocksPlaced = 0;
};

// Snapshot of the server loop for debug display. Plain data, copied out under the server's lock.
struct ServerStats {
    bool running = false;
    std::uint64_t tickCount = 0;
    std::uint64_t skippedTicks = 0;
    // Most ticks the scheduler handed out at once: 1 while keeping up, more while catching up. A stop request
    // still ends such a batch after the running tick.
    std::uint32_t maxScheduledBatch = 0;
    // Measured over the last full second; 0 until one second has passed.
    double ticksPerSecond = 0.0;
    // Work time per tick (excluding the sleep between ticks) over the recent ticks.
    core::TimingSummary tickTime;

    // The chunk table after the last tick. All zero for a server without a world.
    bool hasWorld = false;
    std::size_t loadedChunks = 0;
    std::size_t pendingChunks = 0;
    std::size_t failedChunks = 0;

    // A world with player settings runs the local player.
    bool hasPlayer = false;
    ServerPlayerStats player;
    // With interaction settings as well, the player breaks and places blocks.
    bool hasInteraction = false;
    BlockInteractionStats interaction;

    // Why the server thread stopped on its own (an exception in the loop). Empty otherwise.
    std::string error;
};

} // namespace aurora::server
