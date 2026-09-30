#pragma once

#include <string>
#include <string_view>

namespace aurora::core {

// Names the calling thread for log lines, the profiler and debuggers. Call once at the top of each thread body.
void setCurrentThreadName(std::string_view name);

// The name given by setCurrentThreadName, or a numeric thread id if the thread was never named.
const std::string& currentThreadName();

} // namespace aurora::core
