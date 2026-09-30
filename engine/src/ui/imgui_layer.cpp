#include "ui/imgui_layer.h"

#include "core/log.h"
#include "core/profiler.h"
#include "platform/window.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

namespace aurora::ui {

namespace {

constexpr const char* kGlslVersion = "#version 450 core";

} // namespace

ImGuiLayer::~ImGuiLayer()
{
    shutdown();
}

bool ImGuiLayer::init(platform::Window& window)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename = nullptr; // Debug layout is not persisted yet.

    ImGui::StyleColorsDark();

    // install_callbacks = true: the backend chains to the callbacks Window::create installed, so both see input.
    if (!ImGui_ImplGlfw_InitForOpenGL(window.nativeHandle(), true)) {
        core::logError("ui", "ImGui GLFW backend init failed");
        ImGui::DestroyContext();
        return false;
    }
    if (!ImGui_ImplOpenGL3_Init(kGlslVersion)) {
        core::logError("ui", "ImGui OpenGL3 backend init failed");
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        return false;
    }

    m_initialized = true;
    return true;
}

void ImGuiLayer::shutdown()
{
    if (!m_initialized) {
        return;
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    m_initialized = false;
}

void ImGuiLayer::beginFrame()
{
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void ImGuiLayer::endFrame()
{
    AURORA_PROFILE_ZONE_N("ImGui render");
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

} // namespace aurora::ui
