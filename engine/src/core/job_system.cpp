#include "core/job_system.h"

#include "core/log.h"
#include "core/profiler.h"
#include "core/thread.h"

#include <algorithm>
#include <exception>
#include <format>

namespace aurora::core {

namespace {

// Threads reserved outside the pool: main (client) and server.
constexpr std::size_t kReservedThreads = 2;

void runJob(std::function<void()>& job)
{
    AURORA_PROFILE_ZONE_N("Job");
    try {
        job();
    } catch (const std::exception& e) {
        logError("job", "Job threw an exception: {}", e.what());
    } catch (...) {
        logError("job", "Job threw an unknown exception");
    }
}

} // namespace

std::size_t JobSystem::workerCountFor(std::size_t hardwareThreads)
{
    return hardwareThreads > kReservedThreads + 1 ? hardwareThreads - kReservedThreads : 1;
}

std::size_t JobSystem::defaultWorkerCount()
{
    // hardware_concurrency() may return 0 when unknown; workerCountFor() then falls back to one worker.
    return workerCountFor(std::thread::hardware_concurrency());
}

JobSystem::JobSystem(std::size_t workerCount, StartHook startHook)
    : m_workerCount(std::max<std::size_t>(workerCount, 1))
{
    // The destructor does not run for a constructor that throws, and the members the workers wait on would be
    // destroyed under them. So on any failure here, stop and join the workers already started, then rethrow.
    try {
        m_workers.reserve(m_workerCount);
        for (std::size_t i = 0; i < m_workerCount; ++i) {
            if (startHook) {
                startHook(i);
            }
            m_workers.emplace_back(&JobSystem::workerLoop, this, i);
        }
        if (startHook) {
            startHook(m_workerCount);
        }
        logInfo("job", "Started {} worker thread(s)", m_workerCount);
    } catch (...) {
        const std::size_t started = m_workers.size();
        stopWorkers();
        logError("job", "Could not start the job system ({} of {} worker threads had started)", started,
                 m_workerCount);
        throw;
    }
}

JobSystem::~JobSystem()
{
    shutdown();
}

bool JobSystem::submit(std::function<void()> job)
{
    bool accepted = false;
    {
        std::lock_guard lock(m_mutex);
        if (!m_stopping) {
            m_queue.push_back(std::move(job));
            accepted = true;
        }
    }
    if (!accepted) {
        logWarn("job", "Job rejected: the job system is shutting down");
        return false;
    }
    m_jobAvailable.notify_one();
    return true;
}

void JobSystem::waitIdle()
{
    AURORA_PROFILE_ZONE();
    std::unique_lock lock(m_mutex);
    m_idle.wait(lock, [this] { return m_queue.empty() && m_running == 0; });
}

void JobSystem::shutdown()
{
    if (stopWorkers()) {
        logInfo("job", "Worker threads stopped");
    }
}

bool JobSystem::stopWorkers()
{
    {
        std::lock_guard lock(m_mutex);
        if (m_stopping && m_workers.empty()) {
            return false;
        }
        m_stopping = true;
    }
    m_jobAvailable.notify_all();

    for (std::thread& worker : m_workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    m_workers.clear();
    return true;
}

std::size_t JobSystem::pendingJobs() const
{
    std::lock_guard lock(m_mutex);
    return m_queue.size() + m_running;
}

void JobSystem::workerLoop(std::size_t index)
{
    setCurrentThreadName(std::format("Worker {}", index));

    for (;;) {
        std::function<void()> job;
        {
            std::unique_lock lock(m_mutex);
            m_jobAvailable.wait(lock, [this] { return m_stopping || !m_queue.empty(); });
            if (m_queue.empty()) {
                return; // Stopping, and every queued job has been taken.
            }
            job = std::move(m_queue.front());
            m_queue.pop_front();
            ++m_running;
        }

        runJob(job);
        // Release the job's captures before reporting idle, so waitIdle() callers see their side effects done.
        job = nullptr;

        {
            std::lock_guard lock(m_mutex);
            --m_running;
            if (m_running == 0 && m_queue.empty()) {
                m_idle.notify_all();
            }
        }
    }
}

} // namespace aurora::core
