#pragma once

// Reusable asynchronous source cache. The host owns UI/rendering; fetchers own
// their I/O deadline and must observe cancellation. At most one fetch runs and
// one latest request waits per source. Old-profile results are never published.
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>

namespace aip { namespace content {
template<class Result> class AsyncSource {
public:
    using Clock = std::chrono::steady_clock;
    using Cancel = std::function<bool()>;
    using Fetch = std::function<Result(const Cancel&)>;
    using Wake = std::function<void()>;
private:
    struct Control {
        std::atomic<bool> stop{false};
        std::atomic<std::uint64_t> revision{0};
    };
    std::shared_ptr<Control> control_ = std::make_shared<Control>();
    mutable std::mutex mutex_;
    std::mutex stopMutex_;
    std::condition_variable changed_;
    std::thread worker_;
    bool pending_ = false, busy_ = false;
    std::wstring key_, resultKey_;
    std::optional<Result> result_;
    std::string error_;
    Fetch fetch_;
    Wake wake_;
    Clock::time_point next_{};
    std::chrono::milliseconds interval_{0};

    void Run() noexcept {
        for (;;) {
            Fetch fetch;
            Wake wake;
            std::wstring key;
            std::uint64_t revision;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                changed_.wait(lock, [&] { return control_->stop.load() || pending_; });
                if (control_->stop.load()) return;
                fetch = fetch_; wake = wake_; key = key_;
                revision = control_->revision.load();
                pending_ = false; busy_ = true;
            }
            const auto control = control_;
            Cancel canceled = [control, revision] { return control->stop.load() || control->revision.load() != revision; };
            std::optional<Result> result;
            std::string failure;
            try { result = fetch(canceled); }
            catch (const std::exception& ex) { failure = ex.what(); }
            catch (...) { failure = "Source failed with an unknown exception."; }
            bool publish = false;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                busy_ = false;
                if (!canceled()) {
                    result_ = std::move(result); resultKey_ = key; error_ = std::move(failure);
                    publish = true;
                }
            }
            changed_.notify_all();
            if (publish && wake) { try { wake(); } catch (...) {} }
        }
    }
public:
    AsyncSource() = default;
    ~AsyncSource() { Stop(); }
    AsyncSource(const AsyncSource&) = delete;
    AsyncSource& operator=(const AsyncSource&) = delete;

    bool Request(const std::wstring& key, std::chrono::milliseconds interval, Fetch fetch, Wake wake = {}, bool force = false) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (control_->stop.load() || !fetch) return false;
        if (interval < std::chrono::milliseconds(1)) interval = std::chrono::milliseconds(1);
        const bool changed = key != key_ || interval != interval_;
        if (!changed && !force && (busy_ || pending_ || Clock::now() < next_)) return true;
        key_ = key; interval_ = interval; fetch_ = std::move(fetch); wake_ = std::move(wake);
        next_ = Clock::now() + interval;
        ++control_->revision;
        pending_ = true;
        try { if (!worker_.joinable()) worker_ = std::thread([this] { Run(); }); }
        catch (...) { pending_ = false; resultKey_ = key; result_.reset(); next_ = {}; error_ = "Could not start the source worker."; return false; }
        changed_.notify_all();
        return true;
    }
    // A same-key refresh may serve the last published cache until completion.
    // False means no value is available; error distinguishes terminal failure
    // from loading/superseded state. Result is not modified on a false return.
    bool Read(const std::wstring& key, Result& result, std::string& error) const {
        std::lock_guard<std::mutex> lock(mutex_);
        error.clear();
        if (key_ != key || resultKey_ != key) return false;
        error = error_;
        if (!result_) return false;
        result = *result_; return true;
    }
    // Explicit one-shot callers can wait, while normal UI/poll code only reads
    // the cache. The timeout never detaches the worker or invalidates its data.
    // True means terminal completion (possibly failure), NOT a successful Read.
    bool Wait(const std::wstring& key, std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        return changed_.wait_for(lock, timeout, [&] {
            return control_->stop.load() || key_ != key || (!busy_ && !pending_ && resultKey_ == key);
        }) && !control_->stop.load() && key_ == key && resultKey_ == key;
    }
    void CancelPending() {
        std::lock_guard<std::mutex> lock(mutex_);
        ++control_->revision; pending_ = false; key_.clear(); resultKey_.clear(); result_.reset(); error_.clear();
        changed_.notify_all();
    }
    void Stop() {
        // Stop/destruction belongs to the external owner, never a Fetch/Wake
        // callback on this worker (which cannot join itself). Serialize Stops,
        // and exclude Request's worker creation before inspecting/joining it.
        std::lock_guard<std::mutex> stopLock(stopMutex_);
        { std::lock_guard<std::mutex> lock(mutex_); control_->stop = true; }
        changed_.notify_all();
        if (worker_.joinable()) worker_.join();
    }
};
} }
