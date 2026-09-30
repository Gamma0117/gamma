#include "core/thread.h"

#include "core/profiler.h"

#include <sstream>
#include <thread>

namespace aurora::core {

namespace {

std::string& threadNameStorage()
{
    thread_local std::string name;
    return name;
}

} // namespace

void setCurrentThreadName(std::string_view name)
{
    std::string& stored = threadNameStorage();
    stored.assign(name);
    AURORA_PROFILE_THREAD_NAME(stored.c_str());
}

const std::string& currentThreadName()
{
    std::string& stored = threadNameStorage();
    if (stored.empty()) {
        std::ostringstream id;
        id << "T" << std::this_thread::get_id();
        stored = id.str();
    }
    return stored;
}

} // namespace aurora::core
