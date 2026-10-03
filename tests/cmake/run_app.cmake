# Runs the game once and checks its exit code and log output.
#   cmake -DAPP=<exe> -DEXPECT_EXIT=<code> [-DGAME_DIR=<folder>] [-DBASE_GAME=<folder> -DSTAGING=<folder>]
#         [-DWORK_DIR=<folder>] [-DNO_DISPLAY=ON] [-DBROKEN_LINK=<path>] [-DMAKE_FILE=<path>]
#         [-DEXPECT1=<regex>] [-DEXPECT2=<regex>] [-DEXPECT3=<regex>] [-DFORBID=<regex>]
#         -P run_app.cmake
# GAME_DIR is passed as --game-dir; without it the game searches from WORK_DIR (its working directory).
# BASE_GAME builds the game folder in STAGING instead, for a fixture that holds only its broken files: STAGING is
# emptied, BASE_GAME is copied into it, then every file of GAME_DIR (if given) on top whatever its timestamp
# (game_overlay.cmake), and the game runs with --game-dir STAGING.
# Each test needs its own STAGING (tests run in parallel); the source folders are never written.
# NO_DISPLAY removes DISPLAY/WAYLAND_DISPLAY, so reaching window creation would fail with a GLFW error.
# BROKEN_LINK is created fresh as a symbolic link to a missing target before the run (its parents are created),
# replacing whatever is there, a copied folder included. MAKE_FILE is created as a small regular file before the run
# (its parents are created), e.g. in place of a folder. Both come after the staging copies, so nothing copied
# afterwards can repair them.

set(env_args)
if(NO_DISPLAY)
    list(APPEND env_args --unset=DISPLAY --unset=WAYLAND_DISPLAY)
endif()
set(app_args --frames 1)
if(DEFINED BASE_GAME)
    if(NOT DEFINED STAGING)
        message(FATAL_ERROR "BASE_GAME needs STAGING")
    endif()
    include("${CMAKE_CURRENT_LIST_DIR}/game_overlay.cmake")
    if(DEFINED GAME_DIR)
        aurora_overlay_game("${BASE_GAME}" "${GAME_DIR}" "${STAGING}")
    else()
        aurora_overlay_game("${BASE_GAME}" "" "${STAGING}")
    endif()
    list(APPEND app_args --game-dir "${STAGING}")
elseif(DEFINED GAME_DIR)
    list(APPEND app_args --game-dir "${GAME_DIR}")
endif()
if(NOT DEFINED WORK_DIR)
    set(WORK_DIR "${CMAKE_CURRENT_BINARY_DIR}")
endif()

if(DEFINED BROKEN_LINK)
    get_filename_component(link_parent "${BROKEN_LINK}" DIRECTORY)
    file(REMOVE_RECURSE "${BROKEN_LINK}")
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
