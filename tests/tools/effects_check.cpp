// GL check of the block effects and of block changes reaching the screen (P0-7 plan 14.3): one case of a fixed
// scene, drawn with the real window, chunk renderer and block effects renderer (Xvfb + Mesa in the tests), saved as
// PNGs and judged by their pixels. It is separate from the game and does not test the game's own wiring.
//
//   aurora_effects_check --game-dir <folder> --output-dir <folder> --case <name> [--negative]
//
// The scene: the flat world of the game folder, loaded around chunk (0, 0) with radius 2 (5 x 5 chunks) by a real
// World on this thread; a ClientWorld with render distance 1, so the 3 x 3 middle chunks are drawn, each with all
// eight neighbours; the camera at the eyes of a player standing at (0.5, 64, 0.5), looking north (yaw 0, pitch 0);
// the target cell (0, 65, -3) in chunk (0, -1). Blocks are placed with World::setBlock and reach the client as
// Changed updates from World::publishChanges, as the server sends them. Only the target's south face is in view.
//
// Cases, and their negative controls (--negative), which must fail the same pixel check:
//   outline     selection lines on the target: dark pixels along the 4 edges of its south face and nothing else
//               changed (lines left out)
//   crack_mid   crack stage 5 on the target: darker pixels on part of the face, the rest exactly the block, nothing
//               outside the face (crack left out)
//   crack_late  crack stage 9: as crack_mid, with more crack pixels than stage 5 (crack left out)
//   particles   fragments of the broken target, 0.1 s old: each one in the stone's colour where it is projected,
//               nothing changed far from the cell (fragments left out)
//   removed     the target broken by a Changed (its section becomes all air): the sky shows where it was
//               (Changed not applied)
//   placed      stone placed in the empty target cell by a Changed: stone shows where the sky was (Changed not
//               applied)
//   occluded    lines and crack stage 9 on a target behind a wall: the frame does not change (depth test off)
//
// Before a frame is judged, the scene is checked: meshing settled with nothing left to upload, the chunks around
// the target held and eligible, the GPU table holding the current mesh of the sections looked at (none for an
// all-air section), and the first frame showing what the case starts from (the target drawn, or the sky, or the
// wall). When effects are drawn, the GL state must be the same before and after (unusual values are set first).
//
// Exit codes:
//   0  check passed
//   2  check failed: the pixels do not show what the case needs
//   3  the scene could not be set up or is not what the case needs (data, GL, meshing, eligibility, GPU table)
//   4  the effects renderer did not give back the GL state
//   1  wrong arguments

#include "client/block_particles.h"
#include "client/camera.h"
#include "client/client_world.h"
#include "client/mesh_scheduler.h"
#include "core/job_system.h"
#include "core/log.h"
#include "core/utf8.h"
#include "data/block_loader.h"
#include "data/block_textures.h"
#include "data/flat_preset.h"
#include "data/player_interaction.h"
#include "data/player_movement.h"
#include "platform/window.h"
#include "render/block_effects_renderer.h"
#include "render/chunk_mesher.h"
#include "render/chunk_renderer.h"
#include "render/gl_state.h"
#include "render/mesh_resources.h"
#include "render/renderer.h"
#include "render/screenshot.h"
#include "world/chunk_snapshot.h"
#include "world/flat_generator.h"
#include "world/world.h"

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

using namespace aurora;
using Clock = std::chrono::steady_clock;

constexpr int kPassed = 0;
constexpr int kUsage = 1;
constexpr int kCheckFailed = 2;
constexpr int kSceneFailed = 3;
constexpr int kGlStateChanged = 4;

constexpr world::ChunkPos kCenter{0, 0};
constexpr std::int32_t kLoadRadius = 2;
constexpr std::int32_t kRenderDistance = 1;
constexpr double kFeetX = 0.5;
constexpr double kFeetY = 64.0;
constexpr double kFeetZ = 0.5;
constexpr world::BlockPos kTarget{0, 65, -3};
constexpr world::BlockPos kWall{0, 65, -2}; // Between the camera and the target (occluded).
constexpr render::ClearColor kSkyColor{0.10f, 0.14f, 0.22f};
constexpr float kFarPlane = 200.0f;
constexpr std::chrono::seconds kSettleTimeout{20};
constexpr std::chrono::milliseconds kParticleAge{100};
constexpr std::size_t kMaxMeshJobs = 32; // Meshing jobs in flight at most (the game uses workers x 16).
constexpr std::uint32_t kMidStage = 5;
constexpr std::uint32_t kLateStage = 9;
// The selection lines lie this far outside the cell (BlockEffectsRenderer).
constexpr double kOutlineMargin = 0.002;

// ---------------------------------------------------------------------------------------------------------------
// Images

struct Pixel {
    int r = 0;
    int g = 0;
    int b = 0;
};

Pixel pixelAt(const data::RgbaImage& image, int x, int y)
{
    const std::size_t at = (static_cast<std::size_t>(y) * image.width + static_cast<std::size_t>(x)) * 4;
    return {image.pixels[at], image.pixels[at + 1], image.pixels[at + 2]};
}

int difference(const Pixel& a, const Pixel& b)
{
    return std::max({std::abs(a.r - b.r), std::abs(a.g - b.g), std::abs(a.b - b.b)});
}

// The clear colour, 8-bit.
bool isSky(const Pixel& p)
{
    return std::abs(p.r - 26) <= 2 && std::abs(p.g - 36) <= 2 && std::abs(p.b - 56) <= 2;
}

// Whole pixels, inclusive. Empty when x0 > x1 or y0 > y1.
struct Rect {
    int x0 = 0;
    int y0 = 0;
    int x1 = -1;
    int y1 = -1;

    Rect grown(int by) const { return {x0 - by, y0 - by, x1 + by, y1 + by}; }
    bool contains(int x, int y) const { return x >= x0 && x <= x1 && y >= y0 && y <= y1; }
    int area() const { return x1 < x0 || y1 < y0 ? 0 : (x1 - x0 + 1) * (y1 - y0 + 1); }
};

template <typename Visit>
void forEachPixel(const data::RgbaImage& image, const Rect& rect, Visit visit)
{
    const int x0 = std::max(rect.x0, 0);
    const int y0 = std::max(rect.y0, 0);
    const int x1 = std::min(rect.x1, static_cast<int>(image.width) - 1);
    const int y1 = std::min(rect.y1, static_cast<int>(image.height) - 1);
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            visit(x, y);
        }
    }
}

// Mean colour of a rectangle, and how much of it is sky.
struct RegionColour {
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
    double skyShare = 0.0;
    int pixels = 0;
};

RegionColour regionColour(const data::RgbaImage& image, const Rect& rect)
{
    RegionColour result;
    int sky = 0;
    forEachPixel(image, rect, [&](int x, int y) {
        const Pixel p = pixelAt(image, x, y);
        result.r += p.r;
        result.g += p.g;
        result.b += p.b;
        sky += isSky(p) ? 1 : 0;
        ++result.pixels;
    });
    if (result.pixels > 0) {
        const double n = result.pixels;
        result.r /= n;
        result.g /= n;
        result.b /= n;
        result.skyShare = sky / n;
    }
    return result;
}

std::string describe(const RegionColour& colour)
{
    return std::format("mean rgb ({:.0f}, {:.0f}, {:.0f}), {:.1f}% sky, {} px", colour.r, colour.g, colour.b,
                       colour.skyShare * 100.0, colour.pixels);
}

// Grey like stone (red and blue within a few steps of each other), not the sky.
bool looksLikeStone(const RegionColour& colour)
{
    return colour.skyShare < 0.01 && std::abs(colour.r - colour.b) <= 12.0 && colour.r + colour.g + colour.b > 120.0;
}

// Brown like oak planks: clearly more red than blue.
bool looksLikePlanks(const RegionColour& colour)
{
    return colour.skyShare < 0.01 && colour.r - colour.b >= 25.0;
}

// ---------------------------------------------------------------------------------------------------------------
// Game data (as the game loads it)

struct GameData {
    std::shared_ptr<const data::BlockRegistry> blocks;
    std::shared_ptr<const data::FlatPreset> flatPreset;
    std::shared_ptr<const data::PlayerMovementTuning> movement;
    std::shared_ptr<const data::PlayerInteraction> interaction;
    std::vector<data::BlockTexture> textures;
    std::vector<data::RgbaImage> crackStages;
    std::vector<data::Rgb> particleColours;
    std::filesystem::path shaderFolder;
};

std::optional<GameData> loadGameData(const std::filesystem::path& gameDir)
{
    const std::vector<data::DataPack> packs{{"aurora", gameDir, true}};
    const data::BlockLoadResult blocks = data::loadBlocks(packs);
    data::logBlockLoadResult(blocks, packs.size());
    if (!blocks.registry) {
        return std::nullopt;
    }
    data::BlockTextureLoadResult textures = data::loadBlockTextures(*blocks.registry, blocks.textureFiles);
    data::logBlockTextureLoadResult(textures);
    const data::FlatPresetLoadResult preset = data::loadFlatPreset(packs, *blocks.registry);
    data::logFlatPresetLoadResult(preset);
    const data::PlayerMovementLoadResult movement = data::loadPlayerMovement(packs);
    data::logPlayerMovementLoadResult(movement);
    const data::PlayerInteractionLoadResult interaction = data::loadPlayerInteraction(packs, *blocks.registry);
    data::logPlayerInteractionLoadResult(interaction);
    data::CrackTextureLoadResult cracks = data::loadCrackTextures(packs);
    data::logCrackTextureLoadResult(cracks);
    if (!preset.preset || !movement.tuning || !interaction.interaction ||
        countIssues(textures.issues, data::IssueSeverity::Error) > 0 ||
        countIssues(cracks.issues, data::IssueSeverity::Error) > 0) {
        return std::nullopt;
    }
    std::vector<data::Rgb> particleColours = data::blockParticleColours(*blocks.registry, textures.textures);
    return GameData{.blocks = blocks.registry,
                    .flatPreset = preset.preset,
                    .movement = movement.tuning,
                    .interaction = interaction.interaction,
                    .textures = std::move(textures.textures),
                    .crackStages = std::move(cracks.stages),
                    .particleColours = std::move(particleColours),
                    .shaderFolder = gameDir / "assets" / std::string(data::kBaseNamespace) / "shaders"};
}

// ---------------------------------------------------------------------------------------------------------------
// The check

enum class Case { Outline, CrackMid, CrackLate, Particles, Removed, Placed, Occluded };

struct CaseName {
    std::string_view name;
    Case value;
};

constexpr CaseName kCases[] = {
    {"outline", Case::Outline},     {"crack_mid", Case::CrackMid}, {"crack_late", Case::CrackLate},
    {"particles", Case::Particles}, {"removed", Case::Removed},     {"placed", Case::Placed},
    {"occluded", Case::Occluded},
};

struct Options {
    std::filesystem::path gameDir;
    std::filesystem::path outputDir;
    Case which = Case::Outline;
    std::string_view name;
    bool negative = false;
};

// A finished run: the exit code and the line that says why.
struct Verdict {
    int code = kPassed;
    std::string message;
};

Verdict passed(std::string message)
{
    return {kPassed, std::move(message)};
}
Verdict failed(std::string message)
{
    return {kCheckFailed, std::move(message)};
}
Verdict sceneFailed(std::string message)
{
    return {kSceneFailed, std::move(message)};
}

class EffectsCheck {
public:
    EffectsCheck(const Options& options, const GameData& game, platform::Window& window)
        : m_options(options)
        , m_game(game)
        , m_window(window)
        , m_camera({kFeetX, kFeetY + game.movement->eyeHeight, kFeetZ}, 0.0, 0.0)
        , m_world(game.blocks, m_jobs, world::makeFlatGenerator(game.flatPreset))
        , m_layers(layersOf(game))
        , m_meshResources(m_layers ? render::buildMeshResources(*game.blocks, *m_layers) : nullptr)
        , m_scheduler(m_jobs, render::makeChunkMesher(m_meshResources), kMaxMeshJobs)
        , m_particles(game.interaction->tuning, game.particleColours)
    {
    }

    ~EffectsCheck()
    {
        m_jobs.shutdown();
        m_effects.shutdown();
        m_chunks.shutdown();
        if (m_sentinelTexture != 0) {
            glDeleteTextures(1, &m_sentinelTexture);
        }
    }

    EffectsCheck(const EffectsCheck&) = delete;
    EffectsCheck& operator=(const EffectsCheck&) = delete;

    Verdict run()
    {
        if (Verdict setup = setUp(); setup.code != kPassed) {
            return setup;
        }
        switch (m_options.which) {
        case Case::Outline:
            return runOutline();
        case Case::CrackMid:
        case Case::CrackLate:
            return runCrack();
        case Case::Particles:
            return runParticles();
        case Case::Removed:
        case Case::Placed:
            return runBlockChange();
        case Case::Occluded:
            return runOccluded();
        }
        return sceneFailed("unknown case");
    }

private:
    static std::optional<render::TextureLayers> layersOf(const GameData& game)
    {
        std::vector<std::string> ids;
        for (const data::BlockTexture& texture : game.textures) {
            ids.push_back(texture.id.str());
        }
        return render::assignTextureLayers(ids);
    }

    data::BlockStateId state(std::string_view text) const
    {
        return m_game.blocks->parseState(text).state.value_or(data::kAirState);
    }

    // GL objects, the world and the case's first blocks.
    Verdict setUp()
    {
        if (!m_layers) {
            return sceneFailed("the block textures do not fit the texture layers");
        }
        if (!m_renderer.init()) {
            return sceneFailed("cannot set up the renderer");
        }
        std::string error;
        if (!m_chunks.init(m_game.shaderFolder, m_game.textures, *m_layers, error)) {
            return sceneFailed("cannot set up chunk rendering: " + error);
        }
        if (!m_effects.init(m_game.shaderFolder, m_game.crackStages, error)) {
            return sceneFailed("cannot set up block effects: " + error);
        }
        m_stone = state("aurora:stone");
        m_planks = state("aurora:oak_planks");
        if (m_stone == data::kAirState || m_planks == data::kAirState) {
            return sceneFailed("the game has no aurora:stone or aurora:oak_planks");
        }

        // The server's part: load 5 x 5 chunks and hand them over.
        m_world.ensureLoaded(kCenter, kLoadRadius);
        const Clock::time_point deadline = Clock::now() + kSettleTimeout;
        while (m_world.stats().loadedChunks < 25 && Clock::now() < deadline) {
            m_jobs.waitIdle();
            m_world.update();
        }
        if (m_world.stats().loadedChunks != 25) {
            return sceneFailed(std::format("{} of 25 chunks loaded", m_world.stats().loadedChunks));
        }
        m_client.setCenter(kCenter);
        receive(m_world.takeChunkUpdates());

        // The case's first blocks, as a Changed.
        bool placed = true;
        switch (m_options.which) {
        case Case::Outline:
        case Case::CrackMid:
        case Case::CrackLate:
        case Case::Particles:
        case Case::Removed:
            placed = place(kTarget, m_stone);
            break;
        case Case::Placed:
            break;
        case Case::Occluded:
            placed = place(kTarget, m_stone) && place(kWall, m_planks);
            break;
        }
        if (!placed) {
            return sceneFailed("the world refused a block of the scene");
        }
        publishAndReceive(true);

        std::printf("camera: eyes (%.2f, %.2f, %.2f), yaw %.1f, pitch %.1f, %dx%d; target (%d, %d, %d)\n",
                    m_camera.position().x, m_camera.position().y, m_camera.position().z, m_camera.yaw(),
                    m_camera.pitch(), m_window.framebufferWidth(), m_window.framebufferHeight(), kTarget.x,
                    kTarget.y, kTarget.z);
        return settleAndCheckScene("first blocks");
    }

    bool place(const world::BlockPos& pos, data::BlockStateId blockState)
    {
        const bool done = m_world.setBlock(pos, blockState);
        std::printf("set (%d, %d, %d) to %s: %s\n", pos.x, pos.y, pos.z,
                    m_game.blocks->stateToString(blockState).c_str(), done ? "ok" : "refused");
        return done;
    }

    void receive(const std::vector<world::ChunkUpdate>& updates)
    {
        for (const world::ChunkUpdate& update : updates) {
            m_client.apply(update);
        }
    }

    // World::publishChanges, then the Changed updates go to the client unless `apply` is false (negative controls).
    void publishAndReceive(bool apply)
    {
        m_world.publishChanges();
        const std::vector<world::ChunkUpdate> updates = m_world.takeChunkUpdates();
        for (const world::ChunkUpdate& update : updates) {
            std::printf("Changed (%d, %d) revision %llu, sections 0x%06x (%d copied)%s\n", update.pos.x,
                        update.pos.z, static_cast<unsigned long long>(update.snapshot->revision()),
                        update.changedSections, std::popcount(update.changedSections),
                        apply ? "" : " NOT APPLIED (negative control)");
        }
        if (apply) {
            receive(updates);
        }
    }

    // Client frames without drawing until every needed mesh is made and uploaded, then the scene checks.
    Verdict settleAndCheckScene(std::string_view stage)
    {
        const Clock::time_point start = Clock::now();
        const std::size_t gpuBytesBefore = m_chunks.stats().gpuBytes;
        const std::uint64_t uploadedBefore = m_chunks.stats().uploadedBytes;
        const std::uint64_t staleBefore = m_scheduler.stats().stale + m_chunks.stats().staleUploads;
        std::size_t taken = 0;
        int frames = 0;
        bool settled = false;
        while (Clock::now() - start < kSettleTimeout) {
            m_window.pollEvents();
            m_scheduler.update(m_client, kCenter, m_camera.sectionY());
            if (const std::vector<client::MeshKey> failures = m_scheduler.takeFailures(); !failures.empty()) {
                return sceneFailed(std::format("{} section meshes failed", failures.size()));
            }
            m_chunks.queueUploads(m_scheduler.takeReady());
            m_chunks.update(m_client, std::numeric_limits<std::size_t>::max());
            taken += m_chunks.takeTaken().size();
            ++frames;
            if (m_scheduler.isSettled() && m_chunks.pendingUploads() == 0) {
                settled = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        const client::MeshSchedulerStats after = m_scheduler.stats();
        const render::ChunkRenderStats gpu = m_chunks.stats();
        std::printf("%s: %s after %d frames, %.1f ms; %zu results taken by the GPU table, %llu bytes uploaded, "
                    "%llu stale results dropped, gpu bytes %zu -> %zu, %zu sections meshed, %zu empty (jobs at "
                    "most %zu)\n",
                    std::string(stage).c_str(), settled ? "settled" : "NOT settled", frames,
                    std::chrono::duration<double, std::milli>(Clock::now() - start).count(), taken,
                    static_cast<unsigned long long>(gpu.uploadedBytes - uploadedBefore),
                    static_cast<unsigned long long>(after.stale + gpu.staleUploads - staleBefore), gpuBytesBefore,
                    gpu.gpuBytes, after.meshed, after.empty, kMaxMeshJobs);
        if (!settled) {
            return sceneFailed(std::format("meshing did not settle within {} s ({} waiting, {} in flight, {} "
                                           "uploads)",
                                           kSettleTimeout.count(), after.waiting, after.inFlight,
                                           m_chunks.pendingUploads()));
        }
        return checkScene();
    }

    // The chunks around the target held and eligible; the GPU table holds the current mesh of the sections the
    // frames look at, or nothing for an all-air section.
    Verdict checkScene() const
    {
        if (!m_client.isAreaComplete()) {
            return sceneFailed("not every chunk within the render distance is eligible");
        }
        const world::ChunkPos targetChunk = world::chunkPosOf(kTarget);
        for (std::int32_t dz = -1; dz <= 1; ++dz) {
            for (std::int32_t dx = -1; dx <= 1; ++dx) {
                if (!m_client.snapshot({targetChunk.x + dx, targetChunk.z + dz})) {
                    return sceneFailed(std::format("the target's neighbour chunk ({}, {}) is not held",
                                                   targetChunk.x + dx, targetChunk.z + dz));
                }
            }
        }
        if (!m_client.isEligible(targetChunk) || !m_client.isEligible(kCenter)) {
            return sceneFailed("the target's chunk or the camera's chunk is not eligible");
        }
        const client::SectionKey sections[] = {
            {targetChunk, world::sectionIndex(kTarget.y)},
            {targetChunk, world::sectionIndex(kTarget.y) - 1}, // The ground below.
            {kCenter, world::sectionIndex(kTarget.y) - 1},
        };
        for (const client::SectionKey& section : sections) {
            const std::optional<client::MeshKey> current = m_client.currentKey(section);
            const std::optional<client::MeshKey> onGpu = m_chunks.meshKeyOf(section);
            const bool allAir = m_client.snapshot(section.pos)->section(section.section) == nullptr;
            const auto text = [](const std::optional<client::MeshKey>& key) {
                return key ? std::format("generation {} stamp {}", key->generation, key->stamp) : std::string("none");
            };
            std::printf("section (%d, %d) #%d: %s, current key %s, GPU key %s\n", section.pos.x, section.pos.z,
                        section.section, allAir ? "all air" : "has blocks", text(current).c_str(),
                        text(onGpu).c_str());
            if (!current) {
                return sceneFailed("a section looked at has no current key");
            }
            if (allAir ? onGpu.has_value() : onGpu != current) {
                return sceneFailed("the GPU table does not hold the current mesh of a section looked at");
            }
        }
        return passed("scene ready");
    }

    // ---- Drawing

    float aspect() const
    {
        return static_cast<float>(m_window.framebufferWidth()) / static_cast<float>(m_window.framebufferHeight());
    }

    // Pixel position (x right, y down from the top row) of a world point.
    glm::dvec2 toPixel(const glm::dvec3& point) const
    {
        const glm::mat4 viewProjection = m_camera.projection(aspect(), kFarPlane) * m_camera.viewRotation();
        const glm::vec4 clip = viewProjection * glm::vec4(client::relativeTo(point, m_camera.position()), 1.0f);
        const double x = clip.x / clip.w;
        const double y = clip.y / clip.w;
        return {(x * 0.5 + 0.5) * m_window.framebufferWidth(), (0.5 - y * 0.5) * m_window.framebufferHeight()};
    }

    // The pixels covered by the target's south face (z = target + 1), grown by `margin` blocks around the cell.
    Rect southFace(double margin = 0.0) const
    {
        const double z = kTarget.z + 1.0 + margin;
        const glm::dvec2 a = toPixel({kTarget.x - margin, kTarget.y + 1.0 + margin, z});
        const glm::dvec2 b = toPixel({kTarget.x + 1.0 + margin, kTarget.y - margin, z});
        return {static_cast<int>(std::floor(a.x)), static_cast<int>(std::floor(a.y)),
                static_cast<int>(std::ceil(b.x)) - 1, static_cast<int>(std::ceil(b.y)) - 1};
    }

    // The face shrunk to whole pixels inside it, above the horizon (where the sky is behind the target).
    Rect upperFace() const
    {
        Rect face = southFace().grown(-4);
        face.y1 = std::min(face.y1, m_window.framebufferHeight() / 2 - 8);
        return face;
    }

    // Sets GL state the effects renderer changes to values it never uses, so a missing restore shows.
    void setSentinelState()
    {
        if (m_sentinelTexture == 0) {
            glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &m_sentinelTexture);
            glTextureStorage3D(m_sentinelTexture, 1, GL_RGBA8, 1, 1, 1);
        }
        glDepthFunc(GL_GREATER);
        glDepthMask(GL_TRUE);
        glEnable(GL_BLEND);
        glBlendFuncSeparate(GL_ONE, GL_ZERO, GL_ZERO, GL_ONE);
        glBlendEquationSeparate(GL_FUNC_REVERSE_SUBTRACT, GL_MAX);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_FRONT);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(2.0f, 3.0f);
        glBindVertexArray(0);
        glUseProgram(0);
        glActiveTexture(GL_TEXTURE0 + render::BlockEffectsRenderer::kCrackTextureUnit);
        glBindTexture(GL_TEXTURE_2D_ARRAY, m_sentinelTexture);
        glActiveTexture(GL_TEXTURE3);
    }

    // One frame: sky, chunks, then the effects if given. Saved to <output>/<case>[_negative]_<label>.png and read
    // back from that file. nullopt after printing why.
    std::optional<data::RgbaImage> capture(std::string_view label, const render::BlockEffectsFrame* effects,
                                           Verdict& problem)
    {
        m_window.pollEvents();
        m_renderer.clear(kSkyColor);
        m_chunks.draw(m_camera, aspect(), kFarPlane);
        if (effects) {
            const render::GlStateSnapshot original =
                render::GlStateSnapshot::capture(render::BlockEffectsRenderer::kCrackTextureUnit);
            setSentinelState();
            const render::GlStateSnapshot before =
                render::GlStateSnapshot::capture(render::BlockEffectsRenderer::kCrackTextureUnit);
            m_effects.draw(m_camera, aspect(), kFarPlane, *effects);
            const render::GlStateSnapshot after =
                render::GlStateSnapshot::capture(render::BlockEffectsRenderer::kCrackTextureUnit);
            original.restore();
            std::printf("%s: effects drew %zu calls (%zu fragments); GL state %s\n", std::string(label).c_str(),
                        m_effects.stats().drawCalls, m_effects.stats().particles,
                        before == after ? "given back" : "CHANGED");
            if (!(before == after)) {
                problem = {kGlStateChanged, "the effects renderer did not give back the GL state"};
                return std::nullopt;
            }
        }
        const std::filesystem::path file =
            m_options.outputDir / std::format("{}{}_{}.png", m_options.name, m_options.negative ? "_negative" : "",
                                              label);
        std::error_code ignored;
        std::filesystem::remove(file, ignored);
        std::string error;
        if (!render::saveFramebufferPng(file, m_window.framebufferWidth(), m_window.framebufferHeight(), error)) {
            problem = sceneFailed("cannot save the frame: " + error);
            return std::nullopt;
        }
        m_window.swapBuffers();
        const std::optional<std::vector<std::byte>> bytes = data::readFileBytes(file, error);
        const data::DecodeResult decoded = bytes ? data::decodePng(*bytes) : data::DecodeResult{};
        if (!decoded.image) {
            problem = sceneFailed("cannot read the saved frame back: " + (bytes ? decoded.error : error));
            return std::nullopt;
        }
        std::printf("%s: saved %s\n", std::string(label).c_str(), core::pathToUtf8(file).c_str());
        return decoded.image;
    }

    // The base frame (chunks only) must show the target block on its south face.
    Verdict requireTargetDrawn(const data::RgbaImage& base) const
    {
        const RegionColour face = regionColour(base, southFace().grown(-4));
        std::printf("base: target face %s\n", describe(face).c_str());
        if (!looksLikeStone(face)) {
            return sceneFailed("the base frame does not show the stone target");
        }
        return passed("target drawn");
    }

    // ---- Cases

    Verdict runOutline()
    {
        Verdict problem;
        const std::optional<data::RgbaImage> base = capture("base", nullptr, problem);
        if (!base) {
            return problem;
        }
        if (Verdict drawn = requireTargetDrawn(*base); drawn.code != kPassed) {
            return drawn;
        }
        render::BlockEffectsFrame frame;
        if (!m_options.negative) {
            frame.outline = kTarget;
        }
        std::printf("effects: outline %s\n", m_options.negative ? "left out (negative control)" : "on the target");
        const std::optional<data::RgbaImage> test = capture("effects", &frame, problem);
        if (!test) {
            return problem;
        }

        // The south face's edges, at the lines' margin.
        const double m = kOutlineMargin;
        const double z = kTarget.z + 1.0 + m;
        const glm::dvec2 topLeft = toPixel({kTarget.x - m, kTarget.y + 1.0 + m, z});
        const glm::dvec2 bottomRight = toPixel({kTarget.x + 1.0 + m, kTarget.y - m, z});
        struct Edge {
            const char* name;
            bool horizontal;
            double at;     // y of a horizontal edge, x of a vertical one.
            double from;   // Along the edge.
            double to;
        };
        const Edge edges[] = {
            {"top", true, topLeft.y, topLeft.x, bottomRight.x},
            {"bottom", true, bottomRight.y, topLeft.x, bottomRight.x},
            {"left", false, topLeft.x, topLeft.y, bottomRight.y},
            {"right", false, bottomRight.x, topLeft.y, bottomRight.y},
        };
        const auto isLine = [&](int x, int y) {
            if (x < 0 || y < 0 || x >= static_cast<int>(test->width) || y >= static_cast<int>(test->height)) {
                return false;
            }
            const Pixel p = pixelAt(*test, x, y);
            return difference(p, pixelAt(*base, x, y)) > 8 && std::max({p.r, p.g, p.b}) <= 24;
        };
        bool allEdges = true;
        std::string summary;
        for (const Edge& edge : edges) {
            int samples = 0;
            int hits = 0;
            for (int along = static_cast<int>(edge.from) + 6; along <= static_cast<int>(edge.to) - 6; ++along) {
                ++samples;
                const int across = static_cast<int>(std::floor(edge.at));
                bool hit = false;
                for (int d = -2; d <= 2 && !hit; ++d) {
                    hit = edge.horizontal ? isLine(along, across + d) : isLine(across + d, along);
                }
                hits += hit ? 1 : 0;
            }
            const double share = samples > 0 ? static_cast<double>(hits) / samples : 0.0;
            summary += std::format("{} {}/{} ", edge.name, hits, samples);
            allEdges = allEdges && samples >= 50 && share >= 0.9;
        }
        // Anything changed away from the four edges (hidden edges must stay hidden).
        int stray = 0;
        int changed = 0;
        forEachPixel(*test, {0, 0, static_cast<int>(test->width) - 1, static_cast<int>(test->height) - 1},
                     [&](int x, int y) {
                         if (difference(pixelAt(*test, x, y), pixelAt(*base, x, y)) <= 8) {
                             return;
                         }
                         ++changed;
                         const bool nearHorizontal = x >= topLeft.x - 3 && x <= bottomRight.x + 3 &&
                                                     (std::abs(y + 0.5 - topLeft.y) <= 3.0 ||
                                                      std::abs(y + 0.5 - bottomRight.y) <= 3.0);
                         const bool nearVertical = y >= topLeft.y - 3 && y <= bottomRight.y + 3 &&
                                                   (std::abs(x + 0.5 - topLeft.x) <= 3.0 ||
                                                    std::abs(x + 0.5 - bottomRight.x) <= 3.0);
                         stray += nearHorizontal || nearVertical ? 0 : 1;
                     });
        summary += std::format("line samples hit; {} pixels changed, {} away from the edges (face at x {:.1f}..{:.1f}, "
                               "y {:.1f}..{:.1f})",
                               changed, stray, topLeft.x, bottomRight.x, topLeft.y, bottomRight.y);
        if (allEdges && stray == 0) {
            return passed("outline: " + summary);
        }
        return failed("outline: " + summary);
    }

    struct CrackPixels {
        int inside = 0;    // Pixels well inside the face.
        int changed = 0;   // Of those, changed.
        int darker = 0;    // Of the changed, clearly darker.
        int unchanged = 0; // Exactly the block (within 2 steps).
        int outside = 0;   // Changed pixels outside the face.
    };

    CrackPixels crackPixels(const data::RgbaImage& base, const data::RgbaImage& test) const
    {
        CrackPixels result;
        const Rect face = southFace();
        const Rect inner = face.grown(-3);
        const Rect outer = face.grown(3);
        forEachPixel(test, {0, 0, static_cast<int>(test.width) - 1, static_cast<int>(test.height) - 1},
                     [&](int x, int y) {
                         const Pixel a = pixelAt(base, x, y);
                         const Pixel b = pixelAt(test, x, y);
                         const int diff = difference(a, b);
                         if (inner.contains(x, y)) {
                             ++result.inside;
                             if (diff > 6) {
                                 ++result.changed;
                                 result.darker += (b.r + b.g + b.b) <= (a.r + a.g + a.b) - 30 ? 1 : 0;
                             } else if (diff <= 2) {
                                 ++result.unchanged;
                             }
                         } else if (!outer.contains(x, y) && diff > 6) {
                             ++result.outside;
                         }
                     });
        return result;
    }

    static std::string describeCrack(const CrackPixels& pixels)
    {
        return std::format("{} of {} face pixels changed ({:.1f}%), {} of them darker, {} unchanged, {} changed "
                           "outside the face",
                           pixels.changed, pixels.inside, 100.0 * pixels.changed / std::max(1, pixels.inside),
                           pixels.darker, pixels.unchanged, pixels.outside);
    }

    static bool looksLikeCrack(const CrackPixels& pixels)
    {
        return pixels.inside > 0 && pixels.changed >= pixels.inside * 3 / 100 &&
               pixels.darker * 100 >= pixels.changed * 95 && pixels.unchanged * 2 >= pixels.inside &&
               pixels.outside == 0;
    }

    Verdict runCrack()
    {
        Verdict problem;
        const std::optional<data::RgbaImage> base = capture("base", nullptr, problem);
        if (!base) {
            return problem;
        }
        if (Verdict drawn = requireTargetDrawn(*base); drawn.code != kPassed) {
            return drawn;
        }
        const bool late = m_options.which == Case::CrackLate;
        // crack_late compares with stage 5, always drawn.
        std::optional<CrackPixels> mid;
        if (late) {
            render::BlockEffectsFrame reference;
            reference.crack = render::BlockEffectsFrame::Crack{kTarget, kMidStage};
            const std::optional<data::RgbaImage> midFrame = capture("stage5_reference", &reference, problem);
            if (!midFrame) {
                return problem;
            }
            mid = crackPixels(*base, *midFrame);
            std::printf("stage 5 reference: %s\n", describeCrack(*mid).c_str());
            if (!looksLikeCrack(*mid)) {
                return sceneFailed("the stage 5 reference does not show a crack");
            }
        }
        const std::uint32_t stage = late ? kLateStage : kMidStage;
        render::BlockEffectsFrame frame;
        if (!m_options.negative) {
            frame.crack = render::BlockEffectsFrame::Crack{kTarget, stage};
        }
        std::printf("effects: crack stage %u %s\n", stage,
                    m_options.negative ? "left out (negative control)" : "on the target");
        const std::optional<data::RgbaImage> test = capture("effects", &frame, problem);
        if (!test) {
            return problem;
        }
        const CrackPixels pixels = crackPixels(*base, *test);
        std::string summary = std::format("crack stage {}: {}", stage, describeCrack(pixels));
        bool ok = looksLikeCrack(pixels);
        if (mid) {
            summary += std::format("; stage 5 had {} changed", mid->changed);
            ok = ok && pixels.changed * 10 >= mid->changed * 13;
        }
        return ok ? passed(summary) : failed(summary);
    }

    Verdict runParticles()
    {
        // The target is broken first; the fragments fly where it was.
        if (!place(kTarget, data::kAirState)) {
            return sceneFailed("the world refused to break the target");
        }
        publishAndReceive(true);
        if (Verdict scene = settleAndCheckScene("target broken"); scene.code != kPassed) {
            return scene;
        }
        const world::ChunkPos targetChunk = world::chunkPosOf(kTarget);
        const Clock::time_point now = Clock::now();
        const entity::BlockBrokenEvent event{.position = kTarget,
                                             .previousState = m_stone,
                                             .generation = m_client.snapshot(targetChunk)->generation(),
                                             .serverTick = 1000,
                                             .occurredAt = now - kParticleAge};
        m_particles.add(event, m_client, now);
        const std::vector<client::BlockParticles::Instance> fragments = m_particles.instances(now);
        const data::Rgb colour = m_game.particleColours[m_stone];
        std::printf("fragments: %zu of the stone, colour (%d, %d, %d), %.3f s old\n", fragments.size(), colour[0],
                    colour[1], colour[2], std::chrono::duration<double>(kParticleAge).count());
        if (fragments.size() != m_game.interaction->tuning.particleCount) {
            return sceneFailed("the broken target made the wrong number of fragments");
        }

        Verdict problem;
        const std::optional<data::RgbaImage> base = capture("base", nullptr, problem);
        if (!base) {
            return problem;
        }
        const RegionColour empty = regionColour(*base, upperFace());
        std::printf("base: the broken target's place above the horizon %s\n", describe(empty).c_str());
        if (empty.skyShare < 0.99) {
            return sceneFailed("the broken target is still drawn");
        }
        render::BlockEffectsFrame frame;
        if (!m_options.negative) {
            frame.particles = fragments;
        }
        std::printf("effects: fragments %s\n", m_options.negative ? "left out (negative control)" : "drawn");
        const std::optional<data::RgbaImage> test = capture("effects", &frame, problem);
        if (!test) {
            return problem;
        }

        const Pixel expected{colour[0], colour[1], colour[2]};
        const int width = static_cast<int>(test->width);
        const int height = static_cast<int>(test->height);
        int inFrame = 0;
        int found = 0;
        for (const client::BlockParticles::Instance& fragment : fragments) {
            const glm::dvec2 at = toPixel(fragment.position);
            const int cx = static_cast<int>(std::floor(at.x));
            const int cy = static_cast<int>(std::floor(at.y));
            if (cx < 4 || cy < 4 || cx >= width - 4 || cy >= height - 4) {
                continue;
            }
            ++inFrame;
            bool hit = false;
            for (int dy = -1; dy <= 1 && !hit; ++dy) {
                for (int dx = -1; dx <= 1 && !hit; ++dx) {
                    const Pixel p = pixelAt(*test, cx + dx, cy + dy);
                    hit = difference(p, expected) <= 3 && difference(p, pixelAt(*base, cx + dx, cy + dy)) > 8;
                }
            }
            found += hit ? 1 : 0;
        }
        // Nothing changed far from the cell (one block around it).
        const Rect around = [&] {
            const glm::dvec2 a = toPixel({kTarget.x - 1.0, kTarget.y + 2.0, kTarget.z + 1.0});
            const glm::dvec2 b = toPixel({kTarget.x + 2.0, kTarget.y - 1.0, kTarget.z + 1.0});
            return Rect{static_cast<int>(a.x), static_cast<int>(a.y), static_cast<int>(b.x), static_cast<int>(b.y)};
        }();
        int stray = 0;
        forEachPixel(*test, {0, 0, width - 1, height - 1}, [&](int x, int y) {
            if (!around.contains(x, y) && difference(pixelAt(*test, x, y), pixelAt(*base, x, y)) > 8) {
                ++stray;
            }
        });
        const std::string summary = std::format("fragments: {} of {} in the frame found in their colour at their "
                                                "projected centre, {} pixels changed away from the cell",
                                                found, inFrame, stray);
        if (inFrame >= 8 && found == inFrame && stray == 0) {
            return passed(summary);
        }
        return failed(summary);
    }

    // removed / placed: a Changed for the target, applied (or not, for the negative control), meshed and uploaded.
    Verdict runBlockChange()
    {
        const bool removing = m_options.which == Case::Removed;
        Verdict problem;
        const std::optional<data::RgbaImage> base = capture("before", nullptr, problem);
        if (!base) {
            return problem;
        }
        const RegionColour baseColour = regionColour(*base, upperFace());
        std::printf("before: target face above the horizon %s\n", describe(baseColour).c_str());
        if (removing ? !looksLikeStone(baseColour) : baseColour.skyShare < 0.99) {
            return sceneFailed(removing ? "the stone target is not drawn before the change"
                                        : "the sky is not behind the empty target before the change");
        }

        if (!place(kTarget, removing ? data::kAirState : m_stone)) {
            return sceneFailed("the world refused the change");
        }
        publishAndReceive(!m_options.negative);
        if (Verdict scene = settleAndCheckScene(removing ? "target broken" : "target placed");
            scene.code != kPassed) {
            return scene;
        }
        const std::optional<data::RgbaImage> test = capture("after", nullptr, problem);
        if (!test) {
            return problem;
        }
        const RegionColour after = regionColour(*test, upperFace());
        const std::string summary = std::format("{}: target face above the horizon {}", m_options.name,
                                                describe(after));
        const bool ok = removing ? after.skyShare >= 0.99 : looksLikeStone(after);
        return ok ? passed(summary) : failed(summary);
    }

    Verdict runOccluded()
    {
        Verdict problem;
        const std::optional<data::RgbaImage> base = capture("base", nullptr, problem);
        if (!base) {
            return problem;
        }
        const RegionColour wall = regionColour(*base, southFace().grown(-4));
        std::printf("base: the target's place %s\n", describe(wall).c_str());
        if (!looksLikePlanks(wall)) {
            return sceneFailed("the wall in front of the target is not drawn");
        }
        render::BlockEffectsFrame frame;
        frame.outline = kTarget;
        frame.crack = render::BlockEffectsFrame::Crack{kTarget, kLateStage};
        frame.ignoreDepth = m_options.negative;
        std::printf("effects: outline and crack stage %u on the hidden target%s\n", kLateStage,
                    m_options.negative ? ", depth test off (negative control)" : "");
        const std::optional<data::RgbaImage> test = capture("effects", &frame, problem);
        if (!test) {
            return problem;
        }
        if (m_effects.stats().drawCalls != 2) {
            return sceneFailed("the effects renderer did not draw the lines and the crack");
        }
        int changed = 0;
        forEachPixel(*test, {0, 0, static_cast<int>(test->width) - 1, static_cast<int>(test->height) - 1},
                     [&](int x, int y) { changed += difference(pixelAt(*test, x, y), pixelAt(*base, x, y)) > 2; });
        const std::string summary = std::format("occluded: {} pixels changed by lines and crack behind the wall",
                                                changed);
        return changed == 0 ? passed(summary) : failed(summary);
    }

    const Options& m_options;
    const GameData& m_game;
    platform::Window& m_window;
    render::Renderer m_renderer;
    client::Camera m_camera;
    core::JobSystem m_jobs;
    world::World m_world;
    client::ClientWorld m_client{kRenderDistance};
    std::optional<render::TextureLayers> m_layers;
    std::shared_ptr<const render::MeshResources> m_meshResources;
    client::MeshScheduler m_scheduler;
    render::ChunkRenderer m_chunks;
    render::BlockEffectsRenderer m_effects;
    client::BlockParticles m_particles;
    data::BlockStateId m_stone = data::kAirState;
    data::BlockStateId m_planks = data::kAirState;
    std::uint32_t m_sentinelTexture = 0;
};

bool parseArgs(int argc, char** argv, Options& options)
{
    bool hasCase = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--game-dir" && i + 1 < argc) {
            options.gameDir = argv[++i];
        } else if (arg == "--output-dir" && i + 1 < argc) {
            options.outputDir = argv[++i];
        } else if (arg == "--case" && i + 1 < argc) {
            const std::string_view name = argv[++i];
            const auto found = std::ranges::find(kCases, name, &CaseName::name);
            if (found == std::end(kCases)) {
                return false;
            }
            options.which = found->value;
            options.name = found->name;
            hasCase = true;
        } else if (arg == "--negative") {
            options.negative = true;
        } else {
            return false;
        }
    }
    return hasCase && !options.gameDir.empty() && !options.outputDir.empty();
}

} // namespace

int main(int argc, char** argv)
{
    Options options;
    if (!parseArgs(argc, argv, options)) {
        std::fprintf(stderr, "usage: aurora_effects_check --game-dir <folder> --output-dir <folder> --case "
                             "outline|crack_mid|crack_late|particles|removed|placed|occluded [--negative]\n");
        return kUsage;
    }
    core::Log::init(core::LogConfig{});
    Verdict verdict;
    {
        const std::optional<GameData> game = loadGameData(options.gameDir);
        std::error_code error;
        std::filesystem::create_directories(options.outputDir, error);
        platform::Window window;
        if (!game) {
            verdict = sceneFailed("the game data does not load");
        } else if (!window.create(platform::WindowDesc{.title = "aurora_effects_check", .vsync = false})) {
            verdict = sceneFailed("no window");
        } else {
            std::printf("case %s%s\n", std::string(options.name).c_str(), options.negative ? " (negative)" : "");
            EffectsCheck check(options, *game, window);
            verdict = check.run();
        }
        window.destroy();
    }
    const char* prefix = verdict.code == kPassed        ? "check passed"
                         : verdict.code == kCheckFailed ? "check failed"
                                                        : "check not run";
    std::printf("%s: %s\n", prefix, verdict.message.c_str());
    std::fflush(stdout);
    core::Log::shutdown();
    return verdict.code;
}
