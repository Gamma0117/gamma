# Same as vcpkg's built-in x64-linux, plus per-port overrides.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

set(VCPKG_CMAKE_SYSTEM_NAME Linux)

# The glad port generates a compatibility-profile loader by default; we target 4.5 core.
if(PORT STREQUAL "glad")
    set(GLAD_PROFILE "core")
endif()
