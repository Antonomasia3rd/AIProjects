// No Windows/window/image/provider APIs. Synthetic clocks and callbacks cover
// capture retry cadence, startup bursts, overlap, reset and exception cleanup.
#include "../dependencies/capture_attempt.h"
#include "../dependencies/live_capture_protocol.h"
#include <iostream>
#include <stdexcept>
#include <atomic>
#include <chrono>
#include <thread>

static int checks = 0;
static void Check(bool ok, const char* message) { ++checks; if (!ok) throw std::runtime_error(message); }
int main() {
    try {
        {
            using namespace aip::capture;
            HelperRequest request;
            bool recognized = false;
            std::vector<std::wstring> arguments = {L"fixture.exe", HelperArgument, L"123", L"10", L"7", L"8", L"320", L"180", L"0", L"99"};
            Check(ParseHelperArguments(arguments, request, recognized) && recognized && request.window == 123 &&
                request.windowProcessId == 10 && !request.screenFallback && request.minimumWidth == 320 && request.requestId == 99,
                "isolated helper arguments parse without platform or sidecar operations");
            const auto original = request;
            const auto valid = arguments;
            for (unsigned field = 2; field < valid.size(); ++field) {
                arguments = valid; arguments[field] = L"-1";
                Check(!ParseHelperArguments(arguments, request, recognized) && recognized && request.requestId == original.requestId,
                    "signed helper fields reject before capture and leave the prior request untouched");
            }
            arguments = valid; arguments.push_back(L"--startup");
            Check(!ParseHelperArguments(arguments, request, recognized) && recognized, "helper mode cannot combine with normal application options");
            arguments = valid; arguments.insert(arguments.begin() + 1, L"--configure-only");
            Check(!ParseHelperArguments(arguments, request, recognized) && recognized, "a displaced reserved helper marker still rejects before normal startup");
            arguments = valid; arguments[9] = L"18446744073709551616";
            Check(!ParseHelperArguments(arguments, request, recognized), "64-bit request overflow rejects instead of wrapping");
            arguments = valid; arguments[8] = L"2";
            Check(!ParseHelperArguments(arguments, request, recognized), "screen fallback requires an explicit boolean bit");
            arguments = valid; arguments[4] = arguments[5];
            Check(!ParseHelperArguments(arguments, request, recognized), "output and response handle identities must differ");
            arguments = {L"fixture.exe", L"--version"};
            Check(!ParseHelperArguments(arguments, request, recognized) && !recognized, "ordinary commands do not enter helper mode");
            auto reply = MakeReply(original, Status::Success); reply.width = 640; reply.height = 360;
            Check(ValidReply(original, reply), "fixed reply validates its request and minimum-size metadata");
            ++reply.requestId;
            Check(!ValidReply(original, reply), "a mismatched response cannot publish another attempt's bitmap");
        }
        using aip::CaptureAttemptSchedule;
        CaptureAttemptSchedule schedule;
        auto first = schedule.Begin(0, 10000);
        Check(static_cast<bool>(first) && schedule.InFlight(), "initial attempt can start at monotonic zero");
        for (unsigned now = 0; now < 10000; now += 10)
            Check(!schedule.Begin(now, 10000), "an outstanding capture never overlaps another caller");
        schedule.Complete(first, 500);
        Check(!schedule.InFlight() && !schedule.Begin(10499, 10000), "failed completion delays retry by the full configured interval");
        auto retry = schedule.Begin(10500, 10000);
        Check(static_cast<bool>(retry), "a failed capture retries exactly at its next eligible time");
        schedule.Complete(first, 20000);
        Check(schedule.InFlight(), "a stale completion cannot release a newer capture");
        schedule.Reset();
        Check(!schedule.Begin(20001, 10000), "provider reset invalidates timing without overlapping old capture");
        schedule.Complete(retry, 20001);
        auto replacement = schedule.Begin(20001, 10000);
        Check(static_cast<bool>(replacement), "old-provider completion cannot delay the replacement provider");
        schedule.Complete(replacement, 20002);
        Check(static_cast<bool>(schedule.Begin(1, 10000)), "a backwards synthetic clock resets timing safely");

        Check(aip::CaptureAttemptRetryMs(10000, 1000, true) == 1000 &&
            aip::CaptureAttemptRetryMs(10000, 1000, false) == 10000,
            "startup bursts retain their own interval independently of normal refresh");
        Check(aip::CaptureAttemptRetryMs(0, 0, false) == 1000,
            "capture-once retries initial failures with a one-second floor");
        {
            CaptureAttemptSchedule gate;
            std::mutex mutex;
            std::uint64_t now = 0;
            int captures = 0;
            bool successfulSnapshot = false;
            for (unsigned poll = 0; poll < 30; ++poll) {
                std::unique_lock<std::mutex> lock(mutex);
                if (!successfulSnapshot) { // Same outer once-success condition as the host.
                    const auto token = gate.Begin(now, aip::CaptureAttemptRetryMs(0, 0, false));
                    if (token) {
                        const auto clock = [&]() noexcept { return now; };
                        aip::CaptureAttemptScope<decltype(clock)> attempt(gate, token, lock, clock);
                        lock.unlock(); ++captures;
                        if (captures == 2) successfulSnapshot = true;
                    }
                }
                now += 100;
            }
            Check(captures == 2 && successfulSnapshot && !gate.InFlight(),
                "capture-once stops forever after the first successful retry");
        }
        {
            CaptureAttemptSchedule gate;
            std::mutex mutex;
            std::uint64_t now = 100;
            std::unique_lock<std::mutex> lock(mutex);
            try {
                const auto token = gate.Begin(now, 1000);
                const auto clock = [&]() noexcept { return now; };
                aip::CaptureAttemptScope<decltype(clock)> attempt(gate, token, lock, clock);
                lock.unlock(); now = 150;
                throw std::runtime_error("synthetic capture failure");
            } catch (const std::runtime_error&) {}
            Check(lock.owns_lock() && !gate.InFlight() && !gate.Begin(1149, 1000),
                "exception cleanup reacquires the host mutex and records retry time");
            Check(static_cast<bool>(gate.Begin(1150, 1000)), "the source recovers after throwing capture code");
        }
        {
            CaptureAttemptSchedule gate;
            std::mutex mutex;
            std::atomic<bool> entered{false}, release{false};
            std::atomic<int> active{0}, attempts{0};
            bool overlapBlocked = false, resetBlocked = false;
            int lastGoodSnapshot = 42;
            std::thread capture([&] {
                std::unique_lock<std::mutex> lock(mutex);
                const auto token = gate.Begin(0, 1000);
                const auto clock = []() noexcept { return std::uint64_t(100); };
                aip::CaptureAttemptScope<decltype(clock)> attempt(gate, token, lock, clock);
                lock.unlock(); ++attempts; ++active; entered = true;
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
                while (!release && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
                --active; // Synthetic capture fails and leaves the published last-good value intact.
            });
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (!entered && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            {
                std::unique_lock<std::mutex> lock(mutex);
                overlapBlocked = active == 1 && !gate.Begin(10000, 1000);
                gate.Reset();
                resetBlocked = !gate.Begin(20000, 1000);
            }
            release = true; capture.join();
            Check(entered && overlapBlocked && resetBlocked && attempts == 1 && lastGoodSnapshot == 42,
                "concurrent host callers keep last-good content and cannot overlap an unlocked capture, even after reset");
            Check(!gate.InFlight() && static_cast<bool>(gate.Begin(100, 1000)),
                "finishing the old epoch releases the guard without delaying a new provider");
        }
        std::cout << "Capture attempt schedule: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
