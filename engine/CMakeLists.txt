# One library per module. Link direction enforces the dependency order:
# core <- platform <- data <- world <- entity <- (server | client) <- render <- ui
# render is split in two: aurora_render_mesh (CPU meshing, no GL; client <- render_mesh) and aurora_render (GL).

find_package(Threads REQUIRED)
find_package(Tracy CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(Stb REQUIRED)

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

# data: game content loaded from game/data JSON (block registry) and block texture images (decoded on the CPU,
# before any window exists). JSON and stb stay out of the public headers.
add_library(aurora_data STATIC
    src/data/block_loader.cpp
    src/data/block_registry.cpp
    src/data/block_textures.cpp
    src/data/flat_preset.cpp
    src/data/game_directory.cpp
    src/data/json_support.cpp
    src/data/load_issue.cpp
    src/data/pack_files.cpp
    src/data/path_probe.cpp
    src/data/player_movement.cpp
    src/data/resource_id.cpp
)
add_library(aurora::data ALIAS aurora_data)
target_link_libraries(aurora_data
    PUBLIC aurora::core
    PRIVATE nlohmann_json::nlohmann_json
)
# SYSTEM: the single-header library is not held to our warning flags.
target_include_directories(aurora_data SYSTEM PRIVATE "${Stb_INCLUDE_DIR}")
aurora_set_warnings(aurora_data)

# world: chunks, sections, the chunk table and world generation. Plain data plus jobs; no rendering.
add_library(aurora_world STATIC
    src/world/chunk.cpp
    src/world/chunk_section.cpp
    src/world/chunk_snapshot.cpp
    src/world/flat_generator.cpp
    src/world/world.cpp
)
add_library(aurora::world ALIAS aurora_world)
target_link_libraries(aurora_world PUBLIC aurora::data)
aurora_set_warnings(aurora_world)

# entity: what moves in the world. Collision boxes against the block grid and the player's movement step, shared by
# the server (authoritative) and the client (prediction). Plain data and functions; no threads, no rendering.
add_library(aurora_entity STATIC
    src/entity/aabb.cpp
    src/entity/collision.cpp
    src/entity/collision_shapes.cpp
    src/entity/player_movement.cpp
)
add_library(aurora::entity ALIAS aurora_entity)
target_link_libraries(aurora_entity PUBLIC aurora::world glm::glm)
aurora_set_warnings(aurora_entity)

# client: the client's copy of the world (snapshots from the server), what to mesh and when, the camera, who owns
# the mouse, and the local player's input and prediction. CPU only: no window or GL here, so all of it is tested
# without a context.
add_library(aurora_client STATIC
    src/client/camera.cpp
    src/client/client_collision_view.cpp
    src/client/client_world.cpp
    src/client/cursor_controller.cpp
    src/client/frustum.cpp
    src/client/local_player.cpp
    src/client/mesh_scheduler.cpp
    src/client/movement_sampler.cpp
    src/client/player_control.cpp
)
add_library(aurora::client ALIAS aurora_client)
target_link_libraries(aurora_client PUBLIC aurora::entity)
aurora_set_warnings(aurora_client)

# server: authoritative simulation on its own fixed-rate thread. Knows nothing about render or ui.
add_library(aurora_server STATIC
    src/server/integrated_server.cpp
    src/server/server_player.cpp
    src/server/world_collision_view.cpp
)
add_library(aurora::server ALIAS aurora_server)
target_link_libraries(aurora_server PUBLIC aurora::entity)
aurora_set_warnings(aurora_server)

# render_mesh: the CPU half of rendering. Turns sections into vertex data on worker threads; no GL, so it is tested
# without a context. The client's mesh scheduler calls it through an injected function (client <- render_mesh).
add_library(aurora_render_mesh STATIC
    src/render/chunk_mesher.cpp
    src/render/mesh_resources.cpp
    src/render/mesh_vertex.cpp
    src/render/upload_queue.cpp
)
add_library(aurora::render_mesh ALIAS aurora_render_mesh)
target_link_libraries(aurora_render_mesh PUBLIC aurora::client)
aurora_set_warnings(aurora_render_mesh)

# render: OpenGL state and draw commands.
add_library(aurora_render STATIC
    src/render/chunk_renderer.cpp
    src/render/renderer.cpp
    src/render/screenshot.cpp
    src/render/shader.cpp
    src/render/texture_array.cpp
)
add_library(aurora::render ALIAS aurora_render)
target_link_libraries(aurora_render
    PUBLIC aurora::platform aurora::render_mesh
    PRIVATE glad::glad
)
target_include_directories(aurora_render SYSTEM PRIVATE "${Stb_INCLUDE_DIR}")
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
