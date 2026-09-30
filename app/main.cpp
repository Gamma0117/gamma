#include "platform/window.h"
#include "render/renderer.h"
#include "ui/imgui_layer.h"

#include <imgui.h>

#include <charconv>
#include <chrono>
#include <cstdio>
#include <string_view>

namespace {

struct LaunchOptions {
    // 0 = run until the window is closed. Used by the headless smoke test.
    long long maxFrames = 0;
};

bool parseArgs(int argc, char** argv, LaunchOptions& options)
{
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--frames" && i + 1 < argc) {
            const std::string_view value = argv[++i];
            const auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), options.maxFrames);
            if (ec != std::errc{} || ptr != value.data() + value.size() || options.maxFrames < 0) {
                std::fprintf(stderr, "[app] Invalid --frames value: %s\n", argv[i]);
                return false;
            }
        } else {
            std::fprintf(stderr, "[app] Unknown argument: %s\n", argv[i]);
            std::fprintf(stderr, "Usage: aurora [--frames N]\n");
            return false;
        }
    }
    return true;
}

void drawDebugWindow(aurora::platform::Window& window, const aurora::render::Renderer& renderer)
{
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Debug");
    ImGui::Text("FPS: %.1f (%.2f ms)", io.Framerate, io.Framerate > 0.0f ? 1000.0f / io.Framerate : 0.0f);
    ImGui::Text("Framebuffer: %d x %d", window.framebufferWidth(), window.framebufferHeight());
    ImGui::Text("OpenGL: %s", renderer.glVersion().c_str());
    ImGui::Text("GPU: %s", renderer.glRenderer().c_str());
    bool vsync = window.isVsync();
    if (ImGui::Checkbox("VSync", &vsync)) {
        window.setVsync(vsync);
    }
    ImGui::End();
}

} // namespace

int main(int argc, char** argv)
{
    using Clock = std::chrono::steady_clock;

    LaunchOptions options;
    if (!parseArgs(argc, argv, options)) {
        return 2;
    }

    aurora::platform::Window window;
    if (!window.create(aurora::platform::WindowDesc{})) {
        return 1;
    }

    aurora::render::Renderer renderer;
    if (!renderer.init()) {
        return 1;
    }

    aurora::ui::ImGuiLayer imgui;
    if (!imgui.init(window)) {
        return 1;
    }

    constexpr aurora::render::ClearColor kSkyColor{0.10f, 0.14f, 0.22f};

    long long frameCount = 0;
    const auto startTime = Clock::now();

    while (!window.shouldClose()) {
        window.pollEvents();
        if (window.isKeyDown(aurora::platform::Key::Escape)) {
            window.requestClose();
        }
        if (window.isMinimized()) {
            window.waitEvents();
            continue;
        }

        renderer.clear(kSkyColor);
        imgui.beginFrame();
        drawDebugWindow(window, renderer);
        imgui.endFrame();
        window.swapBuffers();

        ++frameCount;
        if (options.maxFrames > 0 && frameCount >= options.maxFrames) {
            window.requestClose();
        }
    }

    const double elapsedMs = std::chrono::duration<double, std::milli>(Clock::now() - startTime).count();
    if (frameCount > 0) {
        std::printf("[app] %lld frames, avg %.2f ms/frame\n", frameCount, elapsedMs / static_cast<double>(frameCount));
    }

    imgui.shutdown();
    window.destroy();
    return 0;
}
