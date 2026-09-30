#pragma once

#include "server/server_stats.h"

#include <cstddef>

namespace aurora::core {
class TimingHistory;
}

namespace aurora::platform {
class Window;
}

namespace aurora::render {
class Renderer;
}

namespace aurora::ui {

// Numbers the F3 overlay shows besides window and GL info. Filled by the app every frame.
struct DebugOverlayData {
    const core::TimingHistory& frameTimes; // Time between frame starts.
    const core::TimingHistory& cpuTimes;   // Main-thread work per frame, excluding the swap (VSync) wait.
    server::ServerStats server;
    std::size_t workerCount = 0;
    std::size_t pendingJobs = 0;
};

// F3 debug screen: a translucent panel in the top-left corner with frame timing, server ticks, workers,
// window and driver info. Hidden until toggled.
class DebugOverlay {
public:
    bool isVisible() const { return m_visible; }
    void setVisible(bool visible) { m_visible = visible; }
    void toggle() { m_visible = !m_visible; }

    // Call between ImGuiLayer::beginFrame and endFrame. Takes the window for the VSync toggle.
    void draw(platform::Window& window, const render::Renderer& renderer, const DebugOverlayData& data) const;

private:
    bool m_visible = false;
};

} // namespace aurora::ui
