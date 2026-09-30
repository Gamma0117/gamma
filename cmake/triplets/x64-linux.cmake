# Same as vcpkg's built-in x64-linux, plus per-port overrides.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

set(VCPKG_CMAKE_SYSTEM_NAME Linux)

# The glad port generates a compatibility-profile loader by default; we target 4.5 core.
if(PORT STREQUAL "glad")
    set(GLAD_PROFILE "core")
endif()

# The tracy port only forwards its feature options, so pass the other client options directly.
# - TRACY_ENABLE: upstream defaults to OFF since 0.14.0 and the port at our baseline does not turn it on
#   (fixed in port-version 1), which would compile the profiler out.
# - Listen on localhost only (no Windows firewall prompt) and do not announce the client on the LAN.
if(PORT STREQUAL "tracy")
    list(APPEND VCPKG_CMAKE_CONFIGURE_OPTIONS
        "-DTRACY_ENABLE=ON"
        "-DTRACY_ONLY_LOCALHOST=ON"
        "-DTRACY_NO_BROADCAST=ON"
    )
endif()
