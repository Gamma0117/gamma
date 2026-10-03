#include "client/section_latency.h"

#include "client/client_world.h"
#include "core/constants.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <numeric>
#include <utility>

namespace aurora::client {

SectionLatencyTracker::SectionLatencyTracker(std::size_t capacity, std::size_t recent, std::size_t window,
                                             std::size_t maxUnreported)
    : m_capacity(capacity == 0 ? 1 : capacity)
    , m_recentSize(recent == 0 ? 1 : recent)
    , m_windowSize(std::max(window, m_recentSize))
    , m_maxUnreported(maxUnreported == 0 ? 1 : maxUnreported)
{
}

void SectionLatencyTracker::onApplied(const world::ChunkUpdate& update, const ClientWorld& world)
{
    if (update.kind != world::ChunkUpdate::Kind::Changed) {
        return;
    }
    // Taken by the world, so it holds exactly this snapshot now.
    assert(world.snapshot(update.pos) == update.snapshot);
    for (std::int32_t section = 0; section < core::kSectionsPerChunk; ++section) {
        if ((update.changedSections >> section & 1u) == 0) {
            continue;
        }
        const SectionKey key{update.pos, section};
        m_pending.push_back({key, update.generation, *world.stamp(key), update.publishedAt});
        if (m_pending.size() > m_capacity) {
            m_pending.pop_front();
            ++m_overflowed;
        }
    }
}

void SectionLatencyTracker::onGpuTaken(const MeshKey& key, Clock::time_point now)
{
    std::erase_if(m_pending, [&](const Sample& sample) {
        if (sample.section != key.section || sample.generation != key.generation || key.stamp < sample.stamp) {
            return false;
        }
        complete(sample, now);
        return true;
    });
}

void SectionLatencyTracker::onMeshFailed(const MeshKey& key)
{
    std::erase_if(m_pending, [&](const Sample& sample) {
        const bool failed = sample.section == key.section && sample.generation == key.generation &&
                            key.stamp >= sample.stamp;
        m_failed += failed ? 1 : 0;
        return failed;
    });
}

void SectionLatencyTracker::update(const ClientWorld& world)
{
    std::erase_if(m_pending, [&](const Sample& sample) {
        const std::shared_ptr<const world::ChunkSnapshot> chunk = world.snapshot(sample.section.pos);
        const bool gone = !chunk || chunk->generation() != sample.generation || !world.isEligible(sample.section.pos);
        m_canceled += gone ? 1 : 0;
        return gone;
    });
}

SectionLatencyStats SectionLatencyTracker::stats() const
{
    if (m_windowDirty) {
        // At most `window` values, and only after something completed.
        std::vector<double> sorted(m_window.begin(), m_window.end());
        std::ranges::sort(sorted);
        // Nearest rank.
        const auto rank = [&](double fraction) {
            const auto index = static_cast<std::size_t>(std::ceil(fraction * static_cast<double>(sorted.size())));
            return sorted[std::clamp<std::size_t>(index, 1, sorted.size()) - 1];
        };
        m_windowP50Ms = rank(0.50);
        m_windowP95Ms = rank(0.95);
        m_windowMaxMs = sorted.back();
        m_windowDirty = false;
    }
    SectionLatencyStats stats;
    stats.completed = m_completed;
    stats.canceled = m_canceled;
    stats.failed = m_failed;
    stats.pending = m_pending.size();
    stats.overflowed = m_overflowed;
    stats.recentCount = std::min(m_window.size(), m_recentSize);
    if (stats.recentCount > 0) {
        const auto first = m_window.end() - static_cast<std::ptrdiff_t>(stats.recentCount);
        stats.recentMaxMs = *std::max_element(first, m_window.end());
        stats.recentMeanMs = std::accumulate(first, m_window.end(), 0.0) / static_cast<double>(stats.recentCount);
    }
    stats.windowCount = m_window.size();
    stats.windowEvicted = m_windowEvicted;
    stats.windowP50Ms = m_windowP50Ms;
    stats.windowP95Ms = m_windowP95Ms;
    stats.windowMaxMs = m_windowMaxMs;
    stats.runMaxMs = m_runMaxMs;
    stats.unreportedDropped = m_unreportedDropped;
    return stats;
}

std::vector<SectionLatencyTracker::Completion> SectionLatencyTracker::takeCompletions()
{
    std::vector<Completion> taken(m_unreported.begin(), m_unreported.end());
    m_unreported.clear();
    return taken;
}

void SectionLatencyTracker::complete(const Sample& sample, Clock::time_point now)
{
    const double milliseconds = std::chrono::duration<double, std::milli>(now - sample.publishedAt).count();
    ++m_completed;
    m_runMaxMs = m_completed == 1 ? milliseconds : std::max(m_runMaxMs, milliseconds);
    m_window.push_back(milliseconds);
    if (m_window.size() > m_windowSize) {
        m_window.pop_front();
        ++m_windowEvicted;
    }
    m_windowDirty = true;
    m_unreported.push_back({sample.section, sample.publishedAt, now, milliseconds});
    if (m_unreported.size() > m_maxUnreported) {
        m_unreported.pop_front();
        ++m_unreportedDropped;
    }
}

} // namespace aurora::client
