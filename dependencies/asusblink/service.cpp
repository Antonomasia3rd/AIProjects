#include "service.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace aip { namespace asus {
namespace {
bool ValidOptions(const Options& options, std::wstring& error) {
    if (!Validate(options.configuration, error)) return false;
    if (options.pollMs < 1 || options.pollMs > 1000 || options.requestTimeoutMs < 100 || options.requestTimeoutMs > 30000) {
        error = L"Invalid service poll interval or hardware request timeout."; return false;
    }
    return true;
}
std::uint64_t Now() { return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()); }
}
struct Service::Impl {
    struct State {
        std::mutex mutex;
        std::condition_variable changed;
        Options options;
        Snapshot snapshot;
        std::atomic<bool> stop{false}, paused{false};
        std::atomic<std::uint64_t> generation{1};
        bool done = false;
    };
    mutable std::mutex lifecycle, statePointer;
    std::shared_ptr<State> state;
    std::thread worker;
    static void Publish(const std::shared_ptr<State>& state, Snapshot snapshot) {
        std::lock_guard<std::mutex> lock(state->mutex); snapshot.revision = state->snapshot.revision + 1; state->snapshot = std::move(snapshot);
    }
    static void Run(std::shared_ptr<State> state) noexcept {
        Options options; Snapshot snapshot; PatternEngine pattern;
        std::unique_ptr<Backend> backend;
        std::uint64_t generation = 0, diskBytes = 0;
        try {
            while (!state->stop) {
                if (state->generation != generation) {
                    backend.reset();
                    { std::lock_guard<std::mutex> lock(state->mutex); options = state->options; generation = state->generation; }
                    std::wstring error;
                    if (!pattern.Reset(options.configuration, error)) throw std::runtime_error("Invalid ASUS configuration.");
                    snapshot = {}; snapshot.running = true; snapshot.hardwareEnabled = options.hardwareEnabled;
                }
                const auto cancelled = [state, generation] { return state->stop.load() || state->generation.load() != generation; };
                const auto ensureBackend = [&] {
                    if (backend) return;
                    if (options.backendFactory) backend = options.backendFactory();
#ifndef AIP_ASUS_NO_NATIVE_BACKEND
                    else backend = CreateNativeBackend();
#endif
                    if (!backend) throw std::runtime_error("No ASUS backend is available.");
                };
                try {
                    bool needsDisk = std::any_of(options.configuration.events.begin(), options.configuration.events.end(), [](const Event& e) { return !e.states.empty() && e.hddActivity; });
                    if (options.readDiskActivity && needsDisk && !state->paused) {
                        ensureBackend();
                        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(options.requestTimeoutMs);
                        std::wstring error;
                        if (!backend->ReadDiskBytes(diskBytes, [cancelled, deadline] { return cancelled() || std::chrono::steady_clock::now() >= deadline; }, error)) snapshot.error = error;
                    }
                    auto actions = pattern.Step(Now(), state->paused, diskBytes);
                    snapshot.pattern = pattern.Read();
                    for (const auto& action : actions) {
                        if (cancelled() || state->paused) break;
                        if (!options.hardwareEnabled) continue;
                        ensureBackend();
                        bool applied = false; std::wstring error;
                        for (int attempt = 0; attempt <= options.configuration.retries && !cancelled(); ++attempt) {
                            auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(options.requestTimeoutMs);
                            applied = backend->Apply(action.device, action.state, [cancelled, deadline] { return cancelled() || std::chrono::steady_clock::now() >= deadline; }, error);
                            if (applied || attempt == options.configuration.retries) break;
                            std::unique_lock<std::mutex> lock(state->mutex);
                            state->changed.wait_for(lock, std::chrono::milliseconds(500), cancelled);
                        }
                        if (applied) {
                            if (action.device == Device::Mic) snapshot.appliedMic = action.state; else snapshot.appliedKeyboard = action.state;
                            snapshot.error.clear();
                        } else if (!cancelled()) {
                            snapshot.error = error.empty() ? L"ASUS firmware rejected the requested state." : error;
                            switch (options.configuration.errorAction) {
                            case ErrorAction::Pause: state->paused = true; break;
                            case ErrorAction::Stop: state->stop = true; break;
                            case ErrorAction::Fault: snapshot.faulted = true; state->stop = true; break;
                            default: break;
                            }
                        }
                    }
                } catch (const std::exception&) {
                    snapshot.error = L"ASUS backend failed; no further hardware action was attempted in this tick.";
                    backend.reset();
                    if (options.configuration.errorAction == ErrorAction::Pause) state->paused = true;
                    else if (options.configuration.errorAction == ErrorAction::Stop || options.configuration.errorAction == ErrorAction::Fault) {
                        snapshot.faulted = options.configuration.errorAction == ErrorAction::Fault; state->stop = true;
                    }
                }
                snapshot.status = state->paused ? L"Paused" : options.hardwareEnabled ? (snapshot.pattern.complete ? L"Completed" : L"Hardware control enabled") : L"Preview: hardware control disabled";
                if (state->generation == generation) Publish(state, snapshot);
                std::unique_lock<std::mutex> lock(state->mutex);
                state->changed.wait_for(lock, std::chrono::milliseconds(options.pollMs), cancelled);
            }
        } catch (...) { snapshot.error = L"ASUS source worker failed."; snapshot.faulted = true; }
        backend.reset(); snapshot.running = false; snapshot.status = snapshot.faulted ? L"Source faulted" : L"Stopped";
        Publish(state, snapshot);
        { std::lock_guard<std::mutex> lock(state->mutex); state->done = true; }
        state->changed.notify_all();
    }
};
Service::Service() : impl_(new Impl()) {}
Service::~Service() { if (!Stop(2000)) { std::lock_guard<std::mutex> lock(impl_->lifecycle); if (impl_->worker.joinable()) impl_->worker.detach(); } }
bool Service::Start(const Options& options, std::wstring& error) {
    if (!ValidOptions(options, error)) return false;
    std::lock_guard<std::mutex> lock(impl_->lifecycle);
    if (impl_->worker.joinable()) { error = L"ASUS source is already running."; return false; }
    auto state = std::make_shared<Impl::State>(); state->options = options; state->snapshot.running = true;
    try { impl_->worker = std::thread(&Impl::Run, state); } catch (...) { error = L"Could not start ASUS source worker."; return false; }
    { std::lock_guard<std::mutex> pointerLock(impl_->statePointer); impl_->state = std::move(state); }
    return true;
}
bool Service::Reload(const Options& options, std::wstring& error) {
    if (!ValidOptions(options, error)) return false;
    std::lock_guard<std::mutex> lock(impl_->lifecycle); auto state = impl_->state;
    if (!state || state->stop) { error = L"ASUS source is not running."; return false; }
    { std::lock_guard<std::mutex> stateLock(state->mutex); state->options = options; ++state->generation; }
    state->changed.notify_all(); return true;
}
void Service::Pause(bool paused) { std::lock_guard<std::mutex> lock(impl_->statePointer); if (impl_->state) { impl_->state->paused = paused; impl_->state->changed.notify_all(); } }
bool Service::Stop(unsigned timeoutMs) {
    std::lock_guard<std::mutex> lock(impl_->lifecycle); if (!impl_->worker.joinable()) return true;
    auto state = impl_->state; state->stop = true; state->changed.notify_all();
    std::unique_lock<std::mutex> stateLock(state->mutex);
    if (!state->changed.wait_for(stateLock, std::chrono::milliseconds(std::min(timeoutMs, 30000u)), [&] { return state->done; })) return false;
    stateLock.unlock(); impl_->worker.join(); return true;
}
Snapshot Service::Read() const {
    std::shared_ptr<Impl::State> state; { std::lock_guard<std::mutex> lock(impl_->statePointer); state = impl_->state; }
    if (!state) return {}; std::lock_guard<std::mutex> lock(state->mutex); return state->snapshot;
}
} }
