#include "core/job_system.h"
#include "core/log.h"
#include "core/profiler.h"
#include "core/thread.h"
#include "core/timing_history.h"
#include "core/utf8.h"
#include "data/block_loader.h"
#include "data/game_directory.h"
#include "platform/window.h"
#include "render/renderer.h"
#include "server/integrated_server.h"
#include "ui/debug_overlay.h"
#include "ui/imgui_layer.h"

#include <charconv>
#include <chrono>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

// Frame statistics and the F3 graph cover the last 240 frames (4 s at 60 FPS).
constexpr std::size_t kFrameHistory = 240;

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
        } else {
            aurora::core::logError("app", "Unknown argument: {}", arg);
            aurora::core::logError("app", "Usage: aurora [--frames N] [--debug-overlay] [--game-dir <folder>]");
            return false;
        }
    }
    return true;
}

float toMilliseconds(Clock::duration duration)
{
    return std::chrono::duration<float, std::milli>(duration).count();
}

// Finds the game folder and loads the block data. Returns nullptr after logging every problem; the caller exits
// before any window is created.
std::shared_ptr<const aurora::data::BlockRegistry> loadGameData(const LaunchOptions& options)
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
        return nullptr;
    }
    core::logInfo("app", "Game folder: {}{}", core::pathToUtf8(gameDirectory.path),
                  options.gameDir ? " (from --game-dir)" : "");

    // Mods (game/mods/<name>, loaded by name after the base game) come later.
    const std::vector<data::DataPack> packs{{"aurora", gameDirectory.path, true}};
    const data::BlockLoadResult blocks = data::loadBlocks(packs);
    data::logBlockLoadResult(blocks, packs.size());
    return blocks.registry;
}

// Runs the game until the window closes. Every subsystem lives in this scope, so all threads are joined
// before main() closes the log.
int run(const LaunchOptions& options)
{
    using namespace aurora;

    // Data first: broken data ends the run before any window or GL context exists.
    const std::shared_ptr<const data::BlockRegistry> blocks = loadGameData(options);
    if (!blocks) {
        return 1;
    }

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

    core::JobSystem jobs;
    server::IntegratedServer server;
    if (!server.start()) {
        return 1;
    }

    ui::DebugOverlay overlay;
    overlay.setVisible(options.debugOverlay);

    constexpr render::ClearColor kSkyColor{0.10f, 0.14f, 0.22f};

    core::TimingHistory frameTimes(kFrameHistory);
    core::TimingHistory cpuTimes(kFrameHistory);
    long long frameCount = 0;
    const Clock::time_point startTime = Clock::now();
    Clock::time_point previousFrameStart = startTime;
    bool hasPreviousFrame = false;

    // Variable-rate render loop on the main thread; the server ticks on its own thread at a fixed rate.
    while (!window.shouldClose()) {
        const Clock::time_point frameStart = Clock::now();
        if (hasPreviousFrame) {
            frameTimes.add(toMilliseconds(frameStart - previousFrameStart));
        }
        previousFrameStart = frameStart;
        hasPreviousFrame = true;

        {
            AURORA_PROFILE_ZONE_N("Input");
            window.pollEvents();
        }
        if (window.isKeyDown(platform::Key::Escape)) {
            window.requestClose();
        }
        if (window.wasKeyPressed(platform::Key::F3)) {
            overlay.toggle();
            core::logInfo("app", "Debug overlay {}", overlay.isVisible() ? "shown" : "hidden");
        }
        if (window.isMinimized()) {
            window.waitEvents();
            hasPreviousFrame = false; // Time spent minimized is not a frame.
            continue;
        }

        {
            AURORA_PROFILE_ZONE_N("Render");
            renderer.clear(kSkyColor);
            imgui.beginFrame();
            if (overlay.isVisible()) {
                const ui::DebugOverlayData overlayData{
                    frameTimes,          cpuTimes,           server.stats(),       jobs.workerCount(),
                    jobs.pendingJobs(), blocks->blockCount(), blocks->stateCount(),
                };
                overlay.draw(window, renderer, overlayData);
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

    // Reverse start-up order: simulation threads first, then the GL side.
    server.stop();
    jobs.shutdown();
    imgui.shutdown();
    window.destroy();
    return 0;
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
