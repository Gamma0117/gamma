# Runs one case of the GL effects check under Xvfb (see tools/effects_check.cpp).
#   cmake -DXVFB_RUN=<xvfb-run> -DTOOL=<aurora_effects_check> -DGAME_DIR=<folder> -DOUTPUT_DIR=<folder> -DCASE=<name>
#         [-DNEGATIVE=ON] -P run_effects.cmake
# A positive run must exit 0 with "check passed". A negative control must exit 2 with "check failed": the scene
# was set up and drawn and only the pixel check failed. Any other exit (no window, a scene that is not ready, the
# GL state not given back, a crash) fails both.

set(args --game-dir "${GAME_DIR}" --output-dir "${OUTPUT_DIR}" --case "${CASE}")
if(NEGATIVE)
    list(APPEND args --negative)
    set(expected_exit 2)
    set(expected_line "check failed")
else()
    set(expected_exit 0)
    set(expected_line "check passed")
endif()

execute_process(
    COMMAND "${XVFB_RUN}" -a -s "-screen 0 1280x720x24" "${TOOL}" ${args}
    RESULT_VARIABLE tool_exit
    OUTPUT_VARIABLE tool_output
    ERROR_VARIABLE tool_errors
)
message("${tool_output}")
if(tool_errors)
    message("stderr:\n${tool_errors}")
endif()
if(NOT tool_exit STREQUAL "${expected_exit}")
    message(FATAL_ERROR "aurora_effects_check exited with ${tool_exit}, expected ${expected_exit}")
endif()
if(NOT tool_output MATCHES "\n${expected_line}: ")
    message(FATAL_ERROR "aurora_effects_check did not report \"${expected_line}\"")
endif()
