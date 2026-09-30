# One library per module. Link direction enforces the dependency order:
# core <- platform <- data <- world <- entity <- (server | client) <- render <- ui

find_package(glfw3 CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)

set(AURORA_ENGINE_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src")

# core: header-only for now (constants).
add_library(aurora_core INTERFACE)
add_library(aurora::core ALIAS aurora_core)
target_include_directories(aurora_core INTERFACE "${AURORA_ENGINE_SRC}")
target_compile_features(aurora_core INTERFACE cxx_std_20)

# platform: window, GL context, input.
add_library(aurora_platform STATIC
    src/platform/window.cpp
)
add_library(aurora::platform ALIAS aurora_platform)
target_link_libraries(aurora_platform
    PUBLIC aurora::core
    PRIVATE glfw glad::glad
)
aurora_set_warnings(aurora_platform)

# render: OpenGL state and draw commands.
add_library(aurora_render STATIC
    src/render/renderer.cpp
)
add_library(aurora::render ALIAS aurora_render)
target_link_libraries(aurora_render
    PUBLIC aurora::platform
    PRIVATE glad::glad
)
aurora_set_warnings(aurora_render)

# ui: Dear ImGui debug layer.
add_library(aurora_ui STATIC
    src/ui/imgui_layer.cpp
)
add_library(aurora::ui ALIAS aurora_ui)
target_link_libraries(aurora_ui
    PUBLIC aurora::render imgui::imgui
    PRIVATE glfw
)
aurora_set_warnings(aurora_ui)
