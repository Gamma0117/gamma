#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace aurora::core {

// Fixed pool of worker threads for heavy work: terrain generation, meshing, lighting, compression, file I/O.
// Jobs must not touch world data owned by the server thread; they work on copies and hand back new results
// (through async() futures or a result queue owned by the caller).
//
// Jobs run in FIFO order. Priorities (nearest section first) come with meshing in P0-5.
//
// Contract:
// - submit()/async() are thread-safe and may be called from jobs.
// - Once shutdown() has started, submit() returns false and async() returns an invalid future; the job is
//   dropped. Jobs queued before that still run to completion before shutdown() returns.
// - waitIdle() returns only when the queue is empty and no job is running.
class JobSystem {
public:
    // max(1, hardwareThreads - 2): leaves one hardware thread for the main (client) thread and one for the
    // server thread.
    static std::size_t workerCountFor(std::size_t hardwareThreads);
    // workerCountFor(std::thread::hardware_concurrency()), i.e. logical processors, not physical cores.
    static std::size_t defaultWorkerCount();

    // Starts `workerCount` threads (at least one).
    explicit JobSystem(std::size_t workerCount = defaultWorkerCount());
    ~JobSystem();

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    // Queues a fire-and-forget job. An exception escaping the job is logged, and the worker keeps running.
    bool submit(std::function<void()> job);

    // Queues a job and returns its result through a future; an exception thrown by the job is rethrown by get().
    template <typename F>
    auto async(F&& function) -> std::future<std::invoke_result_t<std::decay_t<F>&>>;

    // Blocks until the queue is empty and no job is running. Must not be called from a job (it would wait for
    // itself).
    void waitIdle();

    // Stops accepting jobs, finishes every job already queued, then joins the workers. Call from the owning
    // thread; safe to call more than once.
    void shutdown();

    std::size_t workerCount() const { return m_workerCount; }
    // Queued plus running jobs.
    std::size_t pendingJobs() const;

private:
    void workerLoop(std::size_t index);

    const std::size_t m_workerCount;
    std::vector<std::thread> m_workers;

    mutable std::mutex m_mutex; // Guards everything below.
    std::condition_variable m_jobAvailable;
    std::condition_variable m_idle;
    std::deque<std::function<void()>> m_queue;
    std::size_t m_running = 0;
    bool m_stopping = false;
};

template <typename F>
auto JobSystem::async(F&& function) -> std::future<std::invoke_result_t<std::decay_t<F>&>>
{
    using Result = std::invoke_result_t<std::decay_t<F>&>;

    // std::function needs a copyable callable, so the move-only packaged_task is shared.
    auto task = std::make_shared<std::packaged_task<Result()>>(std::forward<F>(function));
    std::future<Result> result = task->get_future();
    if (!submit([task] { (*task)(); })) {
        return {};
    }
    return result;
}

} // namespace aurora::core
