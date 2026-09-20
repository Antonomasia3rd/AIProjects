# Asynchronous source host audit

Updated 2026-09-10. Bounded review, lifecycle fix and synthetic test expansion completed. No real network, SMTC, package or settings operations.

## Concrete findings sent to root

- **Fixed:** `AsyncSource::Stop` accessed and joined `worker_` without the mutex used by `Request` to create it. Concurrent Request/Stop or multiple Stop callers could therefore race on the same `std::thread` object. A dedicated stop mutex now serializes Stop callers, and setting the stopped flag under the request mutex excludes worker creation before joining outside that mutex.
- `Read` serves the latest published same-key cache during a refresh. `Wait` means that the request reached a terminal result, including failure; it does not mean `Read` will succeed. Current RSS/SMTC wrappers ignore the boolean result from synchronous `Wait`, so a timeout can silently become Loading text or an older cached result. Root owns wrapper changes.
- Fetch cancellation is cooperative. SMTC currently checks cancellation before its bounded API call; replacing its request can suppress publication immediately but cannot interrupt the already-running Windows call. The host itself must never detach a callback that captures application state.
- **Fixed test lifetime:** the original test declared Source before atomics captured by callbacks. An assertion exception could destroy the atomics before Source destruction canceled/joined the worker. Fixture state now precedes Source so it outlives worker teardown.

## Test plan / checkpoint

The first extended suite passed **10,039 checks** before the Stop synchronization change. The final MSVC suite passed **10,103 checks**, including 32 races between repeated requests and two concurrent Stop callers. Added coverage includes superseded pending-request coalescing, same-key cache while refreshing, fresh completion versus failure, old-key waiter invalidation, and throwing wake callbacks. Every blocking fetch fixture observes cancellation and has its own deadline. Fixture objects outlive Source and async waiters.

Command: `tools\TestContentSourceHost.cmd`; log: `DesktopStub/build/source-host-audit-tests.log`. Compilation used C++17 with `/W4 /WX`. `git diff --check` passed. The tests remain portable C++17 without Windows headers; this turn executed them on Windows, not under a race sanitizer or a different OS. The synthetic races exercise the shutdown fix but do not constitute a full race-detector audit.

First quota read was unavailable. The final test checkpoint was five-hour 15%, weekly 72%. Only the shared header and tests were edited; no provider-runtime or real-provider operation was changed/performed by this subtask. Root owns the RSS/SMTC/Notes synchronous-wait handling fix.

## API contract

`Read` can return the last completed same-key cache while a refresh is pending. `Wait` returns true for terminal success or failure; a caller must inspect `Read` and its error. A superseded/canceled key cannot satisfy an old wait or publish its result. Stop is an external-owner operation; Fetch/Wake callbacks must not stop or destroy their own source because a worker cannot join itself. Fetchers remain responsible for bounded I/O and cooperative cancellation. These contracts are now stated in the header.
