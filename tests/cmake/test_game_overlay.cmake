# Regression test for game_overlay.cmake: an overlay file must replace the base file even when both have exactly
# the same timestamp and size (a fresh checkout can give a fixture and the base file that), and base files the
# overlay does not name must stay. The preconditions are checked first, so the test cannot pass without them.
#   cmake -DWORK=<scratch folder> [-DOVERLAY_MODULE=<file>] -P test_game_overlay.cmake
# POSIX only: `touch -d` gives both files the same time and `stat` shows it to the nanosecond.
if(NOT DEFINED WORK)
    message(FATAL_ERROR "WORK is required")
endif()
if(NOT DEFINED OVERLAY_MODULE)
    set(OVERLAY_MODULE "${CMAKE_CURRENT_LIST_DIR}/game_overlay.cmake")
endif()
include("${OVERLAY_MODULE}")

set(base "${WORK}/base")
set(overlay "${WORK}/overlay")
set(staging "${WORK}/staging")
set(replaced "data/aurora/player/movement.json")
file(REMOVE_RECURSE "${WORK}")
file(WRITE "${base}/${replaced}" "{ \"gravity\": 32.0 }\n")
file(WRITE "${overlay}/${replaced}" "{ \"gravity\": 0.00 }\n")
file(WRITE "${base}/data/aurora/blocks/stone.json" "base only\n")
file(WRITE "${overlay}/data/aurora/player/interaction.json" "overlay only\n")

execute_process(COMMAND touch -d "2026-01-01 00:00:00" "${base}/${replaced}" "${overlay}/${replaced}"
                RESULT_VARIABLE touch_result)
execute_process(COMMAND stat -c %y "${base}/${replaced}" OUTPUT_VARIABLE base_time RESULT_VARIABLE stat_base)
execute_process(COMMAND stat -c %y "${overlay}/${replaced}" OUTPUT_VARIABLE overlay_time RESULT_VARIABLE stat_overlay)
file(SIZE "${base}/${replaced}" base_size)
file(SIZE "${overlay}/${replaced}" overlay_size)
file(SHA256 "${base}/${replaced}" base_hash)
file(SHA256 "${overlay}/${replaced}" overlay_hash)
if(NOT touch_result EQUAL 0 OR NOT stat_base EQUAL 0 OR NOT stat_overlay EQUAL 0 OR NOT base_time STREQUAL overlay_time
   OR NOT base_size EQUAL overlay_size OR base_hash STREQUAL overlay_hash)
    message(FATAL_ERROR "Precondition not met: the two files need the same time and size and different content "
                        "(times '${base_time}' '${overlay_time}', sizes ${base_size} ${overlay_size})")
endif()
string(STRIP "${base_time}" base_time)
message("base and overlay ${replaced}: both ${base_size} bytes, both modified ${base_time}, contents differ")

aurora_overlay_game("${base}" "${overlay}" "${staging}")

set(failures)
file(SHA256 "${staging}/${replaced}" staged_hash)
if(NOT staged_hash STREQUAL overlay_hash)
    list(APPEND failures "${replaced} is not the overlay's (the base file was kept)")
endif()
if(NOT EXISTS "${staging}/data/aurora/blocks/stone.json")
    list(APPEND failures "the base file the overlay does not name is gone")
endif()
if(NOT EXISTS "${staging}/data/aurora/player/interaction.json")
    list(APPEND failures "the file only the overlay has is missing")
endif()
if(failures)
    list(JOIN failures "; " text)
    message(FATAL_ERROR "Overlay wrong: ${text}")
endif()
message("overlay ok: the overlay file replaced the base file of the same time and size")
