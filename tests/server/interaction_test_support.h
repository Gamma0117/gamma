#pragma once

#include "core/job_system.h"
#include "data/block_registry.h"
#include "data/player_interaction.h"
#include "entity/collision_shapes.h"
#include "server/block_interaction.h"
#include "server/server_player.h"
#include "server/world_collision_view.h"
#include "world/flat_generator.h"
#include "world/world.h"

#include "../data/data_test_support.h"
#include "../entity/entity_test_support.h"
#include "../world/world_test_support.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace aurora::test {

// The shipped blocks' hardness (stone 1.5, dirt 0.5, grass 0.6, cobblestone, planks and log 2.0, the log with an
// axis), plus aurora:bedrock (unbreakable), aurora:light_air (drawn invisible, not solid: rays pass it) and
// aurora:soft (hardness 0).
inline std::shared_ptr<const data::BlockRegistry> makeInteractionRegistry()
{
    const auto block = [](std::string_view id, float hardness) {
        data::BlockDefinition definition;
        definition.id = *data::ResourceId::parse(id);
        definition.hardness = hardness;
        return definition;
    };
    std::vector<data::BlockDefinition> blocks{block("aurora:stone", 1.5f),       block("aurora:dirt", 0.5f),
                                              block("aurora:grass_block", 0.6f), block("aurora:cobblestone", 2.0f),
                                              block("aurora:oak_planks", 2.0f),  block("aurora:soft", 0.0f)};
    data::BlockDefinition log = block("aurora:oak_log", 2.0f);
    log.properties = {data::BlockProperty{"axis", {"x", "y", "z"}}};
    log.defaultValues = {1};
    blocks.push_back(std::move(log));
    data::BlockDefinition bedrock = block("aurora:bedrock", 0.0f);
    bedrock.unbreakable = true;
    blocks.push_back(std::move(bedrock));
    data::BlockDefinition lightAir = block("aurora:light_air", 0.0f);
    lightAir.render = data::RenderLayer::Invisible;
    lightAir.solid = false;
    blocks.push_back(std::move(lightAir));
    std::vector<data::LoadIssue> issues;
    auto registry = data::BlockRegistry::create(std::move(blocks), issues);
    if (!registry) {
        throw std::runtime_error("test registry failed");
    }
    return registry;
}

// The shipped interaction.json (palette stone, dirt, grass, cobblestone, oak_log, oak_planks), loaded with the real
// loader against `registry`.
inline std::shared_ptr<const data::PlayerInteraction> loadShippedInteraction(const data::BlockRegistry& registry)
{
    TempGame game("interaction_rig");
    std::filesystem::create_directories(game.root() / "base" / "data" / "aurora" / "player");
    std::filesystem::copy_file(std::filesystem::path(AURORA_SOURCE_DIR) / "game/data/aurora/player/interaction.json",
                               game.root() / "base/data/aurora/player/interaction.json");
    const std::vector<data::DataPack> packs{game.pack("base", true)};
    data::PlayerInteractionLoadResult result = data::loadPlayerInteraction(packs, registry);
    if (!result.interaction) {
        throw std::runtime_error("the shipped interaction.json does not load: " + describeIssues(result.issues));
    }
    return result.interaction;
}

// A real World (standard flat layers: stone -64..59, dirt 60..62, grass 63) with a ServerPlayer and its
// BlockInteraction, all driven by the test thread in the order of IntegratedServer::tick.
//
// Lockstep: start() fills the player's input queue and drains it again, so afterwards step(intent) (send one input,
// then tick) applies that very input in that tick. tick() alone is a starving tick.
struct InteractionRig {
    using Clock = std::chrono::steady_clock;

    std::shared_ptr<const data::BlockRegistry> registry = makeInteractionRegistry();
    std::shared_ptr<const data::FlatPreset> preset = makeStandardFlatPreset(*registry);
    core::JobSystem jobs{1};
    world::World world{registry, jobs, world::makeFlatGenerator(preset)};
    std::shared_ptr<const entity::CollisionShapes> shapes =
        std::make_shared<const entity::CollisionShapes>(entity::CollisionShapes::fromRegistry(*registry));
    std::shared_ptr<const data::PlayerInteraction> interaction = loadShippedInteraction(*registry);
    std::shared_ptr<const data::PlayerMovementTuning> tuning =
        std::make_shared<const data::PlayerMovementTuning>(standardTuning());
    server::ServerPlayer player{tuning};
    server::BlockInteraction blocks{interaction, registry, shapes};
    std::uint32_t nextSequence = 1;
    std::uint64_t tickNumber = 0;
    Clock::time_point now = Clock::time_point{} + std::chrono::hours(1);
    std::vector<entity::BlockBrokenEvent> broken;
    std::vector<world::ChunkUpdate> updates;

    explicit InteractionRig(glm::dvec3 feet = {0.5, 64.0, 0.5}, std::int32_t loadRadius = 1)
    {
        load({0, 0}, loadRadius);
        player.spawn(feet);
    }

    data::BlockStateId state(std::string_view text) const { return stateOf(*registry, text); }

    void load(world::ChunkPos center, std::int32_t radius)
    {
        world.ensureLoaded(center, radius);
        jobs.waitIdle();
        world.update();
    }

    std::uint32_t send(const entity::MovementIntent& intent)
    {
        const std::uint32_t sequence = nextSequence++;
        player.receive({.kind = server::PlayerMessage::Kind::Input, .input = {sequence, intent}});
        return sequence;
    }
    void neutralize(std::uint32_t through)
    {
        player.receive({.kind = server::PlayerMessage::Kind::Neutralize, .through = through});
    }

    // One server tick as IntegratedServer runs it: movement, block actions, published changes. 50 ms pass.
    void tick()
    {
        now += std::chrono::milliseconds(50);
        ++tickNumber;
        const server::WorldCollisionView view(world);
        player.tick({view, *shapes});
        blocks.tick(world, player, tickNumber, now);
        world.publishChanges();
        for (world::ChunkUpdate& update : world.takeChunkUpdates()) {
            updates.push_back(std::move(update));
        }
        for (entity::BlockBrokenEvent& event : blocks.takeBrokenEvents()) {
            broken.push_back(event);
        }
    }

    // Two inputs of `intent`, applied in two ticks: afterwards the queue is empty and no longer filling (also after
    // starving ticks, which start filling again).
    void start(const entity::MovementIntent& intent = {})
    {
        send(intent);
        send(intent);
        tick();
        tick();
    }

    // Sends `intent` and runs the tick that applies it.
    void step(const entity::MovementIntent& intent)
    {
        const std::uint32_t sequence = send(intent);
        tick();
        if (player.lastTickInput() != sequence) {
            throw std::runtime_error("the rig lost lockstep");
        }
    }

    entity::PlayerState state() const
    {
        entity::PlayerState result = player.state(tickNumber);
        blocks.describe(result);
        return result;
    }

    data::BlockStateId blockAt(world::BlockPos pos) const { return world.getBlock(pos).value_or(data::kUnknownState); }
};

// Looks straight down (the cell under the feet) or north (yaw 0) at eye height.
inline constexpr float kDown = -90.0f;

inline entity::MovementIntent attackAt(float yaw, float pitch)
{
    return {.yaw = yaw, .pitch = pitch, .attack = true};
}

inline entity::MovementIntent useAt(float yaw, float pitch, std::uint8_t slot, bool attack = false)
{
    return {.yaw = yaw, .pitch = pitch, .attack = attack, .use = true, .slot = slot};
}

} // namespace aurora::test
