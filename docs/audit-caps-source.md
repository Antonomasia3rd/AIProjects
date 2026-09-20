# Native Caps Lock LED source/action engine

Updated 2026-09-11. The existing capsblink pattern and lifecycle now have a shared native implementation for DesktopStub. Preview/source selection alone does not enable device actions. The C# standalone remains until host feature parity is established.

## Existing behavior preserved

- Target defaults to `\Device\KeyboardClass0`; the case-insensitive prefix must end with decimal digits and fit the existing 1024-character limit.
- Interval defaults to 500 ms, with the existing range 50 through 86400000 ms.
- Each tick queries the current indicator structure. Logical Caps Lock on forces only bit `0x4` on; logically off XORs only that bit. UnitId and other indicator flags remain intact.
- Interval changes retain the current device. Target changes restore/close the old target before opening its replacement.
- Normal cleanup synchronizes Caps to the current logical state while preserving the latest other LED flags. It does not blindly restore stale original flags. Original indicators are captured for status and fallback when cleanup reads fail; failed out-parameters are discarded.
- Native target ownership uses the same Global namespace and first-eight-byte uppercase SHA-256 identity as the C# machine-scoped mutex. Unique DOS mapping names avoid same-process collisions.
- Native query responses reject truncated indicator structures; the original C# query did not validate the returned length.

## Public API

`dependencies/hardware/caps_blink_engine.h` has no Windows dependencies:

- `aip::caps::Config`: `actionsEnabled=false`, `target`, `intervalMs`.
- `Indicators`: 16-bit `unitId` and `flags`; pure `Pattern` and `Restore` functions.
- `Controller(unique_ptr<Backend>)`: `Start`/`Update`, `RequestStop`, `Stop`, and copied `GetSnapshot`.
- `Snapshot`: phase/running/actionsEnabled/cleanupPending; logical state; original, applied, and restored indicators with validity flags; target/interval; iteration/write counts; error and cleanupError.
- Injected Backend: Open, ReadIndicators, ReadLogicalCaps, WriteIndicators, HasPendingOperation, Close. Device operations execute on one owner worker; partial Open is followed by Close.

`dependencies/hardware/caps_blink_windows.inc` supplies `MakeWindowsBackend(optional LogicalCapsReader)`. Construction is inert. Actual mutex/mapping/device/IOCTL access starts only through an enabled action. The optional reader lets a host supply UI-sampled logical Caps Lock state; the default retains the legacy GetKeyState behavior. Host Startup/tray/INI policy remains outside the hardware engine.

## Cancellation and cleanup ordering

An early draft released target ownership while canceled I/O could still complete. That race is fixed: the original owner worker remains alive with its mutex, mapping, and device while `cleanupPending=true`. After pending completion, the controller re-reads and restores state before release. It never marks an unresolved late write as restored. Restoration retries are limited after settled operation failures; failures remain visible.

Native IOCTL waits have deadlines and cancellation. Kernel-visible OVERLAPPED structures, data buffers, and device references survive cancellation through completion, following Microsoft's [CancelIoEx lifetime requirements](https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-cancelioex). One-shot completion waits retain storage until notification; [CloseThreadpoolWait](https://learn.microsoft.com/en-us/windows/win32/api/threadpoolapiset/nf-threadpoolapiset-closethreadpoolwait) releases an outstanding wait after its callback completes.

RequestStop is nonblocking. Stop, destruction, or Update(false) can wait for driver cleanup; a host UI should request cancellation, poll GetSnapshot().running, and only join after it becomes false. A driver that ignores cancellation can delay final shutdown. Synchronous device opening also cannot be promised a hard driver-independent deadline. These limitations are reported rather than hidden behind unsafe handle closure.

## Verification completed

`tools/TestCapsBlinkEngine.cmd` passed with MSVC `/W4 /WX`:

- **25 fake-backend checks**.
- **131,072** indicator/logical-state combinations checked against the legacy pattern.
- **50** start/stop cycles with concurrent immutable status reads and no remaining fake handles/mappings.
- Preview produces **zero** backend calls, including logical-key reads.
- Interval/target changes, original-state capture, cleanup fallback, partial-open/query/exception/close failures, and blocked-read cancellation.
- A fake late write proves owner resources remain held while pending, and restoration occurs after completion but before ownership release.
- Windows adapter compiled using `/Zs`; it was never executed.

The final run includes partially modified failed-read outputs, ensuring fallback uses the captured original state. No real keyboard, LED, GetKeyState call, device mutex, DOS mapping, hook/hotkey, or global keyboard state was exercised. Final pre-test usage was 44% five-hour and 78% weekly consumed, with zero reset credits. Test processes completed.

The pattern/controller suite is standard C++17 and can also be built on other platforms, for example `c++ -std=c++17 -pthread -Wall -Wextra -Werror tools/CapsBlinkEngineTests.cpp -o build/CapsBlinkEngineTests`. Linux execution was not performed in this Windows session. Windows driver compatibility and actual LED restoration remain unverified here.

## Integration status

The native engine/adapter and tests are ready for DesktopStub's separately gated action host. Selecting a source, generating a preview, or reading a snapshot must not call Start with actionsEnabled=true. The C# standalone implementation has not been removed or silently redirected; it remains until the parent integration reaches feature parity. No new credential/trust/security policy was introduced.

## Saved checkpoint — 2026-09-12

All implementation, adapter, tests, and documentation are on disk. The final
verified outcome remains **25 fake checks, 131,072 pattern combinations, 50
lifecycle cycles, and Windows adapter `/Zs /W4 /WX` compilation passed**. The
pending-I/O ownership and failed-read fallback fixes are included in that final
run. No subsequent implementation edits or unrecorded tests remain.

Parent-owned work is DesktopStub integration/checkpointing. The host must keep
the controller and logical-reader captures alive during cleanup, request stop
without blocking its UI, and join only after the copied running flag is false.
Do not release target ownership or call a pending restore successful early.
Real device behavior and C# standalone removal remain deferred. This checkpoint
performed no compilation, tests, hardware actions, or additional implementation.
