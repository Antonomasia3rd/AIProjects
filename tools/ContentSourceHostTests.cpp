// No OS integration: callbacks are synthetic and every slow callback observes
// cancellation and has its own deadline, including on an assertion failure.
#include "../dependencies/content_source_host.h"
#include <iostream>
#include <stdexcept>
#include <future>

using Source = aip::content::AsyncSource<int>;
using namespace std::chrono_literals;
static int checks = 0;
static void Require(bool result, const char* message) {
    ++checks; if (!result) throw std::runtime_error(message);
}
template<class Predicate> static bool Until(Predicate ready) {
    auto end = Source::Clock::now() + 2s;
    while (!ready() && Source::Clock::now() < end) std::this_thread::sleep_for(1ms);
    return ready();
}

static void TestRefreshReadContract() {
    std::atomic<bool> started{false}, release{false};
    Source source; // Destroy/join before any state captured by callbacks.
    int result = 0; std::string error;
    Require(source.Request(L"cached", 1h, [](const Source::Cancel&) { return 10; }) &&
        source.Wait(L"cached", 2s), "cache fixture initializes");
    source.Request(L"cached", 1h, [&](const Source::Cancel& cancel) {
        started = true;
        const auto deadline = Source::Clock::now() + 2s;
        while (!release && !cancel() && Source::Clock::now() < deadline) std::this_thread::sleep_for(1ms);
        return 20;
    }, {}, true);
    Require(Until([&] { return started.load(); }), "forced refresh starts");
    Require(source.Read(L"cached", result, error) && result == 10 && error.empty(),
        "same-key refresh retains last published cache while pending");
    Require(!source.Wait(L"cached", 1ms), "old same-key cache does not satisfy fresh completion wait");
    release = true;
    Require(source.Wait(L"cached", 2s) && source.Read(L"cached", result, error) && result == 20,
        "completed refresh replaces old same-key cache");
    source.Request(L"cached", 1h, [](const Source::Cancel&) -> int { throw std::runtime_error("refresh failed"); }, {}, true);
    Require(source.Wait(L"cached", 2s), "Wait reports terminal failure as completed work");
    Require(!source.Read(L"cached", result, error) && error == "refresh failed",
        "failed refresh reports error instead of returning stale success");
}

static void TestPendingCoalescing() {
    std::atomic<bool> started{false}, oldCanceled{false};
    std::atomic<int> middleCalls{0}, latestCalls{0}, notifications{0};
    std::mutex finishGate;
    Source source;
    source.Request(L"old", 1h, [&](const Source::Cancel& cancel) {
        started = true;
        const auto deadline = Source::Clock::now() + 2s;
        while (!cancel() && Source::Clock::now() < deadline) std::this_thread::sleep_for(1ms);
        oldCanceled = cancel();
        // The test holds this only while issuing two nonblocking Requests.
        std::lock_guard<std::mutex> gate(finishGate);
        return 1;
    }, [&] { ++notifications; });
    Require(Until([&] { return started.load(); }), "coalescing fixture starts");
    {
        std::lock_guard<std::mutex> gate(finishGate);
        Require(source.Request(L"middle", 1h, [&](const Source::Cancel&) { ++middleCalls; return 2; }),
            "intermediate pending request accepted");
        Require(source.Request(L"latest", 1h, [&](const Source::Cancel&) { ++latestCalls; return 3; }, [&] { ++notifications; }),
            "latest pending request accepted");
    }
    int result = 0; std::string error;
    Require(source.Wait(L"latest", 2s) && source.Read(L"latest", result, error) && result == 3,
        "latest pending result is published");
    Require(oldCanceled && middleCalls == 0 && latestCalls == 1, "one pending slot coalesces intermediate work");
    Require(!source.Read(L"old", result, error) && !source.Read(L"middle", result, error),
        "superseded profile keys have no readable results");
    Require(Until([&] { return notifications == 1; }), "superseded results do not send completion wakes");
}

static void TestWaitSupersessionAndWakeFailure() {
    std::atomic<bool> started{false}, waiterStarted{false};
    std::atomic<int> wakes{0};
    Source source;
    source.Request(L"old", 1h, [&](const Source::Cancel& cancel) {
        started = true;
        const auto deadline = Source::Clock::now() + 2s;
        while (!cancel() && Source::Clock::now() < deadline) std::this_thread::sleep_for(1ms);
        return 1;
    });
    Require(Until([&] { return started.load(); }), "wait-supersession fixture starts");
    auto waiting = std::async(std::launch::async, [&] {
        waiterStarted = true;
        return source.Wait(L"old", 200ms);
    });
    Require(Until([&] { return waiterStarted.load(); }), "old-key waiter starts");
    source.Request(L"new", 1h, [](const Source::Cancel&) { return 2; }, [&] {
        ++wakes; throw std::runtime_error("synthetic wake failure");
    });
    Require(!waiting.get(), "key supersession cannot be reported as old-key completion");
    Require(source.Wait(L"new", 2s) && Until([&] { return wakes == 1; }), "throwing wake callback is contained");
    source.Request(L"after", 1h, [](const Source::Cancel&) { return 3; });
    int result = 0; std::string error;
    Require(source.Wait(L"after", 2s) && source.Read(L"after", result, error) && result == 3,
        "worker remains usable after wake callback throws");
}

static void TestConcurrentStop() {
    for (int round = 0; round < 32; ++round) {
        std::atomic<int> ready{0};
        std::atomic<bool> go{false};
        Source source;
        auto startTogether = [&] {
            ++ready;
            const auto deadline = Source::Clock::now() + 2s;
            while (!go && Source::Clock::now() < deadline) std::this_thread::yield();
        };
        auto requests = std::async(std::launch::async, [&] {
            startTogether();
            for (int i = 0; i < 128; ++i)
                source.Request(L"race", 1h, [](const Source::Cancel&) { return 5; }, {}, true);
        });
        auto stop1 = std::async(std::launch::async, [&] { startTogether(); source.Stop(); });
        auto stop2 = std::async(std::launch::async, [&] { startTogether(); source.Stop(); });
        Require(Until([&] { return ready == 3; }), "concurrent lifecycle fixtures ready");
        go = true;
        requests.get(); stop1.get(); stop2.get();
        Require(!source.Request(L"closed", 1ms, [](const Source::Cancel&) { return 0; }),
            "concurrent Stop calls serialize with worker creation and reject later requests");
    }
}
int main() {
    try {
        std::atomic<int> calls{0}, wakes{0};
        std::atomic<bool> started{false}, canceled{false};
        Source source;
        auto value = [&](const Source::Cancel&) { ++calls; return 42; };
        Require(source.Request(L"one", 1h, value, [&] { ++wakes; }), "first request starts");
        Require(source.Wait(L"one", 2s), "completed result wakes bounded wait");
        int result = 0; std::string error;
        Require(source.Read(L"one", result, error) && result == 42 && error.empty(), "result snapshot has expected value");
        for (int i = 0; i < 10000; ++i) Require(source.Request(L"one", 1h, value), "cached request accepted");
        Require(calls == 1, "polling cache does not repeatedly fetch");
        Require(Until([&] { return wakes == 1; }), "one completion notification");
        source.Request(L"one", 1h, value, {}, true);
        Require(source.Wait(L"one", 2s) && calls == 2, "forced refresh runs once");
        source.Request(L"one", 1s, value);
        Require(source.Wait(L"one", 2s) && calls == 3, "shorter interval invalidates old long deadline");

        source.Request(L"old", 1h, [&](const Source::Cancel& stop) {
            started = true;
            auto end = Source::Clock::now() + 2s;
            while (!stop() && Source::Clock::now() < end) std::this_thread::sleep_for(1ms);
            canceled = stop(); return 1;
        });
        Require(Until([&] { return started.load(); }), "slow fixture starts");
        Require(!source.Wait(L"old", 1ms), "bounded wait returns while fetch pending");
        source.Request(L"latest", 1h, [](const Source::Cancel&) { return 99; });
        Require(source.Wait(L"latest", 2s), "latest request completes after cancellation");
        Require(canceled && source.Read(L"latest", result, error) && result == 99, "old profile cannot overwrite latest result");
        Require(!source.Read(L"old", result, error), "old profile cache is inaccessible");
        source.Request(L"failure", 1h, [](const Source::Cancel&) -> int { throw std::runtime_error("fixture failure"); });
        Require(source.Wait(L"failure", 2s), "fetch exception reaches terminal state");
        Require(!source.Read(L"failure", result, error) && error == "fixture failure", "failure is data rather than process termination");
        source.Request(L"recovery", 1h, value);
        Require(source.Wait(L"recovery", 2s) && source.Read(L"recovery", result, error), "same worker recovers from callback failure");
        source.CancelPending();
        Require(!source.Read(L"recovery", result, error), "disabling clears cached data");
        source.Request(L"recovery", 1h, value);
        Require(source.Wait(L"recovery", 2s), "reenabling starts a fresh fetch");
        started = false;
        source.Request(L"stop", 1h, [&](const Source::Cancel& stop) {
            started = true;
            auto end = Source::Clock::now() + 2s;
            while (!stop() && Source::Clock::now() < end) std::this_thread::sleep_for(1ms);
            return 0;
        });
        Require(Until([&] { return started.load(); }), "shutdown fixture starts");
        auto start = Source::Clock::now(); source.Stop();
        Require(Source::Clock::now() - start < 1s, "shutdown requests cancellation and joins worker");
        Require(!source.Request(L"closed", 1s, value), "closed source cannot start another worker");
        TestRefreshReadContract();
        TestPendingCoalescing();
        TestWaitSupersessionAndWakeFailure();
        TestConcurrentStop();
        std::cout << "Content source host: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "FAIL: " << ex.what() << '\n'; return 1;
    }
}
