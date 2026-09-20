#pragma once

// Portable port of capsblink's actual indicator pattern and session lifecycle.
// No Win32 dependency, device discovery, settings storage, or implicit action.
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

namespace aip { namespace caps {
constexpr std::uint16_t CapsMask = 4;
struct Indicators { std::uint16_t unitId = 0, flags = 0; };
inline bool operator==(Indicators left, Indicators right) { return left.unitId == right.unitId && left.flags == right.flags; }
inline Indicators Pattern(Indicators current, bool logicalOn)
{
    current.flags = static_cast<std::uint16_t>(logicalOn ? current.flags | CapsMask : current.flags ^ CapsMask);
    return current;
}
inline Indicators Restore(Indicators current, bool logicalOn)
{
    current.flags = static_cast<std::uint16_t>(logicalOn ? current.flags | CapsMask : current.flags & ~CapsMask);
    return current;
}
struct Config {
    bool actionsEnabled = false;
    std::wstring target = L"\\Device\\KeyboardClass0";
    int intervalMs = 500;
};
inline bool Normalize(Config& config, std::wstring& error)
{
    error.clear();
    const auto first = config.target.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) config.target.clear();
    else config.target = config.target.substr(first, config.target.find_last_not_of(L" \t\r\n") - first + 1);
    const std::wstring prefix = L"\\Device\\KeyboardClass";
    if (config.target.size() <= prefix.size() || config.target.size() > 1024)
    {
        error = L"Keyboard target must name a numbered KeyboardClass device.";
        return false;
    }
    for (std::size_t i = 0; i < prefix.size(); ++i)
    {
        const auto lower = [](wchar_t ch) { return ch >= L'A' && ch <= L'Z' ? ch + (L'a' - L'A') : ch; };
        if (lower(config.target[i]) != lower(prefix[i]))
        {
            error = L"Keyboard target must name a numbered KeyboardClass device.";
            return false;
        }
    }
    for (std::size_t i = prefix.size(); i < config.target.size(); ++i)
        if (config.target[i] < L'0' || config.target[i] > L'9')
        {
            error = L"Keyboard target must end in decimal digits.";
            return false;
        }
    config.target.replace(0, prefix.size(), prefix);
    if (config.intervalMs < 50 || config.intervalMs > 86400000)
    {
        error = L"Blink interval must be from 50 through 86400000 milliseconds.";
        return false;
    }
    return true;
}
enum class Phase { Disabled, Starting, Blinking, LogicalOn, Stopping, Stopped, Error };
inline const wchar_t* PhaseText(Phase phase)
{
    switch (phase)
    {
    case Phase::Disabled: return L"Actions disabled";
    case Phase::Starting: return L"Starting";
    case Phase::Blinking: return L"Blinking";
    case Phase::LogicalOn: return L"Caps Lock on";
    case Phase::Stopping: return L"Restoring indicator";
    case Phase::Stopped: return L"Stopped";
    default: return L"Error";
    }
}
struct Snapshot {
    Phase phase = Phase::Disabled;
    bool running = false, actionsEnabled = false, cleanupPending = false;
    bool logicalKnown = false, logicalOn = false;
    bool originalKnown = false, appliedKnown = false, restoredKnown = false;
    Indicators original, applied, restored;
    std::wstring target = L"\\Device\\KeyboardClass0";
    int intervalMs = 500;
    std::uint64_t iterations = 0, writes = 0;
    std::wstring error, cleanupError;
};
using Cancel = std::function<bool()>;

// All backend methods run on one worker, including acquisition and release of
// target ownership. Implementations must bound operations and honor Cancel.
// Close is also called after a failed/partial Open and must be idempotent.
class Backend {
public:
    virtual ~Backend() = default;
    virtual bool Open(const std::wstring& target, const Cancel& canceled, std::wstring& error) = 0;
    virtual bool ReadIndicators(Indicators& value, const Cancel& canceled, std::wstring& error) = 0;
    virtual bool ReadLogicalCaps(bool& enabled, const Cancel& canceled, std::wstring& error) = 0;
    virtual bool WriteIndicators(Indicators value, const Cancel& canceled, std::wstring& error) = 0;
    // True means canceled I/O still owns kernel-visible storage or could still
    // apply a write. The owner worker must not exit or release target ownership.
    virtual bool HasPendingOperation() const noexcept { return false; }
    virtual bool Close(std::wstring& error) = 0;
};

class Controller {
    std::unique_ptr<Backend> backend_;
    mutable std::mutex mutex_;
    std::mutex lifecycle_;
    std::condition_variable wake_;
    std::thread worker_;
    Config config_;
    Snapshot snapshot_;
    bool stop_ = false, running_ = false;
    std::uint64_t revision_ = 0;

    void Publish(const Snapshot& snapshot)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot_ = snapshot;
    }
    bool Canceled(const std::wstring& target) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return stop_ || !config_.actionsEnabled || config_.target != target;
    }
    static void AddError(std::wstring& target, const std::wstring& message)
    {
        if (!target.empty()) target += L"; ";
        target += message.empty() ? L"Keyboard operation failed" : message;
    }
    void Cleanup(bool opened, Snapshot& state)
    {
        state.phase = Phase::Stopping;
        Publish(state);
        const Cancel cleanupCanceled = [] { return false; };
        auto settlePending = [&] {
            while (backend_->HasPendingOperation())
            {
                state.cleanupPending = true;
                Publish(state);
                std::unique_lock<std::mutex> lock(mutex_);
                wake_.wait_for(lock, std::chrono::milliseconds(20));
            }
            state.cleanupPending = false;
        };
        settlePending();
        unsigned pendingRetries = 0;
        while (opened && state.originalKnown)
        {
            try
            {
                Indicators current = state.original;
                std::wstring error;
                const bool fresh = backend_->ReadIndicators(current, cleanupCanceled, error);
                if (backend_->HasPendingOperation())
                {
                    settlePending();
                    if (++pendingRetries >= 4) { AddError(state.cleanupError, L"Indicator restoration repeatedly exceeded its operation deadline"); break; }
                    continue;
                }
                if (!fresh) current = state.original;
                bool logical = (state.original.flags & CapsMask) != 0;
                std::wstring logicalError;
                const bool logicalKnown = backend_->ReadLogicalCaps(logical, cleanupCanceled, logicalError);
                if (!logicalKnown) logical = (state.original.flags & CapsMask) != 0;
                const auto desired = Restore(current, logical);
                // If a fresh read fails, restore the captured original state
                // rather than trusting the last blink write as current state.
                const bool needsWrite = !fresh || !(desired == current);
                const bool restored = !needsWrite || backend_->WriteIndicators(desired, cleanupCanceled, error);
                if (backend_->HasPendingOperation())
                {
                    settlePending();
                    if (++pendingRetries >= 4) { AddError(state.cleanupError, L"Indicator restoration repeatedly exceeded its operation deadline"); break; }
                    continue;
                }
                if (restored)
                {
                    state.restored = desired;
                    state.restoredKnown = true;
                    if (needsWrite) ++state.writes;
                }
                else AddError(state.cleanupError, error);
                if (!logicalKnown) AddError(state.cleanupError, L"Logical Caps Lock was unavailable; used the original Caps bit");
                if (!fresh) AddError(state.cleanupError, L"Current indicators were unavailable; used the original indicator snapshot");
            }
            catch (...)
            {
                AddError(state.cleanupError, L"Indicator restoration threw an exception");
                if (backend_->HasPendingOperation()) { settlePending(); if (++pendingRetries < 4) continue; }
            }
            break;
        }
        settlePending();
        try
        {
            std::wstring error;
            if (!backend_->Close(error)) AddError(state.cleanupError, error);
        }
        catch (...) { AddError(state.cleanupError, L"Device cleanup threw an exception"); }
    }
    void Run()
    {
        Snapshot state = GetSnapshot();
        for (;;)
        {
            Config session;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (stop_ || !config_.actionsEnabled) break;
                session = config_;
            }
            state = {};
            state.running = state.actionsEnabled = true;
            state.target = session.target;
            state.intervalMs = session.intervalMs;
            state.phase = Phase::Starting;
            Publish(state);
            const Cancel canceled = [this, target = session.target] { return Canceled(target); };
            bool opened = false;
            try
            {
                std::wstring error;
                opened = backend_->Open(session.target, canceled, error);
                if (!opened)
                {
                    if (!canceled()) state.error = error.empty() ? L"Could not open keyboard device" : error;
                }
                else
                {
                    bool first = true;
                    while (!canceled())
                    {
                        std::uint64_t revision;
                        {
                            std::lock_guard<std::mutex> lock(mutex_);
                            session.intervalMs = config_.intervalMs;
                            revision = revision_;
                        }
                        state.intervalMs = session.intervalMs;
                        Indicators current;
                        bool logical = false;
                        if (!backend_->ReadIndicators(current, canceled, error))
                        {
                            if (!canceled()) state.error = error.empty() ? L"Could not read keyboard indicators" : error;
                            break;
                        }
                        if (first)
                        {
                            state.original = current;
                            state.originalKnown = true;
                            first = false;
                        }
                        if (!backend_->ReadLogicalCaps(logical, canceled, error))
                        {
                            if (!canceled()) state.error = error.empty() ? L"Could not read logical Caps Lock" : error;
                            break;
                        }
                        if (canceled()) break;
                        const auto desired = Pattern(current, logical);
                        if (!(desired == current))
                        {
                            if (!backend_->WriteIndicators(desired, canceled, error))
                            {
                                if (!canceled()) state.error = error.empty() ? L"Could not update keyboard indicators" : error;
                                break;
                            }
                            ++state.writes;
                        }
                        state.logicalKnown = state.appliedKnown = true;
                        state.logicalOn = logical;
                        state.applied = desired;
                        ++state.iterations;
                        state.phase = logical ? Phase::LogicalOn : Phase::Blinking;
                        Publish(state);
                        std::unique_lock<std::mutex> lock(mutex_);
                        wake_.wait_for(lock, std::chrono::milliseconds(session.intervalMs), [&] { return stop_ || revision_ != revision; });
                    }
                }
            }
            catch (...) { state.error = L"Keyboard backend threw an exception"; }
            Cleanup(opened, state);
            if (!state.error.empty() || !state.cleanupError.empty()) break;
        }
        state.running = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state.actionsEnabled = config_.actionsEnabled;
            state.phase = !state.error.empty() || !state.cleanupError.empty() ? Phase::Error :
                (config_.actionsEnabled ? Phase::Stopped : Phase::Disabled);
            snapshot_ = state;
            running_ = false;
        }
    }
    void Join()
    {
        if (worker_.joinable()) worker_.join();
    }
public:
    explicit Controller(std::unique_ptr<Backend> backend) : backend_(std::move(backend)) {}
    ~Controller() { Stop(); }
    Controller(const Controller&) = delete;
    Controller& operator=(const Controller&) = delete;

    bool Start(Config config, std::wstring& error) { return Update(std::move(config), error); }
    bool Update(Config config, std::wstring& error)
    {
        if (!Normalize(config, error)) return false;
        if (config.actionsEnabled && !backend_) { error = L"Caps blink backend is unavailable"; return false; }
        std::lock_guard<std::mutex> lifecycle(lifecycle_);
        bool running, stopping;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            config_ = config;
            ++revision_;
            running = running_;
            stopping = stop_;
            if (!config.actionsEnabled) stop_ = true;
        }
        wake_.notify_all();
        if (!config.actionsEnabled)
        {
            Join();
            std::lock_guard<std::mutex> lock(mutex_);
            snapshot_.actionsEnabled = snapshot_.running = false;
            snapshot_.target = config.target;
            snapshot_.intervalMs = config.intervalMs;
            if (snapshot_.error.empty() && snapshot_.cleanupError.empty()) snapshot_.phase = Phase::Disabled;
            return true;
        }
        if (running && !stopping) return true;
        Join();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = false;
            running_ = true;
            snapshot_ = {};
            snapshot_.running = snapshot_.actionsEnabled = true;
            snapshot_.target = config.target;
            snapshot_.intervalMs = config.intervalMs;
            snapshot_.phase = Phase::Starting;
        }
        try { worker_ = std::thread([this] { Run(); }); }
        catch (...)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            running_ = snapshot_.running = false;
            snapshot_.phase = Phase::Error;
            error = snapshot_.error = L"Could not create Caps blink worker";
            return false;
        }
        return true;
    }
    void RequestStop()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
            if (running_) snapshot_.phase = Phase::Stopping;
        }
        wake_.notify_all();
    }
    void Stop()
    {
        std::lock_guard<std::mutex> lifecycle(lifecycle_);
        RequestStop();
        Join();
    }
    Snapshot GetSnapshot() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return snapshot_;
    }
};
} } // namespace aip::caps
