#pragma once

// Profiler hooks. Engine code uses only these macros, never Tracy directly, so the backend can be swapped
// or compiled out in one place. Without TRACY_ENABLE every macro expands to nothing.
//
// Zone and frame names must be string literals: Tracy keeps the pointer, not a copy.

#include <tracy/Tracy.hpp>

// Times the enclosing scope, named after the function.
#define AURORA_PROFILE_ZONE() ZoneScoped
// Times the enclosing scope under the given name.
#define AURORA_PROFILE_ZONE_N(name) ZoneScopedN(name)

// Marks the end of a main (client) frame.
#define AURORA_PROFILE_FRAME() FrameMark
// Marks the end of a frame in a secondary frame set, e.g. server ticks.
#define AURORA_PROFILE_FRAME_N(name) FrameMarkNamed(name)

// Sends a text message to the profiler timeline. `color` is 0xRRGGBB.
#define AURORA_PROFILE_MESSAGE(text, size, color) TracyMessageC(text, size, color)

// True while a profiler viewer is connected (on-demand mode collects data only then).
#define AURORA_PROFILER_IS_CONNECTED() (TracyIsConnected)

#ifdef TRACY_ENABLE
// Names the calling thread in the profiler (and in the OS, for debuggers).
#define AURORA_PROFILE_THREAD_NAME(name) tracy::SetThreadName(name)
#define AURORA_PROFILER_ENABLED 1
#else
#define AURORA_PROFILE_THREAD_NAME(name) static_cast<void>(0)
#define AURORA_PROFILER_ENABLED 0
#endif
