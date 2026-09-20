#include "../dependencies/hardware/caps_blink_engine.h"
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using namespace aip::caps;
using Clock = std::chrono::steady_clock;
int failures = 0;
void Check(bool passed, const char* name)
{
    std::cout << (passed ? "ok - " : "FAIL - ") << name << '\n';
    if (!passed) ++failures;
}
struct FakeState {
    std::mutex mutex;
    Indicators indicators{7, 3};
    bool logical = false, mapped = false, opened = false;
    bool failOpen = false, failClose = false, blockRead = false;
    bool lateWriteOnce = false, closedWhilePending = false;
    std::atomic<bool> pending{false};
    Indicators lateIndicators;
    int failReadAt = 0, failLogicalAt = 0, throwReadAt = 0;
    int opens = 0, closes = 0, reads = 0, logicalReads = 0, writes = 0;
    std::vector<std::wstring> events;
};
class FakeBackend final : public Backend {
    std::shared_ptr<FakeState> state_;
public:
    explicit FakeBackend(std::shared_ptr<FakeState> state) : state_(std::move(state)) {}
    bool Open(const std::wstring& target, const Cancel& canceled, std::wstring& error) override
    {
        if (canceled()) return false;
        std::lock_guard<std::mutex> lock(state_->mutex);
        ++state_->opens;
        state_->mapped = true;
        state_->events.push_back(L"open:" + target);
        if (state_->failOpen) { error = L"Fake open failed after creating a mapping"; return false; }
        state_->opened = true;
        return true;
    }
    bool ReadIndicators(Indicators& value, const Cancel& canceled, std::wstring& error) override
    {
        for (;;)
        {
            bool block;
            { std::lock_guard<std::mutex> lock(state_->mutex); block = state_->blockRead; }
            if (!block) break;
            if (canceled()) { error = L"Fake read canceled"; return false; }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (canceled()) return false;
        std::lock_guard<std::mutex> lock(state_->mutex);
        ++state_->reads;
        if (state_->reads == state_->throwReadAt) throw std::runtime_error("fake query exception");
        if (state_->reads == state_->failReadAt) { value = {65535, 65535}; error = L"Fake query failed"; return false; }
        value = state_->indicators;
        return true;
    }
    bool ReadLogicalCaps(bool& enabled, const Cancel& canceled, std::wstring& error) override
    {
        if (canceled()) return false;
        std::lock_guard<std::mutex> lock(state_->mutex);
        ++state_->logicalReads;
        if (state_->logicalReads == state_->failLogicalAt) { enabled = !(state_->indicators.flags & CapsMask); error = L"Fake logical read failed"; return false; }
        enabled = state_->logical;
        return true;
    }
    bool WriteIndicators(Indicators value, const Cancel& canceled, std::wstring& error) override
    {
        if (canceled()) return false;
        std::lock_guard<std::mutex> lock(state_->mutex);
        ++state_->writes;
        if (state_->lateWriteOnce)
        {
            state_->lateWriteOnce = false;
            state_->lateIndicators = value;
            state_->pending = true;
            state_->events.push_back(L"pending-write");
            error = L"Fake write timed out but is still pending";
            return false;
        }
        state_->indicators = value;
        state_->events.push_back(L"write");
        return true;
    }
    bool HasPendingOperation() const noexcept override { return state_->pending.load(); }
    bool Close(std::wstring& error) override
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        ++state_->closes;
        if (state_->pending)
        {
            state_->closedWhilePending = true;
            error = L"Fake ownership cannot close while I/O is pending";
            return false;
        }
        state_->opened = state_->mapped = false;
        state_->events.push_back(L"close");
        if (state_->failClose) { error = L"Fake cleanup failure"; return false; }
        return true;
    }
};
template<class Predicate> bool Wait(Predicate predicate)
{
    const auto deadline = Clock::now() + std::chrono::seconds(2);
    while (!predicate() && Clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return predicate();
}
Config Active(int interval = 86400000)
{
    Config config;
    config.actionsEnabled = true;
    config.intervalMs = interval;
    return config;
}
void TestPurePolicy()
{
    bool matches = true;
    for (unsigned flags = 0; flags <= 65535; ++flags)
        for (bool logical : {false, true})
        {
            Indicators input{37, static_cast<std::uint16_t>(flags)};
            const auto pattern = Pattern(input, logical);
            const auto restored = Restore(input, logical);
            matches = matches && pattern.unitId == 37 && restored.unitId == 37 &&
                pattern.flags == static_cast<std::uint16_t>(logical ? flags | 4 : flags ^ 4) &&
                (restored.flags & ~4u) == (flags & ~4u) && ((restored.flags & 4u) != 0) == logical;
        }
    Check(matches, "all 131072 flag/logical combinations preserve the legacy Caps-only pattern and other LEDs");
    Config config;
    std::wstring error;
    config.target = L"  \\device\\keyboardclass12  ";
    Check(Normalize(config, error) && config.target == L"\\Device\\KeyboardClass12" && !config.actionsEnabled,
        "configuration normalizes the legacy device prefix without enabling actions");
    config.intervalMs = 49;
    Check(!Normalize(config, error), "intervals below the legacy 50ms minimum are rejected");
    config.intervalMs = 86400001;
    Check(!Normalize(config, error), "intervals above the legacy one-day maximum are rejected");
    config.intervalMs = 500;
    config.target = L"\\Device\\KeyboardClassx";
    Check(!Normalize(config, error), "nondecimal keyboard targets are rejected");
}
void TestPreviewAndLifecycle()
{
    auto state = std::make_shared<FakeState>();
    Controller controller(std::make_unique<FakeBackend>(state));
    Config config;
    std::wstring error;
    Check(controller.Start(config, error) && controller.GetSnapshot().phase == Phase::Disabled &&
        state->opens == 0 && state->reads == 0 && state->logicalReads == 0 && state->writes == 0 && state->closes == 0,
        "disabled preview performs zero backend calls including logical-key reads");
    Controller noBackend(nullptr);
    Check(noBackend.Start(config, error), "disabled preview does not require a platform backend");
    config = Active();
    Check(controller.Start(config, error) && Wait([&] { return controller.GetSnapshot().iterations >= 1; }),
        "an explicitly enabled action starts the backend and first blink");
    const auto first = controller.GetSnapshot();
    Check(first.originalKnown && first.original == Indicators{7, 3} && first.appliedKnown && first.applied == Indicators{7, 7},
        "snapshot retains original indicators and the applied Caps-only write");
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->indicators.flags = 6; // Another actor changed a non-Caps indicator.
        state->logical = true;
    }
    config.intervalMs = 750;
    Check(controller.Update(config, error) && Wait([&] { return controller.GetSnapshot().iterations >= 2; }),
        "interval updates interrupt the old wait and retain the hardware session");
    Check(state->opens == 1 && controller.GetSnapshot().phase == Phase::LogicalOn && first.applied.flags == 7,
        "logical-on forces the Caps bit while copied snapshots remain immutable");
    const auto started = Clock::now();
    controller.Stop();
    const auto stopped = controller.GetSnapshot();
    Check(Clock::now() - started < std::chrono::seconds(1) && !stopped.running && stopped.restoredKnown &&
        stopped.restored == Indicators{7, 6} && !state->opened && !state->mapped && state->closes == 1,
        "stop restores current logical Caps and latest other LEDs then closes all fake resources");
    const int calls = state->opens;
    config.actionsEnabled = false;
    Check(controller.Update(config, error) && state->opens == calls && controller.GetSnapshot().phase == Phase::Disabled,
        "returning to preview cannot reopen or write the device");
}
void TestTargetAndFailures()
{
    std::wstring error;
    auto state = std::make_shared<FakeState>();
    Controller controller(std::make_unique<FakeBackend>(state));
    auto config = Active();
    controller.Start(config, error);
    Wait([&] { return controller.GetSnapshot().iterations >= 1; });
    config.target = L"\\Device\\KeyboardClass1";
    Check(controller.Update(config, error) && Wait([&] {
        const auto snapshot = controller.GetSnapshot();
        return snapshot.target == config.target && snapshot.iterations >= 1;
    }), "target changes start a new device session");
    controller.Stop();
    const auto firstClose = std::find(state->events.begin(), state->events.end(), L"close");
    const auto secondOpen = std::find(state->events.begin(), state->events.end(), L"open:\\Device\\KeyboardClass1");
    Check(firstClose < secondOpen && state->opens == 2 && state->closes == 2 && !state->mapped,
        "old-target restoration/close occurs before opening the replacement target");
    for (int failure = 0; failure < 4; ++failure)
    {
        auto broken = std::make_shared<FakeState>();
        broken->failOpen = failure == 0;
        broken->failReadAt = failure == 1 ? 2 : 0;
        broken->throwReadAt = failure == 2 ? 2 : 0;
        broken->failClose = failure == 3;
        Controller tested(std::make_unique<FakeBackend>(broken));
        tested.Start(Active(50), error);
        if (failure == 3)
        {
            Wait([&] { return tested.GetSnapshot().iterations >= 1; });
            tested.Stop();
        }
        else Wait([&] { return !tested.GetSnapshot().running; });
        tested.Stop();
        Check(tested.GetSnapshot().phase == Phase::Error && broken->closes == 1 && !broken->opened && !broken->mapped,
            "partial open, query failure, exception, or cleanup failure is reported and resources close");
    }
    auto fallback = std::make_shared<FakeState>();
    fallback->indicators = {8, 5};
    fallback->failReadAt = fallback->failLogicalAt = 2;
    Controller restoring(std::make_unique<FakeBackend>(fallback));
    restoring.Start(Active(), error);
    Wait([&] { return restoring.GetSnapshot().iterations >= 1; });
    restoring.Stop();
    Check(restoring.GetSnapshot().restoredKnown && fallback->indicators == Indicators{8, 5} &&
        !restoring.GetSnapshot().cleanupError.empty(),
        "failed cleanup reads use the original indicator snapshot and report the fallback");
}
void TestCancelAndStress()
{
    std::wstring error;
    auto blocked = std::make_shared<FakeState>();
    blocked->blockRead = true;
    Controller controller(std::make_unique<FakeBackend>(blocked));
    controller.Start(Active(), error);
    Wait([&] { std::lock_guard<std::mutex> lock(blocked->mutex); return blocked->opened; });
    auto started = Clock::now();
    controller.RequestStop();
    controller.Stop();
    Check(Clock::now() - started < std::chrono::seconds(1) && blocked->closes == 1 && blocked->writes == 0 && !blocked->mapped,
        "cancellation interrupts a blocked fake read and closes without uninitialized restoration writes");
    auto state = std::make_shared<FakeState>();
    Controller repeated(std::make_unique<FakeBackend>(state));
    std::atomic<bool> done{false};
    std::atomic<int> invalidSnapshots{0};
    std::thread reader([&] {
        while (!done)
        {
            const auto snapshot = repeated.GetSnapshot();
            if (snapshot.appliedKnown && !snapshot.originalKnown) ++invalidSnapshots;
            std::this_thread::yield();
        }
    });
    bool ok = true;
    for (int cycle = 0; cycle < 50; ++cycle)
    {
        ok = repeated.Start(Active(), error) && ok;
        ok = Wait([&] { return repeated.GetSnapshot().iterations > 0; }) && ok;
        repeated.RequestStop();
        repeated.Stop();
    }
    done = true;
    reader.join();
    Check(ok && invalidSnapshots == 0 && state->opens == 50 && state->closes == 50 && !state->mapped && !state->opened,
        "fifty start/stop cycles and concurrent status reads leave no fake handles, mappings, or torn snapshots");
}
void TestLateWriteOwnership()
{
    auto state = std::make_shared<FakeState>();
    state->lateWriteOnce = true;
    Controller controller(std::make_unique<FakeBackend>(state));
    std::wstring error;
    controller.Start(Active(), error);
    const bool pending = Wait([&] { return controller.GetSnapshot().cleanupPending; });
    controller.RequestStop();
    const auto stopping = controller.GetSnapshot();
    bool owned;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        owned = state->mapped && state->opened && state->closes == 0;
    }
    Check(pending && stopping.running && !stopping.restoredKnown && owned,
        "a pending late write retains the owner worker, mapping, and device without claiming restoration");
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->indicators = state->lateIndicators;
        state->events.push_back(L"late-complete");
        state->pending = false;
    }
    Check(Wait([&] { return !controller.GetSnapshot().running; }),
        "cleanup can finish only after the fake driver completes its outstanding write");
    controller.Stop();
    const auto completed = std::find(state->events.begin(), state->events.end(), L"late-complete");
    const auto restored = std::find(state->events.begin(), state->events.end(), L"write");
    const auto closed = std::find(state->events.begin(), state->events.end(), L"close");
    Check(completed < restored && restored < closed && !state->closedWhilePending &&
        controller.GetSnapshot().restoredKnown && state->indicators == Indicators{7, 3} && !state->mapped,
        "restoration happens after late completion and before releasing ownership");
}
}
int main()
{
    TestPurePolicy(); TestPreviewAndLifecycle(); TestTargetAndFailures(); TestCancelAndStress(); TestLateWriteOwnership();
    std::cout << (failures ? "Caps blink engine tests failed.\n" : "Caps blink engine tests passed.\n");
    return failures ? 1 : 0;
}
