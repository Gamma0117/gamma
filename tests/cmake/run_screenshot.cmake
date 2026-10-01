# Runs the game in screenshot mode under Xvfb and checks the image.
#   cmake -DXVFB_RUN=<xvfb-run> -DAPP=<exe> -DCHECKER=<aurora_screenshot_check> -DOUTPUT=<file.png>
#         -DEXPECT=terrain|no_terrain [-DAPP_ARGS=<args;...>] -P run_screenshot.cmake
# Every step must work: the game exits with 0, writes a new PNG and the checker can read it. Only then does the
# expected verdict decide: EXPECT=terrain needs checker exit 0; EXPECT=no_terrain needs exit 2 ("no terrain"),
# so a crash, a missing file or an unreadable image never counts as a pass for the negative control.

file(REMOVE "${OUTPUT}")
execute_process(
    COMMAND "${XVFB_RUN}" -a -s "-screen 0 1280x720x24" "${APP}" --screenshot "${OUTPUT}" ${APP_ARGS}
    RESULT_VARIABLE app_exit
    OUTPUT_VARIABLE app_output
    ERROR_VARIABLE app_output
)
message("${app_output}")
if(NOT app_exit STREQUAL "0")
    message(FATAL_ERROR "The game exited with ${app_exit}, expected 0")
endif()
if(NOT EXISTS "${OUTPUT}")
    message(FATAL_ERROR "No screenshot was written to ${OUTPUT}")
endif()

execute_process(
    COMMAND "${CHECKER}" "${OUTPUT}"
    RESULT_VARIABLE check_exit
    OUTPUT_VARIABLE check_output
    ERROR_VARIABLE check_output
)
message("${check_output}")
if(EXPECT STREQUAL "terrain")
    if(NOT check_exit STREQUAL "0")
        message(FATAL_ERROR "Screenshot check failed with ${check_exit}")
    endif()
elseif(EXPECT STREQUAL "no_terrain")
    if(NOT check_exit STREQUAL "2" OR NOT check_output MATCHES "no terrain:")
        message(FATAL_ERROR "Expected the check to report no terrain (exit 2), got ${check_exit}")
    endif()
else()
    message(FATAL_ERROR "EXPECT must be terrain or no_terrain")
endif()
