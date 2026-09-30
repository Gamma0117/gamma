# One library per module. Link direction enforces the dependency order:
# core <- platform <- data <- world <- entity <- (server | client) <- render <- ui

find_package(Threads REQUIRED)
find_package(Tracy CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# The overlay triplets build Tracy with TRACY_ENABLE; without it every profiler macro compiles to nothing.
get_target_property(AURORA_TRACY_DEFINITIONS Tracy::TracyClient INTERFACE_COMPILE_DEFINITIONS)
if(NOT "TRACY_ENABLE" IN_LIST AURORA_TRACY_DEFINITIONS)
    message(WARNING "Tracy client was built without TRACY_ENABLE: profiling is disabled. "
                    "Delete the build folder and configure again so vcpkg rebuilds tracy with cmake/triplets.")
endif()

set(AURORA_ENGINE_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src")

# core: constants, logging, job system, tick timing, profiler hooks.
add_library(aurora_core STATIC
    src/core/job_system.cpp
    src/core/log.cpp
    src/core/thread.cpp
    src/core/tick_scheduler.cpp
    src/core/timing_history.cpp
)
add_library(aurora::core ALIAS aurora_core)
target_include_directories(aurora_core PUBLIC "${AURORA_ENGINE_SRC}")
target_compile_features(aurora_core PUBLIC cxx_std_20)
target_link_libraries(aurora_core PUBLIC Tracy::TracyClient Threads::Threads)
aurora_set_warnings(aurora_core)

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

# data: game content loaded from game/data JSON (block registry). JSON stays out of the public headers.
add_library(aurora_data STATIC
    src/data/block_loader.cpp
    src/data/block_registry.cpp
    src/data/game_directory.cpp
    src/data/load_issue.cpp
    src/data/path_probe.cpp
    src/data/resource_id.cpp
)
add_library(aurora::data ALIAS aurora_data)
target_link_libraries(aurora_data
    PUBLIC aurora::core
    PRIVATE nlohmann_json::nlohmann_json
)
aurora_set_warnings(aurora_data)

# server: authoritative simulation on its own fixed-rate thread. Knows nothing about render or ui.
add_library(aurora_server STATIC
    src/server/integrated_server.cpp
)
add_library(aurora::server ALIAS aurora_server)
target_link_libraries(aurora_server PUBLIC aurora::core)
aurora_set_warnings(aurora_server)

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

# ui: Dear ImGui debug layer and the F3 overlay (reads server stats for display).
add_library(aurora_ui STATIC
    src/ui/debug_overlay.cpp
    src/ui/imgui_layer.cpp
)
add_library(aurora::ui ALIAS aurora_ui)
target_link_libraries(aurora_ui
    PUBLIC aurora::render aurora::server imgui::imgui
    PRIVATE glfw
)
aurora_set_warnings(aurora_ui)
