#pragma once

#include <cstddef>
#include <vector>

namespace aurora::core {

struct TimingSummary {
    float latestMs = 0.0f;
    float averageMs = 0.0f;
    float minMs = 0.0f;
    float maxMs = 0.0f;
};

// Ring buffer of the most recent duration samples in milliseconds, for frame and tick statistics and graphs.
// Not thread-safe: guard it when another thread reads it.
class TimingHistory {
public:
    // `capacity` must be at least 1.
    explicit TimingHistory(std::size_t capacity);

    void add(float milliseconds);
    void clear();

    std::size_t size() const { return m_samples.size(); }
    std::size_t capacity() const { return m_capacity; }
    bool empty() const { return m_samples.empty(); }

    // i = 0 is the oldest sample.
    float at(std::size_t i) const;
    // All zero when empty.
    TimingSummary summary() const;

    // Raw storage for ImGui::PlotLines(values = data(), count = size(), offset = offset()).
    const float* data() const { return m_samples.data(); }
    // Storage index of the oldest sample.
    std::size_t offset() const { return m_samples.size() < m_capacity ? 0 : m_next; }

private:
    std::size_t m_capacity;
    std::size_t m_next = 0; // Storage index the next sample overwrites once full.
    std::vector<float> m_samples;
};

} // namespace aurora::core
