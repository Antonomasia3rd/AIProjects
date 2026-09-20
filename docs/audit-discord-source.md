# Embedded Discord service extraction

Task: reuse DiscordRPC's actual presence builder and IPC/Gateway transports from DesktopStub without launching the standalone application's tray, console, Startup or token-migration lifecycle. No real Discord traffic, user token reads or normal application launches are permitted during validation.

## Inspected coupling

- `drpc_ipc.inc` and `drpc_gateway.inc` already accept cancellation callbacks. IPC uses overlapped I/O; Gateway has its own reader/heartbeat threads and bounded protocol options.
- `drpc_presence.inc` contains the actual template, censor, asset and JSON generation. It reads the common INI helpers and currently stores CPU sampling state in translation-unit globals.
- `drpc_app.inc::RunPresenceLoop` combines transport work with token migration, Startup reconciliation, tray tooltip updates, notification delivery and application logging. Reusing that loop directly would violate the embedded contract.
- `DiscordRPC.cpp` initializes sidecar paths and owns application entry/lifecycle. It currently includes all implementation fragments into one translation unit.
- Gateway shutdown joins internal threads after requesting WebSocket close. The service must not promise immediate cancellation while ignoring these transport-owned cleanup steps.

## Planned interface and source ownership

Repository-validation agent owns the new reusable API/service and inert tests under `dependencies/DiscordRPC`, its small `dependencies/content_sources` adapter, and standalone DiscordRPC build/wrapper integration. DesktopStub source/catalog wiring belongs to root after the API is available.

Proposed namespace: `aip::discord`. `Service` offers `Start(Options)`, `Reload(Options)`, `Stop(timeout)`, and a snapshot returned by value. Options include an explicit profile path, refresh interval and `sendEnabled=false` by default. Snapshots expose the generated activity JSON plus name/details/state, transport status, last error and a revision. No callback may retain a destroyed host object.

The service will compile the existing real presence/transport fragments, not duplicate their protocols or use an external status-file bridge. The standalone loop will use the same service. Service profile context must remain instance/thread scoped; runtime configuration is read-only. Preview mode must avoid credential resolution and transport creation entirely. Tests inject fixture context and fake transport and verify cancellation/reload/snapshot isolation without network or user files.

## Current progress

Concrete implementation now exists:

- `dependencies/DiscordRPC/service.h`: `aip::discord::Service` with Start/Reload, bounded Stop, and snapshot-by-value Read. Options default to sending disabled and accept explicit profiles; fixture-only immutable Profile/Context and owned Transport factory injection are available.
- `dependencies/DiscordRPC/service.cpp`: compiles the actual existing presence builder, IPC and Gateway implementations. It clones the profile/context per reload, owns its worker/transport, publishes previews and status/error snapshots, and never invokes app Startup, tray, console, configuration migration or token persistence. It resolves credentials only inside real enabled Gateway sending.
- `dependencies/DiscordRPC/drpc_environment.inc`: extracted the common compilation environment from the standalone wrapper. Service profile path is thread-local; the existing presence builder's CPU sample state is also thread-local for independent instances.
- `dependencies/content_sources/discord.h`: maps the in-process snapshot directly to tile text; no sidecar bridge or standalone process is used.
- Standalone `RunPresenceLoop` now delegates transport/presence work to the same Service; its existing host-owned reload/Startup/tooltip responsibilities remain outside the service. `DiscordRPC/build.cmd` compiles and links `service.cpp` separately.
- `tools/DiscordServiceTests.cpp` and `tools/TestDiscordService.cmd`: fixture-only lifecycle, reload, preview, fake sending/error/cancellation and concurrent snapshot tests. No real connection or user credential file is used.

Completed validation:

- Service translation unit compiled successfully. The standalone DiscordRPC executable also compiled and linked against the same service (`build/discord-standalone-service-build.log`); it was not launched.
- The first fixture run exposed an actual presence-builder assumption: `ChooseSmallAsset` used `tokens.at("win_title")`, which threw for a supplied partial context. It now uses a checked lookup. The fixture suite then passed **129 checks and 40,000 concurrent snapshot reads** (`build/discord-service-tests.log`). These tests exercised preview, real template/JSON generation, the in-process tile adapter, fake sending/error/recovery, cancellation, pending cleanup, reload and two-profile isolation.
- Updated source guards passed **186 checks** (`build/discord-service-source-check.log`), including actual transport reuse, standalone delegation, preview default, profile snapshots and absence of service lifecycle side effects.
- Final focused fixture pass: **131 checks and 40,000 snapshot reads passed**, including missing-client rejection before sending and observable pending-cleanup state. No real transport or token file was involved.
- Repository project-map validation passed for 14 products. DesktopStub declares the Discord service dependency; CI routing shares only the service API/TU and its directly compiled implementation fragments. Standalone tray/app/CLI changes remain DiscordRPC-owned. Windows CI runs the inert Discord service tests.
- Workflow selector validation also passed after fixing filename matching: `service.cpp` had falsely matched the suffix of `SecureDesktopLauncherService.cpp`. Matching now requires filename boundaries, and known engine consumers use their explicit dependency declarations so same-named Discord/ASUS service files do not cross-select each other.
- The updated shared baseline source scanner understands the extracted Discord environment. Its subsequent run stopped on an unrelated older packaged-Startup guard expecting `RequestEnableAsync().get()`; that guard was reported to the owning root/packaged agent rather than misreported as a Discord service failure (`build/discord-shared-source-check.log`).

API Stop returns false if transport cleanup has not completed by its deadline; no claim of completed shutdown is made in that case. Snapshot reads remain responsive while cleanup is pending, and the host sees `Phase::Stopping`. A noncooperative injected transport is held by worker-owned shared state rather than dangling host references; callers must not unload the compiled service module while pending cleanup exists. The destructor makes one additional bounded wait and then relinquishes its thread handle while owned state drains. No real Gateway/IPC endpoint, network authentication or user credential file was used in this validation. Existing mandatory-DPAPI standalone behavior remains a documented follow-up; this extraction did not add new protection or silently rewrite embedded credentials.

Previous smoke-safety work is already complete and was not repeated for this task. Standalone transport communication and production Gateway shutdown were not exercised against Discord. The separate translation units emit expected MSVC C4505 notices for shared static helpers unused in that host; no compiler errors occurred. DesktopStub catalog/UI integration and its final host build belong to root.

Example host usage:

```cpp
#include "dependencies/DiscordRPC/service.h"
#include "dependencies/content_sources/discord.h"
aip::discord::Service service;
aip::discord::Options options;
options.profilePath = L"C:\\Tiles\\DiscordProfile.ini";
// options.sendEnabled remains false: preview only.
std::wstring error;
if (service.Start(options, error)) {
    auto snapshot = service.Read();
    auto tileText = aip::content::DiscordContent(snapshot);
    // Read again after the worker publishes a preview. No host callback needed.
    bool stopped = service.Stop(5000);
    // If false, retain the service/module and let its owned cleanup drain.
}
```

Compile `dependencies/DiscordRPC/service.cpp` as a separate translation unit and link `user32.lib`, `shell32.lib`, `shlwapi.lib`, `advapi32.lib`, `ole32.lib`, `uuid.lib`, `winhttp.lib`, and `crypt32.lib`. `tools/TestDiscordService.cmd` compiles and runs the fixture suite without any real Discord connection or credential file.

## Cancellation relay follow-up

Review during final host integration found a real race and lifetime flaw in the initial extraction: ActualTransport assigned a mutable cancellation function while Gateway background threads could call it, and member destruction disposed that function before Gateway cleanup. `service_cancel.h` now atomically publishes immutable callback objects through shared relay state. Transport callbacks own that state rather than capturing the transport pointer, and client cleanup precedes relay destruction.

Send/Clear operations retain their cancellation deadline for the full operation and clear it at scope exit. An expired completed-send deadline therefore cannot disconnect an otherwise idle Gateway. Validation used only the relay and injected transports: **140 checks, 40,000 snapshot reads, 80,000 concurrent relay reads passed**; **190 source guards passed**. The regression covers owner destruction, capture release, concurrent callback replacement, active cancellation, and idle/later-operation independence. Logs are `build/hardware-host-discord-relay.log` and `build/hardware-host-discord-source.log`. No real Gateway was contacted.
