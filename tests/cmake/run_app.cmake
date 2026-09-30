# Runs the game once and checks its exit code and log output.
#   cmake -DAPP=<exe> -DGAME_DIR=<folder> -DEXPECT_EXIT=<code> [-DNO_DISPLAY=ON]
#         [-DEXPECT1=<regex>] [-DEXPECT2=<regex>] [-DEXPECT3=<regex>] [-DFORBID=<regex>] -P run_app.cmake
# NO_DISPLAY removes DISPLAY/WAYLAND_DISPLAY, so reaching window creation would fail with a GLFW error.

set(env_args)
if(NO_DISPLAY)
    list(APPEND env_args --unset=DISPLAY --unset=WAYLAND_DISPLAY)
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env ${env_args} "${APP}" --game-dir "${GAME_DIR}" --frames 1
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
