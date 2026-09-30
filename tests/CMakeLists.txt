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
    core/job_system_tests.cpp
    core/log_tests.cpp
    core/sanity_tests.cpp
    core/tick_scheduler_tests.cpp
    core/timing_history_tests.cpp
    data/block_loader_tests.cpp
    data/block_registry_tests.cpp
    data/flat_preset_tests.cpp
    data/game_directory_tests.cpp
    data/path_probe_tests.cpp
    data/resource_id_tests.cpp
    server/integrated_server_tests.cpp
    world/chunk_section_tests.cpp
    world/chunk_tests.cpp
    world/coordinates_tests.cpp
    world/flat_generator_tests.cpp
    world/world_tests.cpp
)
target_link_libraries(aurora_tests PRIVATE aurora::core aurora::data aurora::world aurora::server Catch2::Catch2WithMain)
# The shipped game data is loaded by a test. UTF-8 literal for non-ASCII checkout paths on Windows.
target_compile_definitions(aurora_tests PRIVATE "AURORA_SOURCE_DIR=u8\"${PROJECT_SOURCE_DIR}\"")
aurora_set_warnings(aurora_tests)
# The timeout turns a hang (e.g. a deadlocked shutdown) into a failure instead of a stuck run.
catch_discover_tests(aurora_tests PROPERTIES TIMEOUT 60)

# Start-up with bad game data or a bad --game-dir: every problem is logged and the game exits with code 1 before
# any window exists (no display needed, and any GLFW or window log line fails the test).
add_test(NAME app_game_dir_missing
    COMMAND "${CMAKE_COMMAND}" -DAPP=$<TARGET_FILE:aurora> "-DGAME_DIR=${CMAKE_CURRENT_BINARY_DIR}/no_such_game"
            -DEXPECT_EXIT=1 -DNO_DISPLAY=ON "-DEXPECT1=--game-dir .*no_such_game is not an existing folder"
            "-DFORBID=Game folder:|Loaded|GLFW|Window|OpenGL" -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_app.cmake")
add_test(NAME app_data_errors
    COMMAND "${CMAKE_COMMAND}" -DAPP=$<TARGET_FILE:aurora>
            "-DGAME_DIR=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/broken_game" -DEXPECT_EXIT=1 -DNO_DISPLAY=ON
            "-DEXPECT1=broken_hardness.json /hardness: expected a number >= 0, got string \"abc\""
            "-DEXPECT2=syntax_error.json: parse error at line 5"
            "-DEXPECT3=Block data has 3 error\\(s\\)"
            "-DFORBID=GLFW|Window|OpenGL" -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_app.cmake")
# Valid blocks but a broken flat world preset: the same early exit.
add_test(NAME app_preset_errors
    COMMAND "${CMAKE_COMMAND}" -DAPP=$<TARGET_FILE:aurora>
            "-DGAME_DIR=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/bad_preset_game" -DEXPECT_EXIT=1 -DNO_DISPLAY=ON
            "-DEXPECT1=flat.json /layers/0/height: expected a whole number >= 1, got number 1.5"
            "-DEXPECT2=flat.json /layers/1/block: .*aurora:bedrock"
            "-DEXPECT3=The flat world preset has 2 error\\(s\\)"
            "-DFORBID=GLFW|Window|OpenGL|Server" -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_app.cmake")
set_tests_properties(app_game_dir_missing app_data_errors app_preset_errors PROPERTIES LABELS app TIMEOUT 60)

# Broken links (made at test time; symlinks in git are unreliable on Windows) must stop the start like any other
# unreadable data, never be skipped as "missing".
if(UNIX)
    set(broken_link_root "${CMAKE_CURRENT_BINARY_DIR}/broken_link")
    add_test(NAME app_data_broken_link
        COMMAND "${CMAKE_COMMAND}" -DAPP=$<TARGET_FILE:aurora> "-DGAME_DIR=${broken_link_root}/game"
                "-DBROKEN_LINK=${broken_link_root}/game/data/aurora/blocks" -DEXPECT_EXIT=1 -DNO_DISPLAY=ON
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
        set_tests_properties(app_smoke PROPERTIES LABELS smoke TIMEOUT 60)
    endif()
endif()
