# Runs tools/latency_report.py on one record and checks its exit code and its whole output exactly (CTest's
# PASS_REGULAR_EXPRESSION alone would ignore the exit code).
#   cmake -DPYTHON=<python3> -DTOOL=<latency_report.py> -DRECORD=<file.csv> -DEXPECT_EXIT=<code>
#         -DEXPECT_OUTPUT=<file holding the exact standard output> -P run_latency_report.cmake
# Nothing may go to standard error. Line ends are compared without carriage returns (Windows).

foreach(var PYTHON TOOL RECORD EXPECT_EXIT EXPECT_OUTPUT)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "run_latency_report.cmake needs -D${var}=...")
    endif()
endforeach()

execute_process(
    COMMAND "${PYTHON}" "${TOOL}" "${RECORD}"
    RESULT_VARIABLE tool_exit
    OUTPUT_VARIABLE tool_output
    ERROR_VARIABLE tool_errors
)
file(READ "${EXPECT_OUTPUT}" expected_output)
string(REPLACE "\r" "" tool_output "${tool_output}")
string(REPLACE "\r" "" expected_output "${expected_output}")
message("exit ${tool_exit}\n${tool_output}")
if(tool_errors)
    message(FATAL_ERROR "latency_report.py wrote to standard error:\n${tool_errors}")
endif()
if(NOT tool_exit STREQUAL "${EXPECT_EXIT}")
    message(FATAL_ERROR "latency_report.py exited with ${tool_exit}, expected ${EXPECT_EXIT}")
endif()
if(NOT tool_output STREQUAL expected_output)
    message(FATAL_ERROR "latency_report.py printed something else; expected:\n${expected_output}")
endif()
