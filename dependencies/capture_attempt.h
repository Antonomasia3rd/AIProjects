#pragma once

// Pure monotonic scheduling for a synchronous capture owned by one host.
// The caller serializes access. Reset invalidates timing without allowing a
// replacement capture to overlap an old capture that is still finishing.
#include <cstdint>
#include <mutex>
#include <utility>

namespace aip {
class CaptureAttemptSchedule {
public:
    struct Token {
        std::uint64_t id = 0, epoch = 0;
        explicit operator bool() const noexcept { return id != 0; }
    };
private:
    std::uint64_t epoch_ = 0, sequence_ = 0, active_ = 0, completed_ = 0;
    bool haveCompletion_ = false;
public:
    bool InFlight() const noexcept { return active_ != 0; }
    void Reset() noexcept { ++epoch_; haveCompletion_ = false; }
    Token Begin(std::uint64_t now, std::uint64_t retryMs) noexcept {
        if (InFlight()) return {};
        if (haveCompletion_ && now >= completed_ && now - completed_ < retryMs) return {};
        if (++sequence_ == 0) ++sequence_;
        active_ = sequence_;
        return {active_, epoch_};
    }
    void Complete(Token token, std::uint64_t now) noexcept {
        if (!token || active_ != token.id) return;
        active_ = 0;
        if (token.epoch == epoch_) { completed_ = now; haveCompletion_ = true; }
    }
};

inline std::uint64_t CaptureAttemptRetryMs(int refreshMs, int startupRefreshMs, bool startupSettling) noexcept {
    if (startupSettling && startupRefreshMs > 0) return static_cast<std::uint64_t>(startupRefreshMs);
    if (refreshMs > 0) return static_cast<std::uint64_t>(refreshMs);
    // Refresh=0 means capture once after SUCCESS. An unavailable initial
    // surface can recover later, with bounded failed-attempt retries.
    return 1000;
}

// Captures may release the host's state mutex while synchronous I/O runs.
// Scope cleanup reacquires it before releasing the in-flight token, including
// when capture/file/diagnostic code throws. Clock is a nonthrowing monotonic
// functor; no callbacks allocate or touch system capture APIs here.
template<class Clock> class CaptureAttemptScope {
    CaptureAttemptSchedule& schedule_;
    CaptureAttemptSchedule::Token token_;
    std::unique_lock<std::mutex>& lock_;
    Clock clock_;
public:
    CaptureAttemptScope(CaptureAttemptSchedule& schedule, CaptureAttemptSchedule::Token token,
        std::unique_lock<std::mutex>& lock, Clock clock)
        : schedule_(schedule), token_(token), lock_(lock), clock_(std::move(clock)) {}
    ~CaptureAttemptScope() {
        if (!lock_.owns_lock()) lock_.lock();
        schedule_.Complete(token_, clock_());
    }
    CaptureAttemptScope(const CaptureAttemptScope&) = delete;
    CaptureAttemptScope& operator=(const CaptureAttemptScope&) = delete;
};
} // namespace aip
