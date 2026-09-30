add_executable(aurora main.cpp)
target_link_libraries(aurora PRIVATE aurora::ui aurora::server)
aurora_set_warnings(aurora)

if(MSVC)
    set_property(DIRECTORY "${PROJECT_SOURCE_DIR}" PROPERTY VS_STARTUP_PROJECT aurora)
endif()
