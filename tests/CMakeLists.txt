find_package(Catch2 3 CONFIG REQUIRED)
include(Catch)

add_executable(aurora_tests
    core/sanity_tests.cpp
)
target_link_libraries(aurora_tests PRIVATE aurora::core Catch2::Catch2WithMain)
aurora_set_warnings(aurora_tests)
catch_discover_tests(aurora_tests)

# Headless smoke test: open the real window under Xvfb (Mesa llvmpipe) and exit after a few frames.
if(UNIX AND NOT APPLE)
    find_program(XVFB_RUN xvfb-run)
    if(XVFB_RUN)
        add_test(NAME app_smoke
            COMMAND "${XVFB_RUN}" -a -s "-screen 0 1280x720x24" $<TARGET_FILE:aurora> --frames 60)
        set_tests_properties(app_smoke PROPERTIES LABELS smoke TIMEOUT 60)
    endif()
endif()
