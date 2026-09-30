add_executable(aurora main.cpp)
target_link_libraries(aurora PRIVATE aurora::ui aurora::server aurora::data)
# Last-resort game folder for development runs (VS F5, ctest): the source tree's game/. UTF-8 literal, so a
# non-ASCII checkout path still works on Windows.
target_compile_definitions(aurora PRIVATE "AURORA_SOURCE_GAME_DIR=u8\"${PROJECT_SOURCE_DIR}/game\"")
aurora_set_warnings(aurora)

if(MSVC)
    set_property(DIRECTORY "${PROJECT_SOURCE_DIR}" PROPERTY VS_STARTUP_PROJECT aurora)
endif()
