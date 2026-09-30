#include "core/timing_history.h"

#include <algorithm>
#include <cassert>

namespace aurora::core {

TimingHistory::TimingHistory(std::size_t capacity)
    : m_capacity(std::max<std::size_t>(capacity, 1))
{
    m_samples.reserve(m_capacity);
}

void TimingHistory::add(float milliseconds)
{
    if (m_samples.size() < m_capacity) {
        m_samples.push_back(milliseconds);
        return;
    }
    m_samples[m_next] = milliseconds;
    m_next = (m_next + 1) % m_capacity;
}

void TimingHistory::clear()
{
    m_samples.clear();
    m_next = 0;
}

float TimingHistory::at(std::size_t i) const
{
    assert(i < m_samples.size());
    return m_samples[(offset() + i) % m_samples.size()];
}

TimingSummary TimingHistory::summary() const
{
    if (m_samples.empty()) {
        return {};
    }

    TimingSummary result;
    result.latestMs = at(m_samples.size() - 1);
    result.minMs = m_samples.front();
    result.maxMs = m_samples.front();
    double total = 0.0;
    for (const float sample : m_samples) {
        result.minMs = std::min(result.minMs, sample);
        result.maxMs = std::max(result.maxMs, sample);
        total += sample;
    }
    result.averageMs = static_cast<float>(total / static_cast<double>(m_samples.size()));
    return result;
}

} // namespace aurora::core
