# Runs the game in screenshot mode under Xvfb and checks the image.
#   cmake -DXVFB_RUN=<xvfb-run> -DAPP=<exe> -DCHECKER=<aurora_screenshot_check> -DOUTPUT=<file.png>
#         -DEXPECT=terrain|no_terrain -DEXPECT_PITCH=<degrees> [-DAPP_ARGS=<args;...>] -P run_screenshot.cmake
# Every step must work: the game exits with 0, writes a new PNG, the camera of the capture is at the player's eyes
# and the checker can read the image. Only then does the expected verdict decide: EXPECT=terrain needs checker
# exit 0; EXPECT=no_terrain needs exit 2 ("no terrain"), so a crash, a missing file, a wrong camera or an
# unreadable image never counts as a pass for the negative control.
#
# The camera: the game logs its game folder, where the client spawned the player and the camera of the capture.
# The expected eyes are the spawn plus eye_height from that folder's player/movement.json (read here, not from the
# game), looking at EXPECT_PITCH. The log prints 2 decimals (the pitch 1), so numbers are compared in 1/10000 units
# with a tolerance of 0.011 (two roundings of 0.005).

# "65.62" or "1.6200000000000001" -> 656200 or 16200: a plain decimal in 1/10000 units, cut after 4 decimals.
function(to_ten_thousandths text out)
    if(NOT text MATCHES "^(-?)([0-9]+)(\\.([0-9]*))?$")
        message(FATAL_ERROR "Not a plain decimal number: '${text}'")
    endif()
    set(sign "${CMAKE_MATCH_1}")
    set(whole "${CMAKE_MATCH_2}")
    string(SUBSTRING "${CMAKE_MATCH_4}0000" 0 4 fraction)
    math(EXPR value "${whole} * 10000 + ${fraction}")
    if(sign STREQUAL "-")
        math(EXPR value "-${value}")
    endif()
    set(${out} ${value} PARENT_SCOPE)
endfunction()

# Fails unless |actual - expected| <= 110 (both in 1/10000 units).
function(require_close what actual expected)
    math(EXPR difference "${actual} - (${expected})")
    if(difference LESS -110 OR difference GREATER 110)
        message(FATAL_ERROR "${what}: ${actual} instead of ${expected} (1/10000 blocks or degrees)")
    endif()
endfunction()

function(check_camera output)
    if(NOT output MATCHES "Game folder: ([^\n]+)")
        message(FATAL_ERROR "The game did not log its game folder")
    endif()
    string(REGEX REPLACE " \\(from --game-dir\\)$" "" game_folder "${CMAKE_MATCH_1}")
    set(movement_file "${game_folder}/data/aurora/player/movement.json")
    if(NOT EXISTS "${movement_file}")
        message(FATAL_ERROR "No movement settings at ${movement_file}")
    endif()
    file(READ "${movement_file}" movement)
    string(JSON eye_text GET "${movement}" eye_height)
    to_ten_thousandths("${eye_text}" eye)

    set(number "(-?[0-9]+\\.[0-9]+)")
    if(NOT output MATCHES "\\[client\\][^\n]*Player spawned at \\(${number}, ${number}, ${number}\\)")
        message(FATAL_ERROR "The game did not log where the client spawned the player")
    endif()
    to_ten_thousandths("${CMAKE_MATCH_1}" spawn_x)
    to_ten_thousandths("${CMAKE_MATCH_2}" spawn_y)
    to_ten_thousandths("${CMAKE_MATCH_3}" spawn_z)
    if(NOT output MATCHES "Screenshot saved to [^\n]*camera at ${number}, ${number}, ${number}, pitch ${number}\\)")
        message(FATAL_ERROR "The game did not log the camera of the screenshot")
    endif()
    to_ten_thousandths("${CMAKE_MATCH_1}" camera_x)
    to_ten_thousandths("${CMAKE_MATCH_2}" camera_y)
    to_ten_thousandths("${CMAKE_MATCH_3}" camera_z)
    to_ten_thousandths("${CMAKE_MATCH_4}" camera_pitch)
    to_ten_thousandths("${EXPECT_PITCH}" expected_pitch)

    require_close("Camera x (spawn x)" ${camera_x} ${spawn_x})
    require_close("Camera y (spawn y + eye_height ${eye_text})" ${camera_y} "${spawn_y} + ${eye}")
    require_close("Camera z (spawn z)" ${camera_z} ${spawn_z})
    require_close("Camera pitch" ${camera_pitch} ${expected_pitch})
    message("camera ok: at the player's eyes (eye_height ${eye_text}), pitch ${EXPECT_PITCH}")
endfunction()

if(NOT DEFINED EXPECT_PITCH)
    message(FATAL_ERROR "EXPECT_PITCH is required")
endif()

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
check_camera("${app_output}")

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
