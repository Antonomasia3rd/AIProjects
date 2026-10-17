#pragma once
#include <atomic>
#include <cstdint>
#include <limits>

namespace aip { namespace idle {
// All callers share a budget, including rendering completion and the poller.
// A disabled interval never performs automatic maintenance. A changed interval
// is evaluated against the last claim, rather than an obsolete future deadline.
constexpr std::uint64_t Never = (std::numeric_limits<std::uint64_t>::max)();
inline bool Claim(std::atomic<std::uint64_t>& last, std::uint64_t now, std::uint64_t interval)
{
    if (!interval || now == Never) return false;
    auto observed = last.load(std::memory_order_relaxed);
    // A thread may sample the monotonic clock and pause before entering here.
    // Never let that older sample move another caller's claimed time backward.
    while (observed == Never || (now >= observed && now - observed >= interval)) {
        if (last.compare_exchange_weak(observed, now, std::memory_order_relaxed)) return true;
    }
    return false;
}
} }
