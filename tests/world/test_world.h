#pragma once

#include "core/job_system.h"
#include "world/flat_generator.h"
#include "world/world.h"

#include "../data/data_test_support.h"
#include "world_test_support.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace aurora::test {

// A real World with the standard flat layout (stone -64..59, dirt 60..62, grass 63), driven by the test thread:
// it is created here, so this thread owns it.
struct TestWorld {
    std::shared_ptr<const data::BlockRegistry> registry = makeTestRegistry();
    std::shared_ptr<const data::FlatPreset> preset = makeStandardFlatPreset(*registry);
    core::JobSystem jobs{1};
    world::World world{registry, jobs, world::makeFlatGenerator(preset)};

    data::BlockStateId state(const char* text) const { return stateOf(*registry, text); }

    // Loads the chunks within `radius` of `center` (waiting for them) and returns the updates since the last take.
    std::vector<world::ChunkUpdate> load(world::ChunkPos center, std::int32_t radius)
    {
        world.ensureLoaded(center, radius);
        jobs.waitIdle();
        world.update();
        return world.takeChunkUpdates();
    }

    // publishChanges(), then the updates since the last take.
    std::vector<world::ChunkUpdate> publish()
    {
        world.publishChanges();
        return world.takeChunkUpdates();
    }
};

} // namespace aurora::test
