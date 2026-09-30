#include "core/tick_scheduler.h"

#include <algorithm>
#include <cassert>

namespace aurora::core {

TickScheduler::TickScheduler(Duration interval, std::uint32_t maxCatchUpTicks)
    : m_interval(interval)
    , m_maxCatchUpTicks(std::max<std::uint32_t>(maxCatchUpTicks, 1))
{
    assert(interval > Duration::zero());
}

void TickScheduler::reset(TimePoint now)
{
    m_nextTick = now;
}

TickScheduler::Advance TickScheduler::advance(TimePoint now)
{
    if (now < m_nextTick) {
        return {};
    }

    // Deadlines in [m_nextTick, now].
    const auto due = static_cast<std::uint64_t>((now - m_nextTick) / m_interval) + 1;
    m_nextTick += m_interval * static_cast<Duration::rep>(due);

    if (due <= m_maxCatchUpTicks) {
        return {static_cast<std::uint32_t>(due), 0};
    }
    return {1, due - 1};
}

} // namespace aurora::core
