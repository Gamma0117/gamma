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

    // Whether ImGui wants the mouse or keyboard (it is over or focused on a panel) as of the last frame. The game
    // ignores such input.
    bool wantsMouse() const;
    bool wantsKeyboard() const;
    // While the game owns the mouse (captured cursor), ImGui gets no mouse input at all.
    void setMouseEnabled(bool enabled);

private:
    bool m_initialized = false;
};

} // namespace aurora::ui
