#pragma once

#include "entity/player_messages.h"
#include "server/server_player.h"
#include "server/server_stats.h"
#include "world/chunk_update.h"
#include "world/coordinates.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace aurora::core {
class JobSystem;
}

namespace aurora::data {
class BlockRegistry;
struct FlatPreset;
struct PlayerMovementTuning;
} // namespace aurora::data

namespace aurora::entity {
class CollisionShapes;
}

namespace aurora::world {
class World;
}

namespace aurora::server {

struct ServerConfig {
    // A world needs all three. Without them the server runs its tick loop with no world (loop tests).
    // The job system must outlive the server.
    core::JobSystem* jobs = nullptr;
    std::shared_ptr<const data::BlockRegistry> blocks;
    std::shared_ptr<const data::FlatPreset> flatPreset;
    // With a world and these settings, the server runs the local player (ServerPlayer). Without them, only the
    // world (world tests).
    std::shared_ptr<const data::PlayerMovementTuning> playerMovement{};
    // Collision boxes per block state; built from `blocks` when null (tests pass partial shapes).
    std::shared_ptr<const entity::CollisionShapes> collisionShapes{};
    // Chunks loaded around the load center, as a square radius: 9 is 19 x 19 = 361 chunks, one ring more than a
    // render distance of 8 so every drawn chunk has its neighbours. Entries stay one ring further (World keeps
    // radius + 1), so while the center moves more than that can be loaded.
    std::int32_t loadRadius = 9;
    SpawnColumn spawnColumn{};
};

// Test seams. Both run on the server thread only.
struct ServerTestHooks {
    using Clock = std::chrono::steady_clock;

    // Called at the start of every tick with its number (1 for the first). May block. An exception from it stops
    // the server like any other error in the loop.
    std::function<void(std::uint64_t tickNumber)> onTick;
    // Replaces Clock::now() for the whole loop: scheduling, sleeping, tick timing and TPS. Must be monotonic and
    // safe to call while the test thread moves it. The loop still wakes at least every tick interval of real time
    // to read it again.
    std::function<Clock::time_point()> clock;
};

// The authoritative game simulation, run inside the game process on its own thread at a fixed
// core::kTicksPerSecond. The render loop runs independently at a variable frame rate; the client reaches the
// server through packets (P0-11).
//
// The World (and the local player) are created at the start of the server thread and destroyed before it ends, so
// the server thread is their owner: only it reads or writes world data. Other threads see stats(), the chunk
// updates (immutable snapshots of loaded chunks and notices of unloaded ones, in the order they happened) and the
// player's state; the client sends the player's inputs. Until packets exist (P0-11) these in-memory mailboxes are
// the client's only way to the world.
//
// Each tick: the player's messages, the load center and World::update(), the spawn (once the spawn column's chunk
// and its eight neighbours are loaded: feet on the highest block of the columns under the player), one player
// step, then the player's state is published.
//
// Errors: an exception anywhere in the loop (world creation, a tick) is caught at the top of the thread, logged
// and put in ServerStats::error; the thread ends and stop() joins it as usual.
class IntegratedServer {
public:
    // A server with no world.
    IntegratedServer();
    explicit IntegratedServer(ServerConfig config, ServerTestHooks hooks = {});
    ~IntegratedServer();

    IntegratedServer(const IntegratedServer&) = delete;
    IntegratedServer& operator=(const IntegratedServer&) = delete;

    // Starts the server thread. Returns false if it is already running. Throws std::system_error if the thread
    // cannot be created (a fatal start-up error).
    bool start();
    // Asks the loop to stop and wakes it, without waiting. The running tick finishes; no further tick starts, not
    // even the rest of a catch-up batch. Thread-safe.
    void requestStop();
    // requestStop(), then joins the thread. Safe to call more than once.
    void stop();

    // Thread-safe snapshot.
    ServerStats stats() const;

    // The load center: the spawn column's chunk until the player has spawned (whatever is set here, so the spawn
    // area always loads); then this chunk if set, else the player's chunk. A server without a player uses this
    // chunk if set, else the spawn column's. Thread-safe; the latest call wins. The free-flying debug camera sets
    // it and clears it again.
    void setViewCenterOverride(std::optional<world::ChunkPos> center);
    // Every chunk update since the last call, oldest first. Thread-safe. Restarting the server clears them.
    std::vector<world::ChunkUpdate> takeChunkUpdates();

    // The local player's mailbox: one FIFO for both kinds of message, so a Neutralize always arrives after the
    // inputs sent before it. Thread-safe. Messages sent before the spawn are ignored (see ServerPlayer).
    void sendPlayerInput(const entity::PlayerInput& input);
    void neutralizePlayerInputs(std::uint32_t through);
    // The state after the newest tick since the last call, or nothing new. Thread-safe.
    std::optional<entity::PlayerState> takePlayerState();

private:
    void run();
    void runLoop();
    struct Simulation {
        world::World* world = nullptr;
        ServerPlayer* player = nullptr;
        const entity::CollisionShapes* shapes = nullptr;
    };

    void tick(std::uint64_t tickNumber, const Simulation& simulation);
    world::ChunkPos loadCenter(const ServerPlayer* player, std::optional<world::ChunkPos> override) const;
    void trySpawn(const world::World& world, ServerPlayer& player) const;
    ServerTestHooks::Clock::time_point now() const;

    const ServerConfig m_config;
    const ServerTestHooks m_hooks;
    std::thread m_thread;

    mutable std::mutex m_mutex; // Guards everything below.
    std::condition_variable m_wake;
    bool m_stopRequested = false;
    ServerStats m_stats;
    std::optional<world::ChunkPos> m_viewCenterOverride;
    std::vector<world::ChunkUpdate> m_chunkUpdates;
    std::vector<PlayerMessage> m_playerMessages;
    std::optional<entity::PlayerState> m_playerState;
};

} // namespace aurora::server
