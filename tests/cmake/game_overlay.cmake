# Builds a test's game folder: STAGING emptied, BASE copied into it, then every file of OVERLAY copied on top.
#   include(game_overlay.cmake)
#   aurora_overlay_game(<base folder> <overlay folder or ""> <staging folder>)
# The overlay files always replace what is there, whatever the timestamps: file(COPY) skips a file whose
# destination has the same timestamp, which a fresh checkout often gives a fixture and the base file it replaces,
# so each one is copied with file(COPY_FILE), which compares nothing.
function(aurora_overlay_game base overlay staging)
    file(REMOVE_RECURSE "${staging}")
    file(MAKE_DIRECTORY "${staging}")
    file(COPY "${base}/" DESTINATION "${staging}")
    if(overlay STREQUAL "")
        return()
    endif()
    file(GLOB_RECURSE overlay_files LIST_DIRECTORIES false RELATIVE "${overlay}" "${overlay}/*")
    foreach(relative IN LISTS overlay_files)
        get_filename_component(parent "${staging}/${relative}" DIRECTORY)
        file(MAKE_DIRECTORY "${parent}")
        file(COPY_FILE "${overlay}/${relative}" "${staging}/${relative}" RESULT copy_result)
        if(NOT copy_result EQUAL 0)
            message(FATAL_ERROR "Cannot copy ${overlay}/${relative} into ${staging}: ${copy_result}")
        endif()
    endforeach()
endfunction()
