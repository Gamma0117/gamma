#include "client/mesh_scheduler.h"

#include "client/client_world.h"
#include "core/constants.h"
#include "core/job_system.h"
#include "core/log.h"
#include "core/profiler.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace aurora::client {

namespace {

std::string describe(const SectionKey& key)
{
    return std::format("chunk ({}, {}) section {}", key.pos.x, key.pos.z, key.section);
}

} // namespace

MeshScheduler::MeshScheduler(core::JobSystem& jobs, MeshFunction meshFunction, std::size_t maxInFlight)
    : m_jobs(jobs)
    , m_meshFunction(std::move(meshFunction))
    , m_maxInFlight(maxInFlight == 0 ? 1 : maxInFlight)
{
}

// Dropping the futures does not wait: the jobs own their inputs and finish on their own.
MeshScheduler::~MeshScheduler() = default;

void MeshScheduler::update(ClientWorld& world, world::ChunkPos cameraChunk, std::int32_t cameraSection)
{
    AURORA_PROFILE_ZONE_N("Mesh scheduler");
    for (const world::ChunkPos pos : world.takeChangedChunks()) {
        forgetChunk(pos);
        if (world.isEligible(pos)) {
            addChunk(world, pos);
        }
    }
    collect(world);
    submit(world, cameraChunk, cameraSection);
}

std::vector<ReadyMesh> MeshScheduler::takeReady()
{
    for (const ReadyMesh& ready : m_ready) {
        setState(ready.key, ready.mesh.empty() ? State::Empty : State::Meshed);
    }
    return std::exchange(m_ready, {});
}

MeshSchedulerStats MeshScheduler::stats() const
{
    MeshSchedulerStats stats;
    for (const auto& [key, record] : m_records) {
        switch (record.state) {
        case State::Waiting:
            ++stats.waiting;
            break;
        case State::Meshed:
            ++stats.meshed;
            break;
        case State::Empty:
            ++stats.empty;
            break;
        case State::Failed:
            ++stats.failed;
            break;
        case State::InFlight:
        case State::Ready:
            break; // Counted from the lists below, which include stale entries.
        }
    }
    stats.inFlight = m_inFlight.size();
    stats.ready = m_ready.size();
    return stats;
}

bool MeshScheduler::isSettled() const
{
    if (!m_inFlight.empty() || !m_ready.empty()) {
        return false;
    }
    return std::ranges::all_of(m_records, [](const auto& item) {
        return item.second.state == State::Meshed || item.second.state == State::Empty;
    });
}

void MeshScheduler::forgetChunk(world::ChunkPos pos)
{
    for (std::int32_t section = 0; section < core::kSectionsPerChunk; ++section) {
        m_records.erase(SectionKey{pos, section});
    }
    std::erase_if(m_ready, [pos](const ReadyMesh& ready) { return ready.key.section.pos == pos; });
}

void MeshScheduler::addChunk(const ClientWorld& world, world::ChunkPos pos)
{
    const std::shared_ptr<const world::ChunkSnapshot> snapshot = world.snapshot(pos);
    for (std::int32_t section = 0; section < core::kSectionsPerChunk; ++section) {
        if (snapshot->section(section) == nullptr) {
            continue; // All air: no faces to make.
        }
        const SectionKey sectionKey{pos, section};
        if (const std::optional<MeshKey> key = world.currentKey(sectionKey)) {
            m_records[sectionKey] = Record{*key, State::Waiting};
        }
    }
}

void MeshScheduler::collect(const ClientWorld& world)
{
    for (auto job = m_inFlight.begin(); job != m_inFlight.end();) {
        if (job->result.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            ++job;
            continue;
        }
        const MeshKey key = job->key;
        const auto record = m_records.find(key.section);
        const bool current = record != m_records.end() && record->second.key == key && world.isCurrent(key);
        try {
            MeshData mesh = job->result.get();
            if (current) {
                // Empty results go to the renderer as well: a section that had faces before must lose its old mesh.
                record->second.state = State::Ready;
                m_ready.push_back({key, std::move(mesh)});
            }
        } catch (const std::exception& e) {
            if (current) {
                record->second.state = State::Failed;
                core::logError("client", "Meshing {} failed: {}", describe(key.section), e.what());
            }
        } catch (...) {
            if (current) {
                record->second.state = State::Failed;
                core::logError("client", "Meshing {} failed: unknown exception", describe(key.section));
            }
        }
        job = m_inFlight.erase(job);
    }
}

void MeshScheduler::submit(const ClientWorld& world, world::ChunkPos cameraChunk, std::int32_t cameraSection)
{
    if (m_inFlight.size() >= m_maxInFlight) {
        return;
    }
    std::vector<SectionKey> waiting;
    for (const auto& [key, record] : m_records) {
        if (record.state == State::Waiting) {
            waiting.push_back(key);
        }
    }
    const std::size_t count = std::min(waiting.size(), m_maxInFlight - m_inFlight.size());
    const auto nearer = [&](const SectionKey& a, const SectionKey& b) {
        const auto rank = [&](const SectionKey& key) {
            const std::int64_t vertical = key.section - static_cast<std::int64_t>(cameraSection);
            return std::pair{world::chunkDistance(key.pos, cameraChunk), vertical < 0 ? -vertical : vertical};
        };
        return rank(a) < rank(b);
    };
    std::partial_sort(waiting.begin(), waiting.begin() + static_cast<std::ptrdiff_t>(count), waiting.end(), nearer);

    for (std::size_t i = 0; i < count; ++i) {
        Record& record = m_records.at(waiting[i]);
        auto chunks = world.neighbourhood(waiting[i].pos);
        if (!chunks) {
            continue; // Not eligible any more; the next change notice forgets it.
        }
        MeshInput input{std::move(*chunks), waiting[i].section};
        // The job owns its input and a copy of the function; nothing here is captured.
        std::future<MeshData> result =
            m_jobs.async([function = m_meshFunction, input = std::move(input)] { return function(input); });
        if (!result.valid()) {
            record.state = State::Failed;
            core::logError("client", "Meshing {} failed: the job system is not accepting jobs",
                           describe(waiting[i]));
            continue;
        }
        record.state = State::InFlight;
        m_inFlight.push_back({record.key, std::move(result)});
    }
}

void MeshScheduler::setState(const MeshKey& key, State state)
{
    const auto record = m_records.find(key.section);
    if (record != m_records.end() && record->second.key == key) {
        record->second.state = state;
    }
}

} // namespace aurora::client
