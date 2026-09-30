#include "core/job_system.h"
#include "core/log.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>

using namespace std::chrono_literals;
using aurora::core::JobSystem;

namespace {

using Clock = std::chrono::steady_clock;

// Silences expected error and warning lines (throwing jobs, rejected jobs) for one test.
class ScopedLogLevel {
public:
    explicit ScopedLogLevel(aurora::core::LogLevel level)
        : m_previous(aurora::core::Log::minLevel())
    {
        aurora::core::Log::setMinLevel(level);
    }
    ~ScopedLogLevel() { aurora::core::Log::setMinLevel(m_previous); }

    ScopedLogLevel(const ScopedLogLevel&) = delete;
    ScopedLogLevel& operator=(const ScopedLogLevel&) = delete;

private:
    aurora::core::LogLevel m_previous;
};

// Blocks a worker until `release` is set. Gives up after 10 s so a failed test can never hang the run.
void holdUntil(const std::atomic<bool>& release)
{
    const Clock::time_point deadline = Clock::now() + 10s;
    while (!release.load() && Clock::now() < deadline) {
        std::this_thread::sleep_for(1ms);
    }
}

bool waitFor(const std::atomic<bool>& flag)
{
    const Clock::time_point deadline = Clock::now() + 10s;
    while (!flag.load() && Clock::now() < deadline) {
        std::this_thread::sleep_for(1ms);
    }
    return flag.load();
}

} // namespace

// Shared state is declared before the JobSystem in every test, so it outlives the workers even if a REQUIRE
// ends the test early.

TEST_CASE("Worker count leaves two hardware threads free", "[core][jobs]")
{
    CHECK(JobSystem::workerCountFor(0) == 1); // hardware_concurrency() unknown
    CHECK(JobSystem::workerCountFor(1) == 1);
    CHECK(JobSystem::workerCountFor(2) == 1);
    CHECK(JobSystem::workerCountFor(3) == 1);
    CHECK(JobSystem::workerCountFor(4) == 2);
    CHECK(JobSystem::workerCountFor(8) == 6);
    CHECK(JobSystem::workerCountFor(16) == 14);
    CHECK(JobSystem::defaultWorkerCount() >= 1);

    JobSystem jobs(0);
    CHECK(jobs.workerCount() == 1);
}

TEST_CASE("Every submitted job runs", "[core][jobs]")
{
    constexpr int kJobs = 10000;
    std::atomic<int> counter{0};
    JobSystem jobs(4);

    for (int i = 0; i < kJobs; ++i) {
        REQUIRE(jobs.submit([&counter] { counter.fetch_add(1); }));
    }
    jobs.waitIdle();

    CHECK(counter.load() == kJobs);
    CHECK(jobs.pendingJobs() == 0);
}

TEST_CASE("async returns results and rethrows exceptions", "[core][jobs]")
{
    JobSystem jobs(2);

    auto answer = jobs.async([] { return 6 * 7; });
    auto text = jobs.async([] { return std::string("aurora"); });
    auto moveOnly = jobs.async([value = std::make_unique<int>(5)] { return *value; });
    auto failing = jobs.async([]() -> int { throw std::runtime_error("expected by the test"); });

    REQUIRE(answer.valid());
    REQUIRE(text.valid());
    REQUIRE(moveOnly.valid());
    REQUIRE(failing.valid());
    CHECK(answer.get() == 42);
    CHECK(text.get() == "aurora");
    CHECK(moveOnly.get() == 5);
    CHECK_THROWS_AS(failing.get(), std::runtime_error);
}

TEST_CASE("Jobs run on worker threads, not the caller", "[core][jobs]")
{
    constexpr std::size_t kWorkers = 3;
    std::mutex mutex;
    std::set<std::thread::id> threadIds;
    JobSystem jobs(kWorkers);

    for (int i = 0; i < 1000; ++i) {
        REQUIRE(jobs.submit([&mutex, &threadIds] {
            std::lock_guard lock(mutex);
            threadIds.insert(std::this_thread::get_id());
        }));
    }
    jobs.waitIdle();

    std::lock_guard lock(mutex);
    CHECK(threadIds.count(std::this_thread::get_id()) == 0);
    CHECK(!threadIds.empty());
    CHECK(threadIds.size() <= kWorkers);
}

TEST_CASE("waitIdle waits for a job that is still running", "[core][jobs]")
{
    std::atomic<bool> started{false};
    std::atomic<bool> release{false};
    std::atomic<bool> finished{false};
    std::atomic<bool> idleReturned{false};
    JobSystem jobs(1);

    REQUIRE(jobs.submit([&] {
        started = true;
        holdUntil(release);
        finished = true;
    }));
    REQUIRE(waitFor(started));

    // The queue is empty now, but the job is still running.
    CHECK(jobs.pendingJobs() == 1);

    std::thread waiter([&] {
        jobs.waitIdle();
        idleReturned = true;
    });
    std::this_thread::sleep_for(200ms);
    CHECK_FALSE(idleReturned.load());

    release = true;
    waiter.join();
    CHECK(idleReturned.load());
    CHECK(finished.load());
    CHECK(jobs.pendingJobs() == 0);
}

TEST_CASE("A throwing job does not stop its worker", "[core][jobs]")
{
    ScopedLogLevel quiet(aurora::core::LogLevel::Off);
    std::atomic<int> counter{0};
    JobSystem jobs(1);

    REQUIRE(jobs.submit([] { throw std::runtime_error("expected by the test"); }));
    REQUIRE(jobs.submit([&counter] { counter.fetch_add(1); }));
    jobs.waitIdle();

    CHECK(counter.load() == 1);
}

TEST_CASE("Shutdown finishes queued jobs and rejects new ones", "[core][jobs]")
{
    constexpr int kQueuedJobs = 100;
    ScopedLogLevel quiet(aurora::core::LogLevel::Off);
    std::atomic<bool> release{false};
    std::atomic<int> counter{0};
    JobSystem jobs(1);

    // Keep the only worker busy so the jobs below are still queued when shutdown starts.
    REQUIRE(jobs.submit([&release] { holdUntil(release); }));
    for (int i = 0; i < kQueuedJobs; ++i) {
        REQUIRE(jobs.submit([&counter] { counter.fetch_add(1); }));
    }

    std::thread stopper([&jobs] { jobs.shutdown(); });

    // Submissions are refused as soon as shutdown has begun. Jobs accepted before that are empty.
    bool rejected = false;
    const Clock::time_point deadline = Clock::now() + 10s;
    while (!rejected && Clock::now() < deadline) {
        rejected = !jobs.submit([] {});
        std::this_thread::yield();
    }
    CHECK(rejected);
    CHECK_FALSE(jobs.async([] { return 1; }).valid());
    CHECK(counter.load() == 0); // Still queued behind the held job.

    release = true;
    stopper.join();

    CHECK(counter.load() == kQueuedJobs);
    CHECK(jobs.pendingJobs() == 0);
    CHECK_FALSE(jobs.submit([] {}));
    jobs.shutdown(); // Second call is a no-op.
}
