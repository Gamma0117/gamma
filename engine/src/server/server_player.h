#pragma once

#include "data/player_movement.h"
#include "entity/player_messages.h"
#include "entity/player_movement.h"
#include "server/server_stats.h"

#include <glm/vec3.hpp>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>

namespace aurora::entity {
struct CollisionWorld;
}

namespace aurora::world {
class World;
}

namespace aurora::server {

// One message from the client, in the order it was sent.
struct PlayerMessage {
    enum class Kind : std::uint8_t {
        Input,      // One client tick of intent.
        Neutralize, // The client stopped taking input: inputs up to `through` become neutral.
    };
    Kind kind = Kind::Input;
    entity::PlayerInput input{}; // Input only.
    std::uint32_t through = 0; // Neutralize only.
};

// The local player as the server simulates it: the authority on where the player is. Server thread only; no
// threads or world of its own, so the rules are tested directly.
//
// Time: tick() is called once for every server tick that runs and steps the physics exactly once, whatever came
// in. Many inputs never move the clock forward; no input means a neutral intent (no movement, jump, sneak or
// sprint), never a repeat of the last one, while gravity and collision go on. A frozen tick (rule B) does not move.
//
// Inputs, by sequence number:
// - An input at or below the highest sequence received so far is dropped (late or duplicate); the rest wait in
//   order. Before the spawn every message is dropped and the numbers stay at 0.
// - At the start of a tick, inputs beyond kMaxPendingInputs are dropped from the front.
// - Choosing a tick's intent: none waiting -> neutral, and the player starts filling. While filling, fewer than
//   kFillTarget waiting and fewer than kFillWaitTicks ticks waited -> neutral. Otherwise the front input is used
//   and filling stops. So a single input is used by the third tick after it arrived at the latest, and a steady
//   stream keeps one input in reserve against arrival jitter.
// - Neutralize(through) makes every waiting input up to `through` neutral (keeping its look), and so are inputs
//   received later with such a sequence. The range only grows: neutralizedThrough = max(old, through).
// - Intents are sanitised (entity::sanitizeIntent) on arrival; a non-finite angle makes the intent neutral and
//   uses the last valid angles.
//
// lastInput() ends the settled run: the front waiting sequence - 1, or the highest received when none waits.
// Every input at or below it is applied, dropped or will never be accepted; it never goes down.
class ServerPlayer {
public:
    static constexpr std::size_t kMaxPendingInputs = 4;
    static constexpr std::size_t kFillTarget = 2;
    static constexpr std::uint32_t kFillWaitTicks = 2;

    explicit ServerPlayer(std::shared_ptr<const data::PlayerMovementTuning> tuning);

    bool spawned() const { return m_spawned; }
    // Puts the feet at `feet`, standing still. Only the first call counts.
    void spawn(const glm::dvec3& feet);

    void receive(const PlayerMessage& message);
    // One server tick. Does nothing before the spawn.
    void tick(const entity::CollisionWorld& world);

    std::uint32_t lastInput() const;
    // The highest Neutralize range received: inputs up to it are (or will arrive) neutral. Never goes down.
    std::uint32_t neutralizedThrough() const { return m_neutralizedThrough; }
    // What the last tick used: the input's sequence (none for a neutral tick while starving or filling) and the
    // intent as applied (after sanitising and neutralising).
    std::optional<std::uint32_t> lastTickInput() const { return m_lastTickInput; }
    const entity::MovementIntent& lastTickIntent() const { return m_lastTickIntent; }
    const entity::PlayerMotion& motion() const { return m_motion; }
    entity::PlayerState state(std::uint64_t serverTick) const;
    ServerPlayerStats stats() const;
    const data::PlayerMovementTuning& tuning() const { return *m_tuning; }

private:
    std::shared_ptr<const data::PlayerMovementTuning> m_tuning;
    bool m_spawned = false;
    entity::PlayerMotion m_motion;
    bool m_frozen = false;

    std::deque<entity::PlayerInput> m_pending;
    std::uint32_t m_highestReceived = 0;
    std::uint32_t m_neutralizedThrough = 0;
    bool m_filling = true;
    std::uint32_t m_waited = 0;
    entity::MovementIntent m_lastIntent; // Its look stays for neutral ticks.
    std::optional<std::uint32_t> m_lastTickInput;
    entity::MovementIntent m_lastTickIntent;
    float m_lastValidYaw = 0.0f;
    float m_lastValidPitch = 0.0f;
    bool m_warnedInvalidAngles = false;

    std::uint64_t m_starvedTicks = 0;
    std::uint64_t m_primingTicks = 0;
    std::uint64_t m_droppedInputs = 0;
    std::uint64_t m_staleInputs = 0;
    std::uint64_t m_preSpawnMessages = 0;
};

// The block column the local player spawns in.
struct SpawnColumn {
    std::int32_t x = 0;
    std::int32_t z = 0;
};

// Where the player spawns in `column`, once the column's chunk and its eight neighbours are loaded (nothing until
// then): centred in the column, feet on the highest block of every column the player's box covers (it may be
// wider than one), or at the bottom of the world if they are all empty.
std::optional<glm::dvec3> findSpawn(const world::World& world, SpawnColumn column,
                                    const data::PlayerMovementTuning& tuning);

} // namespace aurora::server
