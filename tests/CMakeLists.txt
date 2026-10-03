find_package(Catch2 3 CONFIG REQUIRED)
include(Catch)

# Catch2 splits a test-name filter at commas, so a test whose name has a comma cannot be run on its own by name.
file(GLOB_RECURSE AURORA_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
foreach(source IN LISTS AURORA_TEST_SOURCES)
    file(STRINGS "${source}" names_with_commas REGEX "TEST_CASE\\(\"[^\"]*,[^\"]*\"")
    if(names_with_commas)
        message(FATAL_ERROR "Test names must not contain commas (${source}): ${names_with_commas}")
    endif()
endforeach()

add_executable(aurora_tests
    client/block_action_integration_tests.cpp
    client/block_particles_tests.cpp
    client/camera_tests.cpp
    client/client_world_changes_tests.cpp
    client/client_world_tests.cpp
    client/cursor_controller_tests.cpp
    client/local_player_tests.cpp
    client/loop_simulation_tests.cpp
    client/mesh_scheduler_tests.cpp
    client/movement_sampler_tests.cpp
    client/player_action_input_tests.cpp
    client/player_input_loop_tests.cpp
    client/section_latency_tests.cpp
    client/player_integration_tests.cpp
    core/job_system_tests.cpp
    core/log_tests.cpp
    core/sanity_tests.cpp
    core/tick_scheduler_tests.cpp
    core/timing_history_tests.cpp
    data/block_loader_tests.cpp
    data/block_registry_tests.cpp
    data/block_textures_tests.cpp
    data/flat_preset_tests.cpp
    data/game_directory_tests.cpp
    data/path_probe_tests.cpp
    data/player_interaction_tests.cpp
    data/player_movement_tests.cpp
    data/resource_id_tests.cpp
    entity/block_raycast_tests.cpp
    entity/collision_tests.cpp
    entity/player_movement_tests.cpp
    platform/input_state_tests.cpp
    render/axis_texture_tests.cpp
    render/block_change_pipeline_tests.cpp
    render/chunk_mesher_tests.cpp
    render/mesh_pipeline_tests.cpp
    render/upload_queue_tests.cpp
    server/block_interaction_tests.cpp
    server/integrated_server_tests.cpp
    server/player_physics_tests.cpp
    server/server_player_tests.cpp
    support/png_test_support.cpp
    world/chunk_section_tests.cpp
    world/chunk_tests.cpp
    world/coordinates_tests.cpp
    world/flat_generator_tests.cpp
    world/world_changes_tests.cpp
    world/world_tests.cpp
)
# CPU libraries only: no test needs a window or a GL context.
target_link_libraries(aurora_tests PRIVATE aurora::core aurora::data aurora::world aurora::entity aurora::client
                                           aurora::render_mesh
                                           aurora::server Catch2::Catch2WithMain)
target_include_directories(aurora_tests SYSTEM PRIVATE "${Stb_INCLUDE_DIR}")
# The shipped game data is loaded by a test. UTF-8 literal for non-ASCII checkout paths on Windows.
target_compile_definitions(aurora_tests PRIVATE "AURORA_SOURCE_DIR=u8\"${PROJECT_SOURCE_DIR}\"")
aurora_set_warnings(aurora_tests)
# The timeout turns a hang (e.g. a deadlocked shutdown) into a failure instead of a stuck run.
catch_discover_tests(aurora_tests PROPERTIES TIMEOUT 60)

# Reads a screenshot of the start view and says whether it shows terrain (see tools/screenshot_check.cpp).
add_executable(aurora_screenshot_check tools/screenshot_check.cpp)
target_link_libraries(aurora_screenshot_check PRIVATE aurora::data)
aurora_set_warnings(aurora_screenshot_check)

# Draws block effects and block changes in a fixed scene with the real GL renderers and judges the frames by their
# pixels (see tools/effects_check.cpp). Needs a display: registered with the Xvfb tests below.
find_package(glad CONFIG REQUIRED)
add_executable(aurora_effects_check tools/effects_check.cpp)
target_link_libraries(aurora_effects_check PRIVATE aurora::render aurora::world aurora::data glad::glad)
aurora_set_warnings(aurora_effects_check)

# tools/latency_report.py: whole-run percentiles from an --latency-log record (0 ms is a valid latency), and records
# that are cut, lost completions or hold a latency that is not a finite number >= 0 (NaN, +-infinity, negative)
# reported as incomplete. Each record's exact output is in expected/<record>.txt; run_latency_report.cmake checks it
# and the exact exit code (0 whole run, 2 incomplete).
find_program(AURORA_PYTHON3 NAMES python3 python)
if(AURORA_PYTHON3)
    set(latency_records "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/latency_record")
    foreach(record_exit IN ITEMS complete:0 zero:0 cut:2 dropped:2 nan:2 infinity:2 negative:2)
        string(REPLACE ":" ";" record_exit "${record_exit}")
        list(GET record_exit 0 record)
        list(GET record_exit 1 expected_exit)
        add_test(NAME latency_report_${record}
            COMMAND "${CMAKE_COMMAND}" "-DPYTHON=${AURORA_PYTHON3}"
                    "-DTOOL=${PROJECT_SOURCE_DIR}/tools/latency_report.py"
                    "-DRECORD=${latency_records}/${record}.csv" -DEXPECT_EXIT=${expected_exit}
                    "-DEXPECT_OUTPUT=${latency_records}/expected/${record}.txt"
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_latency_report.cmake")
        set_tests_properties(latency_report_${record} PROPERTIES LABELS tools)
    endforeach()
endif()

# Start-up with bad game data or a bad --game-dir: every problem is logged and the game exits with code 1 before
# any window exists (no display needed, and any GLFW or window log line fails the test).
#
# Most fixtures hold only their broken files: run_app.cmake copies the shipped game into a staging folder of the
# test's own and the fixture on top (BASE_GAME), so the one loader under test fails and every other loader must
# load cleanly (their error summaries are forbidden).
set(app_base_game "${PROJECT_SOURCE_DIR}/game")
set(app_staging "${CMAKE_CURRENT_BINARY_DIR}/app_staging")
add_test(NAME app_game_dir_missing
    COMMAND "${CMAKE_COMMAND}" -DAPP=$<TARGET_FILE:aurora> "-DGAME_DIR=${CMAKE_CURRENT_BINARY_DIR}/no_such_game"
            -DEXPECT_EXIT=1 -DNO_DISPLAY=ON "-DEXPECT1=--game-dir .*no_such_game is not an existing folder"
            "-DFORBID=Game folder:|Loaded|GLFW|Window|OpenGL" -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_app.cmake")
# Block files alone (no shipped game): the block loader stops the start before any other loader runs.
add_test(NAME app_data_errors
    COMMAND "${CMAKE_COMMAND}" -DAPP=$<TARGET_FILE:aurora>
            "-DGAME_DIR=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/broken_game" -DEXPECT_EXIT=1 -DNO_DISPLAY=ON
            "-DEXPECT1=broken_hardness.json /hardness: expected a number >= 0, got string \"abc\""
            "-DEXPECT2=syntax_error.json: parse error at line 5"
            "-DEXPECT3=Block data has 3 error\\(s\\)"
            "-DFORBID=GLFW|Window|OpenGL" -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_app.cmake")
# Each loader's summary line. An error test of one loader forbids the others' lines (only its own data is broken)
# besides any window or server line.
set(app_loader_summaries "Block data has" "Block textures have" "The flat world preset has"
    "The player movement settings have" "The player interaction settings have" "Crack textures have")
function(app_forbid out own)
    set(forbid GLFW Window OpenGL Server)
    foreach(summary IN LISTS app_loader_summaries)
        if(NOT summary STREQUAL own)
            list(APPEND forbid "${summary}")
        endif()
    endforeach()
    list(JOIN forbid "|" joined)
    set(${out} "${joined}" PARENT_SCOPE)
endfunction()
app_forbid(app_forbid_preset "The flat world preset has")
app_forbid(app_forbid_textures "Block textures have")
app_forbid(app_forbid_movement "The player movement settings have")
app_forbid(app_forbid_interaction "The player interaction settings have")
app_forbid(app_forbid_cracks "Crack textures have")
# The shipped game with a broken flat world preset: the same early exit.
add_test(NAME app_preset_errors
    COMMAND "${CMAKE_COMMAND}" -DAPP=$<TARGET_FILE:aurora> "-DBASE_GAME=${app_base_game}"
            "-DSTAGING=${app_staging}/app_preset_errors"
            "-DGAME_DIR=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/bad_preset_game" -DEXPECT_EXIT=1 -DNO_DISPLAY=ON
            "-DEXPECT1=flat.json /layers/0/height: expected a whole number >= 1, got number 1.5"
            "-DEXPECT2=flat.json /layers/1/block: .*aurora:bedrock"
            "-DEXPECT3=The flat world preset has 2 error\\(s\\)"
            "-DFORBID=${app_forbid_preset}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_app.cmake")
# A block texture that is not a PNG (and one of the wrong size): the same early exit, naming each file.
add_test(NAME app_texture_errors
    COMMAND "${CMAKE_COMMAND}" -DAPP=$<TARGET_FILE:aurora> "-DBASE_GAME=${app_base_game}"
            "-DSTAGING=${app_staging}/app_texture_errors"
            "-DGAME_DIR=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/bad_texture_game" -DEXPECT_EXIT=1 -DNO_DISPLAY=ON
            "-DEXPECT1=stone.png: not a PNG file"
            "-DEXPECT2=dirt.png: a block texture must be 32x32 pixels, this one is 16x16"
            "-DEXPECT3=Block textures have 2 error\\(s\\)"
            "-DFORBID=${app_forbid_textures}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_app.cmake")
# Broken player movement settings: the same early exit.
add_test(NAME app_movement_errors
    COMMAND "${CMAKE_COMMAND}" -DAPP=$<TARGET_FILE:aurora> "-DBASE_GAME=${app_base_game}"
            "-DSTAGING=${app_staging}/app_movement_errors"
            "-DGAME_DIR=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/bad_movement_game" -DEXPECT_EXIT=1 -DNO_DISPLAY=ON
            "-DEXPECT1=movement.json /walk_speed: expected a number in \\[0, 50\\], got string \"fast\""
            "-DEXPECT2=movement.json /step_height: must be below height \\(1.8\\): got 2.5"
            "-DEXPECT3=The player movement settings have 2 error\\(s\\)"
            "-DFORBID=${app_forbid_movement}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_app.cmake")
# Valid in every other way, but gravity 0 (the player would never find the ground): the same early exit.
add_test(NAME app_movement_zero_gravity
    COMMAND "${CMAKE_COMMAND}" -DAPP=$<TARGET_FILE:aurora> "-DBASE_GAME=${app_base_game}"
            "-DSTAGING=${app_staging}/app_movement_zero_gravity"
            "-DGAME_DIR=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/zero_gravity_game" -DEXPECT_EXIT=1 -DNO_DISPLAY=ON
            "-DEXPECT1=movement.json /gravity: expected a number in \\(0, 200\\], got number 0"
            "-DEXPECT2=The player movement settings have 1 error\\(s\\)"
            "-DFORBID=${app_forbid_movement}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_app.cmake")
# Broken player interaction settings: the same early exit.
add_test(NAME app_interaction_errors
    COMMAND "${CMAKE_COMMAND}" -DAPP=$<TARGET_FILE:aurora> "-DBASE_GAME=${app_base_game}"
            "-DSTAGING=${app_staging}/app_interaction_errors"
            "-DGAME_DIR=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/bad_interaction_game" -DEXPECT_EXIT=1 -DNO_DISPLAY=ON
            "-DEXPECT1=interaction.json /reach: expected a number in \\[0.1, 16\\], got string \"far\""
            "-DEXPECT2=interaction.json /palette/1: unknown block aurora:marble"
            "-DEXPECT3=The player interaction settings have 2 error\\(s\\)"
            "-DFORBID=${app_forbid_interaction}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_app.cmake")
# A crack stage of the wrong size: the same early exit.
add_test(NAME app_crack_texture_errors
    COMMAND "${CMAKE_COMMAND}" -DAPP=$<TARGET_FILE:aurora> "-DBASE_GAME=${app_base_game}"
            "-DSTAGING=${app_staging}/app_crack_texture_errors"
            "-DGAME_DIR=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/bad_crack_game" -DEXPECT_EXIT=1 -DNO_DISPLAY=ON
            "-DEXPECT1=destroy_stage_4.png: a block texture must be 32x32 pixels, this one is 16x16"
            "-DEXPECT2=Crack textures have 1 error\\(s\\)"
            "-DFORBID=${app_forbid_cracks}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_app.cmake")
set_tests_properties(app_game_dir_missing app_data_errors app_preset_errors app_texture_errors app_movement_errors
                     app_movement_zero_gravity app_interaction_errors app_crack_texture_errors
                     PROPERTIES LABELS app TIMEOUT 60)
# The staging copy must take every fixture file over the base file even when both have the same timestamp and size
# (a fresh checkout gave them that, and the base data was silently kept). POSIX only (touch -d, stat).
if(UNIX)
    add_test(NAME app_fixture_overlay_same_timestamp
        COMMAND "${CMAKE_COMMAND}" "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/overlay_test"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/test_game_overlay.cmake")
    set_tests_properties(app_fixture_overlay_same_timestamp PROPERTIES LABELS app TIMEOUT 60)
endif()

# Broken links (made at test time; symlinks in git are unreliable on Windows) must stop the start like any other
# unreadable data, never be skipped as "missing".
if(UNIX)
    set(broken_link_root "${CMAKE_CURRENT_BINARY_DIR}/broken_link")
    # The shipped game with its blocks folder replaced by a broken link (after the staging copy).
    add_test(NAME app_data_broken_link
        COMMAND "${CMAKE_COMMAND}" -DAPP=$<TARGET_FILE:aurora> "-DBASE_GAME=${app_base_game}"
                "-DSTAGING=${app_staging}/app_data_broken_link"
                "-DBROKEN_LINK=${app_staging}/app_data_broken_link/data/aurora/blocks" -DEXPECT_EXIT=1 -DNO_DISPLAY=ON
                "-DEXPECT1=data/aurora/blocks: cannot access the folder: broken link"
                "-DFORBID=Loaded|GLFW|Window|OpenGL" -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_app.cmake")
    add_test(NAME app_game_candidate_broken_link
        COMMAND "${CMAKE_COMMAND}" -DAPP=$<TARGET_FILE:aurora> "-DWORK_DIR=${broken_link_root}/cwd"
                "-DBROKEN_LINK=${broken_link_root}/cwd/game/data/aurora" -DEXPECT_EXIT=1 -DNO_DISPLAY=ON
                "-DEXPECT1=cannot access .*cwd/game/data/aurora: broken link"
                "-DFORBID=Game folder:|Loaded|GLFW|Window|OpenGL" -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_app.cmake")
    # A broken link in the middle of the candidate path (game/data -> missing), not at its end.
    add_test(NAME app_game_candidate_middle_link
        COMMAND "${CMAKE_COMMAND}" -DAPP=$<TARGET_FILE:aurora> "-DWORK_DIR=${broken_link_root}/cwd_middle"
                "-DBROKEN_LINK=${broken_link_root}/cwd_middle/game/data" -DEXPECT_EXIT=1 -DNO_DISPLAY=ON
                "-DEXPECT1=cannot access .*cwd_middle/game/data/aurora: broken link .*cwd_middle/game/data"
                "-DFORBID=Game folder:|Loaded|GLFW|Window|OpenGL" -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_app.cmake")
    # A file where a folder of the candidate path should be (game/data is a file): not "missing", so no fallback.
    add_test(NAME app_game_candidate_file_in_path
        COMMAND "${CMAKE_COMMAND}" -DAPP=$<TARGET_FILE:aurora> "-DWORK_DIR=${broken_link_root}/cwd_file"
                "-DMAKE_FILE=${broken_link_root}/cwd_file/game/data" -DEXPECT_EXIT=1 -DNO_DISPLAY=ON
                "-DEXPECT1=cannot access .*cwd_file/game/data/aurora: .*cwd_file/game/data is not a folder"
                "-DFORBID=Game folder:|Loaded|GLFW|Window|OpenGL" -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_app.cmake")
    set_tests_properties(app_data_broken_link app_game_candidate_broken_link app_game_candidate_middle_link
                         app_game_candidate_file_in_path PROPERTIES LABELS app TIMEOUT 60)
endif()

# Headless smoke test: open the real window under Xvfb (Mesa llvmpipe), draw the F3 overlay and exit after a
# few frames.
if(UNIX AND NOT APPLE)
    find_program(XVFB_RUN xvfb-run)
    if(XVFB_RUN)
        add_test(NAME app_smoke
            COMMAND "${XVFB_RUN}" -a -s "-screen 0 1280x720x24" $<TARGET_FILE:aurora> --frames 60 --debug-overlay)
        # xvfb-run -a is not safe to start several times at once (display numbers, its temporary folder), so the
        # tests that use it never run in parallel with each other (ctest -j).
        set_tests_properties(app_smoke PROPERTIES LABELS smoke TIMEOUT 60 RESOURCE_LOCK xvfb)

        # The start view once the world around it is drawn: sky above, grass below, seen from the player's eyes
        # (spawn + eye_height, start pitch -25). The second run draws everything but the chunks and must be told
        # apart (the checker reports "no terrain"); a failed run, a camera not at the eyes or a missing or broken
        # image fails both.
        set(screenshot_dir "${CMAKE_CURRENT_BINARY_DIR}/screenshots")
        file(MAKE_DIRECTORY "${screenshot_dir}")
        add_test(NAME app_render_screenshot
            COMMAND "${CMAKE_COMMAND}" "-DXVFB_RUN=${XVFB_RUN}" -DAPP=$<TARGET_FILE:aurora>
                    -DCHECKER=$<TARGET_FILE:aurora_screenshot_check> "-DOUTPUT=${screenshot_dir}/start_view.png"
                    -DEXPECT=terrain -DEXPECT_PITCH=-25.0 -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_screenshot.cmake")
        add_test(NAME app_render_screenshot_without_world
            COMMAND "${CMAKE_COMMAND}" "-DXVFB_RUN=${XVFB_RUN}" -DAPP=$<TARGET_FILE:aurora>
                    -DCHECKER=$<TARGET_FILE:aurora_screenshot_check>
                    "-DOUTPUT=${screenshot_dir}/start_view_no_world.png" -DEXPECT=no_terrain -DEXPECT_PITCH=-25.0
                    -DAPP_ARGS=--no-world-draw -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_screenshot.cmake")
        set_tests_properties(app_render_screenshot app_render_screenshot_without_world
                             PROPERTIES LABELS smoke TIMEOUT 120 RESOURCE_LOCK xvfb)

        # Selection lines, cracks, fragments and block changes on screen, each with its negative control (the
        # effect or the change left out, or depth testing off), which must fail the same pixel check.
        set(effects_dir "${CMAKE_CURRENT_BINARY_DIR}/effects")
        file(MAKE_DIRECTORY "${effects_dir}")
        foreach(effects_case IN ITEMS outline crack_mid crack_late particles removed placed occluded)
            foreach(negative IN ITEMS OFF ON)
                set(effects_test "effects_${effects_case}")
                if(negative)
                    string(APPEND effects_test "_negative")
                endif()
                add_test(NAME ${effects_test}
                    COMMAND "${CMAKE_COMMAND}" "-DXVFB_RUN=${XVFB_RUN}" -DTOOL=$<TARGET_FILE:aurora_effects_check>
                            "-DGAME_DIR=${PROJECT_SOURCE_DIR}/game" "-DOUTPUT_DIR=${effects_dir}"
                            -DCASE=${effects_case} -DNEGATIVE=${negative}
                            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_effects.cmake")
                set_tests_properties(${effects_test} PROPERTIES LABELS "smoke;effects" TIMEOUT 120 RESOURCE_LOCK xvfb)
            endforeach()
        endforeach()
    endif()
endif()
