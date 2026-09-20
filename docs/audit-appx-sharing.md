# AppX sharing audit

Updated 2026-09-10. Bounded sharing task completed. Tests used compile checks, pure XML/script inspection and inert subprocess fixtures; no package registration, activation, StartupTask mutation, or normal app launch.

## Findings

- NowPlayingTile duplicated command-line quoting, PowerShell literal escaping, Base64 encoding, system-directory discovery, process/pipe creation, timeout handling, and output capture in `dependencies/NowPlayingTile/npt_manifest.inc`. It now consumes the same shared runner as DesktopStub.
- NPT's unbounded pipe-draining loop can prevent its outer timeout check from running while a child continuously produces output. Further inspection found that the shared runner also lacked a per-pass drain limit; this stage adds a 64 KiB limit so process/timeout checks remain reachable independently of capture limits.
- DesktopStub built its PowerShell registration script with a double-quoted manifest path. Valid path characters such as `$` and backticks could be interpreted by PowerShell instead of reaching `Add-AppxPackage` literally. Both consumers now use the shared typed builder with single-quoted literal paths.
- NPT duplicated the startup manifest XML; both products now consume `packaged_startup_manifest.h`.
- DesktopStub has richer native COM registration timeout/uncertain-result tracking and helper-process fallback; NPT currently performs registration via PowerShell and activation via `IApplicationActivationManager`. This stage will preserve those app-level choices and NPT widget/artwork behavior.

## Shared mechanism

The typed `AppxRegistrationSpec` and pure registration-script builder use the existing shared PowerShell literal formatter. Both products' PowerShell registration call sites consume it. NPT process execution uses the same captured runner as DesktopStub, and its startup extension uses the shared XML fragment builder. Per-app identity, paths, logging, and result presentation remain in the adapters.

Initial quota checkpoint: five-hour 59% consumed; weekly 9% consumed. Final project-map validation checkpoint after external resets: five-hour 36%, weekly 40%. This subtask did not consume reset credits.

## Implemented checkpoint

- Added pure typed `appx_registration_script.h`, consumed by DesktopStub's two PowerShell registration paths and NPT's registration function. Manifest paths use shared literal quoting; `-ErrorAction Stop` prevents a later lookup from concealing failed registration.
- NPT now includes the same `powershell_runner.inc` already used by DesktopStub. Its approximately 320-line process/encoding implementation is replaced with a small options/result adapter. Existing timeout and output-cap values are retained; wait failures and uncertain termination are reported.
- The shared runner now returns to process/deadline checks after each 64 KiB drained. It records confirmed termination for output-overflow and wait-failure paths as well as timeouts, and transfers unresolved-process ownership through the existing callback for all three failure causes.
- NPT startup XML now calls `BuildDesktopStartupExtension` using its existing task ID, executable and display name. Native media, artwork, widget, activation arguments, and COM registration policy were not merged or removed.
- New `tools/TestAppxSharing.cmd` compiled and ran 16 checks with zero failures. Scripts and XML were inspected only. Continuous-output process tests launch the test executable itself, which only writes a fixed stdout buffer; no PowerShell process or application is invoked. Capture-limit termination was confirmed. With uncapped capture, the 50 ms timeout completed and confirmed child termination in **63 ms**.
- Compile-only checks for both consumer hosts passed: `NptAppxSharingCheck.obj` and `DesktopAppxSharingCheck.obj` under `DesktopStub/build/obj`; log `DesktopStub/build/appx-sharing-compile.log`. No production EXE was launched, registered, replaced, or stopped by this stage.
- NPT source checks passed **71 checks**; DesktopStub source checks passed **1,727 checks**, including actual shared-builder/runner consumption guards. Logs: `DesktopStub/build/appx-sharing-npt-source.log` and `DesktopStub/build/appx-sharing-desktop-source.log`.
- Both projects' source-test runners invoke `tools/TestAppxSharing.cmd`, so either project's CI build runs the isolated shared tests. `.github/project-map.json` now declares the new shared registration/startup headers for both consumers and the PowerShell runner for NPT. Project-map validation passed for **14 projects**; the parent's `content_source_host.h` entry was preserved. `git diff --check` passed.
- README/dependency documentation describes the shared execution behavior, bounded stdout handling, and initially disabled startup extension. Actual package deployment and activation remain intentionally untested on the user's computer.

## Duplicate mechanisms left separate

DesktopStub's native `PackageManager.RegisterPackageAsync` path retains its helper process, cancellation and uncertain-registration tracking. NPT retains its `IApplicationActivationManager` adapter and package lookup/result UI. Consolidating those into one typed native API requires a separate bounded design preserving the different lifecycle contracts; the current shared mechanism is real registration command construction plus process execution, consumed by both products.
