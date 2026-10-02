#include "client/camera.h"
#include "client/client_collision_view.h"
#include "client/client_world.h"
#include "client/cursor_controller.h"
#include "client/local_player.h"
#include "client/mesh_scheduler.h"
#include "client/movement_sampler.h"
#include "core/constants.h"
#include "core/job_system.h"
#include "core/log.h"
#include "core/profiler.h"
#include "core/thread.h"
#include "core/tick_scheduler.h"
#include "core/timing_history.h"
#include "core/utf8.h"
#include "data/block_loader.h"
#include "data/block_textures.h"
#include "data/flat_preset.h"
#include "data/game_directory.h"
#include "data/player_movement.h"
#include "entity/collision_shapes.h"
#include "platform/window.h"
#include "render/chunk_mesher.h"
#include "render/chunk_renderer.h"
#include "render/mesh_resources.h"
#include "render/mesh_vertex.h"
#include "render/renderer.h"
#include "render/screenshot.h"
#include "server/integrated_server.h"
#include "ui/debug_overlay.h"
#include "ui/imgui_layer.h"

#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

// Frame statistics and the F3 graph cover the last 240 frames (4 s at 60 FPS).
constexpr std::size_t kFrameHistory = 240;

// Chunks drawn around the camera (square radius: 17 x 17). The server loads one ring more, so every drawn chunk
// has all its neighbours.
constexpr std::int32_t kRenderDistance = 8;
constexpr std::int32_t kServerLoadRadius = kRenderDistance + 1;
// Meshing jobs in flight per worker. The scheduler hands out the nearest sections each frame, so this bounds
// how stale the queue's order can get: about this many section meshes of work per worker (about 25 ms in a Debug
// build), while still letting a frame start enough jobs that the frame rate does not limit meshing.
constexpr std::size_t kMeshJobsPerWorker = 16;
// Mesh data uploaded to the GPU per frame at most (at least one mesh always goes).
constexpr std::size_t kUploadBytesPerFrame = 2 * 1024 * 1024;
// --screenshot gives up when the world around the camera is not drawn by then.
constexpr std::chrono::seconds kScreenshotTimeout{30};

// Where the camera waits until the player has spawned: above the spawn column, looking north and down. Then it is
// at the player's eyes with the same look; --screenshot captures that view. Pitch -25 keeps the far ground out of
// the top tenth of the picture from eye height (the screenshot check wants sky there).
constexpr double kStartX = 0.5;
constexpr double kStartY = 80.0;
constexpr double kStartZ = 0.5;
constexpr double kStartYaw = 0.0;
constexpr double kStartPitch = -25.0;

#ifdef NDEBUG
constexpr const char* kBuildType = "Release";
#else
constexpr const char* kBuildType = "Debug";
#endif

struct LaunchOptions {
    // 0 = run until the window is closed. Used by the headless smoke test.
    long long maxFrames = 0;
    // Start with the F3 overlay open.
    bool debugOverlay = false;
    // Folder holding data/ and assets/. Without it the game looks in ./game, then in the source tree it was built
    // from.
    std::optional<std::filesystem::path> gameDir;
    // Waits until the world around the start position is drawn, saves that frame (without the UI) and exits.
    std::optional<std::filesystem::path> screenshot;
    // Test only: everything but the chunks is drawn, to prove the screenshot check fails without terrain.
    bool drawWorld = true;
};

bool parseArgs(int argc, char** argv, LaunchOptions& options)
{
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--frames" && i + 1 < argc) {
            const std::string_view value = argv[++i];
            const auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), options.maxFrames);
            if (ec != std::errc{} || ptr != value.data() + value.size() || options.maxFrames < 0) {
                aurora::core::logError("app", "Invalid --frames value: {}", value);
                return false;
            }
        } else if (arg == "--debug-overlay") {
            options.debugOverlay = true;
        } else if (arg == "--game-dir" && i + 1 < argc) {
            options.gameDir = std::filesystem::path(argv[++i]);
        } else if (arg == "--screenshot" && i + 1 < argc) {
            options.screenshot = std::filesystem::path(argv[++i]);
        } else if (arg == "--no-world-draw") {
            options.drawWorld = false;
        } else {
            aurora::core::logError("app", "Unknown argument: {}", arg);
            aurora::core::logError("app", "Usage: aurora [--frames N] [--debug-overlay] [--game-dir <folder>] "
                                          "[--screenshot <file.png>]");
            return false;
        }
    }
    return true;
}

float toMilliseconds(Clock::duration duration)
{
    return std::chrono::duration<float, std::milli>(duration).count();
}

struct GameData {
    std::shared_ptr<const aurora::data::BlockRegistry> blocks;
    std::shared_ptr<const aurora::data::FlatPreset> flatPreset;
    std::shared_ptr<const aurora::data::PlayerMovementTuning> movement;
    std::vector<aurora::data::BlockTexture> textures;
    std::filesystem::path shaderFolder;
};

// Finds the game folder and loads the block data, the block textures, the flat world preset and the player
// movement settings. Returns nullopt after logging every problem; the caller exits before any window is created.
std::optional<GameData> loadGameData(const LaunchOptions& options)
{
    using namespace aurora;

    std::vector<std::filesystem::path> candidates;
    std::error_code error;
    const std::filesystem::path workingDirectory = std::filesystem::current_path(error);
    if (!error) {
        candidates.push_back(workingDirectory / "game");
    }
    candidates.emplace_back(AURORA_SOURCE_GAME_DIR);

    const data::GameDirectory gameDirectory = data::resolveGameDirectory(options.gameDir, candidates);
    if (gameDirectory.path.empty()) {
        core::logError("app", "{}", gameDirectory.error);
        return std::nullopt;
    }
    core::logInfo("app", "Game folder: {}{}", core::pathToUtf8(gameDirectory.path),
                  options.gameDir ? " (from --game-dir)" : "");

    // Mods (game/mods/<name>, loaded by name after the base game) come later.
    const std::vector<data::DataPack> packs{{"aurora", gameDirectory.path, true}};
    const data::BlockLoadResult blocks = data::loadBlocks(packs);
    data::logBlockLoadResult(blocks, packs.size());
    if (!blocks.registry) {
        return std::nullopt;
    }

    // Exactly the files the block loader resolved; images are decoded and checked before any window exists.
    data::BlockTextureLoadResult textures = data::loadBlockTextures(*blocks.registry, blocks.textureFiles);
    data::logBlockTextureLoadResult(textures);

    // The preset names blocks, so it is read against the finished registry.
    const data::FlatPresetLoadResult preset = data::loadFlatPreset(packs, *blocks.registry);
    data::logFlatPresetLoadResult(preset);
    const data::PlayerMovementLoadResult movement = data::loadPlayerMovement(packs);
    data::logPlayerMovementLoadResult(movement);
    if (!preset.preset || !movement.tuning || countIssues(textures.issues, data::IssueSeverity::Error) > 0) {
        return std::nullopt;
    }
    return GameData{blocks.registry, preset.preset, movement.tuning, std::move(textures.textures),
                    gameDirectory.path / "assets" / std::string(data::kBaseNamespace) / "shaders"};
}

// Feeds this frame's window input to the cursor state and the camera's look; while flying freely, the movement
// keys move the camera instead of the player.
void handleInput(aurora::platform::Window& window, aurora::ui::ImGuiLayer& imgui,
                 aurora::client::CursorController& cursor, aurora::client::Camera& camera, double frameSeconds,
                 bool freeFlight)
{
    using aurora::platform::Key;
    if (window.wasKeyPressed(Key::Escape)) {
        cursor.onEscape();
    }
    if (window.wasMouseButtonPressed(aurora::platform::MouseButton::Left)) {
        cursor.onClick(imgui.wantsMouse());
    }
    window.setCursorCaptured(cursor.captured());
    imgui.setMouseEnabled(!cursor.captured());
    if (window.cursorMoved()) {
        cursor.onCursorMoved(window.cursorX(), window.cursorY());
    }

    const glm::dvec2 look = cursor.takeLookDelta();
    camera.turn(look.x, look.y);
    if (freeFlight && cursor.acceptsMovement(imgui.wantsKeyboard())) {
        camera.move({.forward = window.isKeyDown(Key::W),
                     .back = window.isKeyDown(Key::S),
                     .left = window.isKeyDown(Key::A),
                     .right = window.isKeyDown(Key::D),
                     .up = window.isKeyDown(Key::Space),
                     .down = window.isKeyDown(Key::LeftShift),
                     .fast = window.isKeyDown(Key::LeftControl)},
                    frameSeconds);
    }
}

aurora::client::MovementKeys heldMovementKeys(const aurora::platform::Window& window)
{
    using aurora::platform::Key;
    return {.forward = window.isKeyDown(Key::W),
            .back = window.isKeyDown(Key::S),
            .left = window.isKeyDown(Key::A),
            .right = window.isKeyDown(Key::D),
            .jump = window.isKeyDown(Key::Space),
            .sneak = window.isKeyDown(Key::LeftShift),
            .sprint = window.isKeyDown(Key::LeftControl)};
}

// Runs the game until the window closes. Every subsystem lives in this scope, so all threads are joined
// before main() closes the log.
int run(const LaunchOptions& options)
{
    using namespace aurora;

    // Data first: broken data ends the run before any window or GL context exists.
    const std::optional<GameData> gameData = loadGameData(options);
    if (!gameData) {
        return 1;
    }
    const data::BlockRegistry& blocks = *gameData->blocks;

    platform::Window window;
    if (!window.create(platform::WindowDesc{})) {
        return 1;
    }

    render::Renderer renderer;
    if (!renderer.init()) {
        return 1;
    }

    // After window.create(): ImGui chains to the window's input callbacks.
    ui::ImGuiLayer imgui;
    if (!imgui.init(window)) {
        return 1;
    }

    // Texture layers and the block table for meshing are CPU data; the renderer turns them into GL objects.
    std::vector<std::string> textureIds;
    for (const data::BlockTexture& texture : gameData->textures) {
        textureIds.push_back(texture.id.str());
    }
    const std::optional<render::TextureLayers> layers = render::assignTextureLayers(textureIds);
    if (!layers) {
        core::logError("render", "{} block textures do not fit the {} texture layers", textureIds.size(),
                       render::kMaxTextureLayers);
        return 1;
    }
    const std::shared_ptr<const render::MeshResources> meshResources = render::buildMeshResources(blocks, *layers);
    render::ChunkRenderer chunkRenderer;
    if (std::string error; !chunkRenderer.init(gameData->shaderFolder, gameData->textures, *layers, error)) {
        core::logError("render", "Cannot set up chunk rendering: {}", error);
        return 1;
    }

    client::Camera camera({kStartX, kStartY, kStartZ}, kStartYaw, kStartPitch);
    client::CursorController cursor;

    core::JobSystem jobs;
    // The server creates the world on its own thread and generates chunks on the job system's workers; the client
    // gets immutable snapshots from it and meshes them on the same workers.
    server::IntegratedServer server(server::ServerConfig{
        .jobs = &jobs,
        .blocks = gameData->blocks,
        .flatPreset = gameData->flatPreset,
        .playerMovement = gameData->movement,
        .loadRadius = kServerLoadRadius,
    });
    if (!server.start()) {
        return 1;
    }
    client::ClientWorld clientWorld(kRenderDistance);

    // The local player: predicted on the client's copy of the world with the same physics as the server's.
    const entity::CollisionShapes collisionShapes = entity::CollisionShapes::fromRegistry(blocks);
    const client::ClientCollisionView collisionView(clientWorld);
    const entity::CollisionWorld collision{collisionView, collisionShapes};
    client::LocalPlayer localPlayer(gameData->movement);
    client::MovementSampler movementSampler;
    core::TickScheduler clientTicks(core::kTickInterval, core::kMaxClientCatchUpTicks);
    bool freeFlight = false;
    // Input stopped: no pending jump, unsettled inputs neutral here and on the server. Called as it happens.
    const auto blockPlayerInput = [&] {
        if (const std::optional<std::uint32_t> through =
                client::blockPlayerInput(movementSampler, localPlayer, collision)) {
            server.neutralizePlayerInputs(*through);
        }
    };
    client::MeshScheduler meshScheduler(jobs, render::makeChunkMesher(meshResources),
                                        jobs.workerCount() * kMeshJobsPerWorker);

    ui::DebugOverlay overlay;
    overlay.setVisible(options.debugOverlay);

    constexpr render::ClearColor kSkyColor{0.10f, 0.14f, 0.22f};
    const float farPlane =
        static_cast<float>((kServerLoadRadius + 1) * core::kSectionSize * 2 + core::kWorldHeight);

    core::TimingHistory frameTimes(kFrameHistory);
    core::TimingHistory cpuTimes(kFrameHistory);
    long long frameCount = 0;
    int exitCode = 0;
    const Clock::time_point startTime = Clock::now();
    Clock::time_point previousFrameStart = startTime;
    bool hasPreviousFrame = false;

    // Variable-rate render loop on the main thread; the server ticks on its own thread at a fixed rate.
    while (!window.shouldClose()) {
        const Clock::time_point frameStart = Clock::now();
        double frameSeconds = 0.0; // 0 for the first frame and after a pause (minimised).
        if (hasPreviousFrame) {
            frameTimes.add(toMilliseconds(frameStart - previousFrameStart));
            frameSeconds = std::chrono::duration<double>(frameStart - previousFrameStart).count();
        }
        previousFrameStart = frameStart;
        hasPreviousFrame = true;

        {
            AURORA_PROFILE_ZONE_N("Input");
            window.pollEvents();
            // Before the minimised branch below can skip the frame: losing focus must release the mouse.
            if (window.takeFocusLost()) {
                cursor.onFocusChanged(false);
                window.setCursorCaptured(false);
                imgui.setMouseEnabled(true);
                blockPlayerInput();
            }
            if (window.wasKeyPressed(platform::Key::F3)) {
                overlay.toggle();
                core::logInfo("app", "Debug overlay {}", overlay.isVisible() ? "shown" : "hidden");
            }
        }
        if (window.isMinimized()) {
            blockPlayerInput(); // No ticks run while minimised; the server goes on with neutral ticks.
            window.waitEvents();
            hasPreviousFrame = false; // Time spent minimized is not a frame.
            continue;
        }
        // ImGui takes this frame's events first, so whether it wants a click is known for where the cursor is
        // now: a click on the F3 panel never captures the mouse, even when the cursor got there this very frame.
        imgui.beginFrame();
        if (!options.screenshot) {
            handleInput(window, imgui, cursor, camera, frameSeconds, freeFlight);
        }
        // A release counts even if a click of the same poll captured again: the input was interrupted.
        if (cursor.takeReleased()) {
            blockPlayerInput();
        }
        const bool acceptingInput = !options.screenshot && window.isFocused() &&
                                    cursor.acceptsMovement(imgui.wantsKeyboard()) && !freeFlight;
        if (!acceptingInput && movementSampler.accepting()) {
            blockPlayerInput(); // Stopped this frame (UI keyboard, free flight); the events above block on their own.
        }
        movementSampler.endFrame(acceptingInput, window.wasKeyPressed(platform::Key::Space));

        {
            AURORA_PROFILE_ZONE_N("World view");
            for (const world::ChunkUpdate& update : server.takeChunkUpdates()) {
                clientWorld.apply(update);
            }

            // The player: the newest server state, then this frame's client ticks.
            const Clock::time_point now = Clock::now();
            if (const std::optional<entity::PlayerState> state = server.takePlayerState()) {
                const bool spawning = !localPlayer.spawned();
                localPlayer.receive(*state, collision);
                if (spawning) {
                    clientTicks.reset(now);
                    core::logInfo("app", "Player spawned at ({:.2f}, {:.2f}, {:.2f})", state->motion.position.x,
                                  state->motion.position.y, state->motion.position.z);
                }
            }
            if (localPlayer.spawned()) {
                const std::uint32_t ticks = clientTicks.advance(now).ticksToRun;
                for (std::uint32_t i = 0; i < ticks; ++i) {
                    const entity::MovementIntent intent =
                        movementSampler.sample(heldMovementKeys(window), static_cast<float>(camera.yaw()),
                                               static_cast<float>(camera.pitch()));
                    if (const std::optional<entity::PlayerInput> input = localPlayer.tick(intent, collision)) {
                        server.sendPlayerInput(*input);
                    }
                }
                if (!freeFlight) {
                    const double alpha = clientTicks.progress(now);
                    camera.setPosition(localPlayer.renderPosition(alpha) +
                                       glm::dvec3(0.0, localPlayer.eyeHeight(alpha), 0.0));
                }
            }
            server.setViewCenterOverride(freeFlight ? std::optional(camera.chunk()) : std::nullopt);

            const world::ChunkPos center = camera.chunk();
            clientWorld.setCenter(center);
            meshScheduler.update(clientWorld, center, camera.sectionY());
            chunkRenderer.queueUploads(meshScheduler.takeReady());
            chunkRenderer.update(clientWorld, kUploadBytesPerFrame);
        }

        {
            AURORA_PROFILE_ZONE_N("Render");
            renderer.clear(kSkyColor);
            const float aspect = static_cast<float>(window.framebufferWidth()) /
                                 static_cast<float>(window.framebufferHeight());
            if (options.drawWorld) {
                chunkRenderer.draw(camera, aspect, farPlane);
            }

            // Captured before the UI is drawn, once every chunk around the camera is meshed and uploaded.
            if (options.screenshot) {
                const bool ready = localPlayer.spawned() && clientWorld.isAreaComplete() && meshScheduler.isSettled() &&
                                   chunkRenderer.pendingUploads() == 0;
                if (ready) {
                    std::string error;
                    if (render::saveFramebufferPng(*options.screenshot, window.framebufferWidth(),
                                                   window.framebufferHeight(), error)) {
                        core::logInfo("app", "Screenshot saved to {} ({}x{}, frame {}, camera at {:.2f}, {:.2f}, "
                                             "{:.2f}, pitch {:.1f})",
                                      core::pathToUtf8(*options.screenshot), window.framebufferWidth(),
                                      window.framebufferHeight(), frameCount + 1, camera.position().x,
                                      camera.position().y, camera.position().z, camera.pitch());
                    } else {
                        core::logError("app", "Screenshot failed: {}", error);
                        exitCode = 1;
                    }
                    window.requestClose();
                } else if (Clock::now() - startTime > kScreenshotTimeout) {
                    const client::MeshSchedulerStats meshes = meshScheduler.stats();
                    core::logError("app",
                                   "Screenshot failed: the world was not ready after {} s ({} of {} chunks drawable, "
                                   "{} meshes waiting, {} in flight, {} failed, {} uploads pending, player {})",
                                   kScreenshotTimeout.count(), clientWorld.eligibleCount(),
                                   (2 * kRenderDistance + 1) * (2 * kRenderDistance + 1), meshes.waiting,
                                   meshes.inFlight, meshes.failed, chunkRenderer.pendingUploads(),
                                   localPlayer.spawned() ? "spawned" : "not spawned");
                    exitCode = 1;
                    window.requestClose();
                }
            }

            if (overlay.isVisible()) {
                ui::DebugOverlayData overlayData{
                    .frameTimes = frameTimes,
                    .cpuTimes = cpuTimes,
                    .server = server.stats(),
                    .workerCount = jobs.workerCount(),
                    .pendingJobs = jobs.pendingJobs(),
                    .blockCount = blocks.blockCount(),
                    .blockStateCount = blocks.stateCount(),
                    .camera = &camera,
                    .renderDistance = kRenderDistance,
                    .chunksHeld = clientWorld.chunkCount(),
                    .chunksDrawable = clientWorld.eligibleCount(),
                    .meshes = meshScheduler.stats(),
                    .gpu = chunkRenderer.stats(),
                    .cursorCaptured = cursor.captured(),
                    .player = &localPlayer,
                    .freeFlight = freeFlight,
                };
                if (overlay.draw(window, renderer, overlayData).toggleFreeFlight) {
                    freeFlight = !freeFlight;
                    if (freeFlight) {
                        blockPlayerInput(); // In this frame: the player must not keep walking until the next.
                    }
                    core::logInfo("app", "Free-flying camera {}", freeFlight ? "on" : "off");
                }
            }
            imgui.endFrame();
        }
        cpuTimes.add(toMilliseconds(Clock::now() - frameStart));

        {
            AURORA_PROFILE_ZONE_N("Swap buffers");
            window.swapBuffers();
        }
        AURORA_PROFILE_FRAME();

        ++frameCount;
        if (options.maxFrames > 0 && frameCount >= options.maxFrames) {
            window.requestClose();
        }
    }

    const double elapsedMs = std::chrono::duration<double, std::milli>(Clock::now() - startTime).count();
    if (frameCount > 0) {
        core::logInfo("app", "{} frames, avg {:.2f} ms/frame", frameCount, elapsedMs / static_cast<double>(frameCount));
    }

    // Reverse start-up order: simulation threads first (queued meshing jobs finish, their results are dropped),
    // then the GL side.
    server.stop();
    jobs.shutdown();
    chunkRenderer.shutdown();
    imgui.shutdown();
    window.destroy();
    return exitCode;
}

} // namespace

int main(int argc, char** argv)
{
    aurora::core::setCurrentThreadName("Main");
    aurora::core::Log::init(aurora::core::LogConfig{.directory = "logs"});
    aurora::core::logInfo("app", "Aurora starting ({} build)", kBuildType);

    LaunchOptions options;
    int exitCode = 2;
    if (parseArgs(argc, argv, options)) {
        // Exceptions are reserved for fatal start-up errors (e.g. a worker thread that cannot be started).
        // run() has already unwound and joined every thread when one lands here.
        try {
            exitCode = run(options);
        } catch (const std::exception& e) {
            aurora::core::logError("app", "Fatal start-up error: {}", e.what());
            exitCode = 1;
        }
    }

    aurora::core::logInfo("app", "Exiting with code {}", exitCode);
    // Last: every thread that logs has been joined inside run().
    aurora::core::Log::shutdown();
    return exitCode;
}
