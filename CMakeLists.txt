cmake_minimum_required(VERSION 3.25)

project(aurora VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(AURORA_BUILD_TESTS "Build unit tests" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(aurora_warnings)

add_subdirectory(engine)
add_subdirectory(app)

if(AURORA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
