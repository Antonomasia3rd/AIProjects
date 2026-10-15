#pragma once
#include <atomic>
#include <functional>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

namespace aip { namespace notes {
// Owner-thread only. Tasks receive copied inputs and must support cancellation
// with a finite deadline. A normal UI action reaps only tasks already done;
// global shutdown cancels every task before joining any of them.
class SignInWorkers {
public:
    using Task = std::function<void()>;
    using Starter = std::function<std::thread(Task)>;
private:
    struct Record {
        std::thread thread;
        std::shared_ptr<std::atomic<bool>> done = std::make_shared<std::atomic<bool>>(false);
        Task cancel;
    };
    std::vector<std::unique_ptr<Record>> records_;
    Starter starter_;
    bool stopping_ = false;
    static void Cancel(const Task& action) noexcept { try { if (action) action(); } catch (...) {} }
public:
    explicit SignInWorkers(Starter starter = {}) : starter_(std::move(starter)) {}
    SignInWorkers(const SignInWorkers&) = delete;
    SignInWorkers& operator=(const SignInWorkers&) = delete;
    ~SignInWorkers() { CancelAndJoin(); }
    std::size_t Size() const { return records_.size(); }
    std::size_t ReapCompleted() {
        std::size_t reaped = 0;
        for (auto current = records_.begin(); current != records_.end();) {
            if (!(*current)->done->load(std::memory_order_acquire)) { ++current; continue; }
            if ((*current)->thread.joinable()) (*current)->thread.join();
            current = records_.erase(current); ++reaped;
        }
        return reaped;
    }
    bool Start(Task cancel, Task task) {
        if (stopping_) { Cancel(cancel); return false; }
        try {
            ReapCompleted();
            auto record = std::make_unique<Record>(); record->cancel = cancel;
            const auto done = record->done;
            // Publish allocated storage before creating a joinable thread. A
            // vector-allocation failure cannot destroy a running thread record.
            records_.push_back(std::move(record));
            try {
                Task entry = [done, task = std::move(task)]() mutable {
                    try { if (task) task(); } catch (...) {}
                    // Destroy copied HTTP inputs before reporting completion.
                    task = {}; done->store(true, std::memory_order_release);
                };
                records_.back()->thread = starter_ ? starter_(std::move(entry)) : std::thread(std::move(entry));
                if (!records_.back()->thread.joinable()) { Cancel(records_.back()->cancel); records_.pop_back(); return false; }
            } catch (...) { Cancel(records_.back()->cancel); records_.pop_back(); return false; }
            return true;
        } catch (...) { Cancel(cancel); return false; }
    }
    void CancelAndJoin() {
        stopping_ = true;
        for (const auto& record : records_) Cancel(record->cancel);
        for (const auto& record : records_) if (record->thread.joinable()) record->thread.join();
        records_.clear(); stopping_ = false;
    }
};
} }
