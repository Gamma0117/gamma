# Runs the game once and checks its exit code and log output.
#   cmake -DAPP=<exe> -DEXPECT_EXIT=<code> [-DGAME_DIR=<folder>] [-DWORK_DIR=<folder>] [-DNO_DISPLAY=ON]
#         [-DBROKEN_LINK=<path>] [-DMAKE_FILE=<path>]
#         [-DEXPECT1=<regex>] [-DEXPECT2=<regex>] [-DEXPECT3=<regex>] [-DFORBID=<regex>]
#         -P run_app.cmake
# GAME_DIR is passed as --game-dir; without it the game searches from WORK_DIR (its working directory).
# NO_DISPLAY removes DISPLAY/WAYLAND_DISPLAY, so reaching window creation would fail with a GLFW error.
# BROKEN_LINK is created fresh as a symbolic link to a missing target before the run (its parents are created).
# MAKE_FILE is created as a small regular file before the run (its parents are created), e.g. in place of a folder.

set(env_args)
if(NO_DISPLAY)
    list(APPEND env_args --unset=DISPLAY --unset=WAYLAND_DISPLAY)
endif()
set(app_args --frames 1)
if(DEFINED GAME_DIR)
    list(APPEND app_args --game-dir "${GAME_DIR}")
endif()
if(NOT DEFINED WORK_DIR)
    set(WORK_DIR "${CMAKE_CURRENT_BINARY_DIR}")
endif()

if(DEFINED BROKEN_LINK)
    get_filename_component(link_parent "${BROKEN_LINK}" DIRECTORY)
    file(REMOVE "${BROKEN_LINK}")
    file(MAKE_DIRECTORY "${link_parent}" "${WORK_DIR}")
    file(CREATE_LINK "${link_parent}/no_such_link_target" "${BROKEN_LINK}" SYMBOLIC RESULT link_result)
    if(NOT link_result EQUAL 0)
        message(FATAL_ERROR "Cannot create the broken link ${BROKEN_LINK}: ${link_result}")
    endif()
endif()

if(DEFINED MAKE_FILE)
    file(REMOVE_RECURSE "${MAKE_FILE}")
    file(WRITE "${MAKE_FILE}" "a file, not a folder\n")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env ${env_args} "${APP}" ${app_args}
    WORKING_DIRECTORY "${WORK_DIR}"
    RESULT_VARIABLE exit_code
    OUTPUT_VARIABLE output
    ERROR_VARIABLE output
)
message("${output}")

if(NOT exit_code STREQUAL EXPECT_EXIT)
    message(FATAL_ERROR "Exit code ${exit_code}, expected ${EXPECT_EXIT}")
endif()
foreach(name EXPECT1 EXPECT2 EXPECT3)
    if(DEFINED ${name} AND NOT output MATCHES "${${name}}")
        message(FATAL_ERROR "Output does not match ${name}: ${${name}}")
    endif()
endforeach()
if(DEFINED FORBID AND output MATCHES "${FORBID}")
    message(FATAL_ERROR "Output matches FORBID: ${FORBID}")
endif()
