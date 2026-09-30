#pragma once

namespace aurora::platform {
class Window;
}

namespace aurora::ui {

// Owns the Dear ImGui context and its GLFW + OpenGL3 backends.
class ImGuiLayer {
public:
    ImGuiLayer() = default;
    ~ImGuiLayer();

    ImGuiLayer(const ImGuiLayer&) = delete;
    ImGuiLayer& operator=(const ImGuiLayer&) = delete;

    bool init(platform::Window& window);
    void shutdown();

    void beginFrame();
    void endFrame();

private:
    bool m_initialized = false;
};

} // namespace aurora::ui
