#include "ui/debug_overlay.h"

#include "core/profiler.h"
#include "core/timing_history.h"
#include "platform/window.h"
#include "render/renderer.h"

#include <imgui.h>

#include <algorithm>
#include <format>
#include <string>

namespace aurora::ui {

namespace {

constexpr float kMargin = 10.0f;
constexpr float kBackgroundAlpha = 0.6f;
constexpr ImVec2 kGraphSize{300.0f, 60.0f};
// The frame graph always shows at least 0..33.3 ms (30 FPS) so a steady 60 FPS sits mid-height.
constexpr float kGraphMinScaleMs = 1000.0f / 30.0f;

#ifdef NDEBUG
constexpr const char* kBuildType = "Release";
#else
constexpr const char* kBuildType = "Debug";
#endif

void drawFrameSection(const DebugOverlayData& data)
{
    const core::TimingSummary frame = data.frameTimes.summary();
    const core::TimingSummary cpu = data.cpuTimes.summary();
    const float fps = frame.averageMs > 0.0f ? 1000.0f / frame.averageMs : 0.0f;

    ImGui::Text("%.0f FPS   frame %.2f ms", static_cast<double>(fps), static_cast<double>(frame.averageMs));
    ImGui::Text("  min %.2f / max %.2f ms over %zu frames", static_cast<double>(frame.minMs),
                static_cast<double>(frame.maxMs), data.frameTimes.size());
    ImGui::Text("  CPU %.2f ms (max %.2f)", static_cast<double>(cpu.averageMs), static_cast<double>(cpu.maxMs));

    const float scaleMax = std::max(kGraphMinScaleMs, frame.maxMs * 1.1f);
    const std::string scaleLabel = std::format("0 - {:.1f} ms", scaleMax);
    ImGui::PlotLines("##frame_times", data.frameTimes.data(), static_cast<int>(data.frameTimes.size()),
                     static_cast<int>(data.frameTimes.offset()), scaleLabel.c_str(), 0.0f, scaleMax, kGraphSize);
}

void drawServerSection(const DebugOverlayData& data)
{
    const server::ServerStats& server = data.server;
    if (!server.running) {
        ImGui::TextUnformatted("Server stopped");
        return;
    }
    ImGui::Text("Server %.1f TPS   tick %.3f ms (max %.3f)", server.ticksPerSecond,
                static_cast<double>(server.tickTime.averageMs), static_cast<double>(server.tickTime.maxMs));
    ImGui::Text("  tick #%llu, skipped %llu", static_cast<unsigned long long>(server.tickCount),
                static_cast<unsigned long long>(server.skippedTicks));
}

void drawSystemSection(platform::Window& window, const render::Renderer& renderer)
{
    ImGui::Text("Window %d x %d", window.framebufferWidth(), window.framebufferHeight());
    ImGui::SameLine();
    bool vsync = window.isVsync();
    if (ImGui::Checkbox("VSync", &vsync)) {
        window.setVsync(vsync);
    }
    ImGui::Text("OpenGL %s", renderer.glVersion().c_str());
    ImGui::Text("GPU %s", renderer.glRenderer().c_str());
#if AURORA_PROFILER_ENABLED
    ImGui::Text("Tracy %s", AURORA_PROFILER_IS_CONNECTED() ? "connected" : "waiting (localhost, on demand)");
#else
    ImGui::TextUnformatted("Tracy off");
#endif
}

} // namespace

void DebugOverlay::draw(platform::Window& window, const render::Renderer& renderer,
                        const DebugOverlayData& data) const
{
    if (!m_visible) {
        return;
    }
    AURORA_PROFILE_ZONE_N("Debug overlay");

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + kMargin, viewport->WorkPos.y + kMargin), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(kBackgroundAlpha);
    constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove |
                                        ImGuiWindowFlags_NoDocking;

    if (ImGui::Begin("Debug (F3)", nullptr, kFlags)) {
        ImGui::Text("Aurora (%s)", kBuildType);
        ImGui::Separator();
        drawFrameSection(data);
        ImGui::Separator();
        drawServerSection(data);
        ImGui::Text("Workers %zu (pending jobs %zu)", data.workerCount, data.pendingJobs);
        ImGui::Text("Blocks %zu (%u states)", data.blockCount, static_cast<unsigned>(data.blockStateCount));
        ImGui::Separator();
        drawSystemSection(window, renderer);
        ImGui::TextDisabled("F3: hide");
    }
    ImGui::End();
}

} // namespace aurora::ui
