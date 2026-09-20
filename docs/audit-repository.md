# Repository build and test audit

Work log: 2026-09-07 onward. Findings come from the current working tree, which already contained extensive uncommitted changes when this audit began. Existing changes are preserved. This file records build/test findings; `REWORK_AUDIT.md` tracks the overall rework.

## Architecture inventory

| Product | Implementation and build | Consolidation status |
| --- | --- | --- |
| DesktopStub | C++17, Win32/GDI+/WinRT, MSVC; host and broker executables | Product entry points compose `dependencies/DesktopStub` and the shared desktop baseline. Content/plugin restructuring is in progress. |
| DiscordRPC | C++17, Win32/WinHTTP, MSVC | Small product wrapper; implementation in `dependencies/DiscordRPC`. |
| ADBController | C++17, Win32, MSVC | Product wrapper/resources and batch launcher; implementation in `dependencies/ADBController`. |
| CharmTray | C++17, Win32, MSVC | Product wrapper; implementation in `dependencies/CharmTray`. |
| NowPlayingTile | C++17, Win32/WinRT/GDI+, MSVC | Product source fragments moved to `dependencies/NowPlayingTile`; deleted old `src/` files are an existing pending change. |
| RealTimeNotesDeskband | C++17 COM DLL, MSVC or MinGW | Implementation in `dependencies/RealTimeNotesDeskband`; this audit makes MSVC the preferred compiler. |
| SecureDesktopLauncher | C++17 LocalSystem service and password launcher, MSVC | Product wrappers; implementation in `dependencies/SecureDesktopLauncher`. |
| asusblink | C#/.NET Framework, WinForms/WMI/ACPI | Metadata wrapper plus `dependencies/asusblink`; consumes shared managed INI, tray, startup and logging. |
| capsblink | C#/.NET Framework, WinForms/device I/O | Metadata wrapper plus `dependencies/capsblink`; consumes shared managed baseline. |
| DNSAutoUpdate | C#/.NET Framework resident utility | Metadata wrapper plus `dependencies/DNSAutoUpdate`; consumes shared managed baseline. |
| PhotoCollage | C#/.NET Framework/System.Drawing CLI | Metadata wrapper plus `dependencies/PhotoCollage`; no common resident tray/startup settings contract yet. |
| TaskSchedulerMigration | C#/.NET Framework/Task Scheduler COM CLI | Wrapper plus `dependencies/TaskSchedulerMigration`; no common resident tray/startup settings contract yet. |
| AllowContentAboveLock | C#/.NET Framework Windows service | Declarative policy overlay over `dependencies/registry_notification_service.cs`. |
| YourPhoneHideBanner | C#/.NET Framework Windows service | Declarative policy overlay over the same service engine. |
| WindhawkMods (three mods) | C++23, Windhawk's Clang/MinGW SDK | Host-specific mods remain outside the 14-product release map and shared application lifecycle. |

Moving product implementation into a dependency subdirectory has made the entry points smaller, but it has not turned every product into a reusable plugin. Product-specific fragments still contain large independent implementations. The seven managed products above also remain C#; converting their service, WMI, WinForms, COM and device behavior to C++ would be a functional rewrite requiring separate parity validation. The Windhawk SDK is a separate host ABI and cannot simply be replaced with DesktopStub's build command.

## Findings and fixes

- **Fixed: portable checks were incomplete and not release-gating.** Linux CI previously inspected only the declarative tile layout; Windows releases could proceed even if that independent job failed. `tools/CMakeLists.txt` now builds the portable C++ source guards and layout runtime test, and the Windows job depends on the portable job. CTest gives each check its correct product working directory and a timeout.
- **Fixed: NowPlayingTile's source test used Windows-only paths.** Its source loader now normalizes path separators so the same executable can run on Linux/macOS.
- **Fixed: the deskband needlessly selected a second toolchain.** `BuildDeskband.cmd` now tries MSVC first, with MinGW as a compatibility fallback. Windows CI no longer installs MinGW solely for the deskband.
- **Fixed: missing artifacts could produce a green smoke run.** `Smoke-WindowsBuild.cmd --projects All` or `--projects DesktopStub,DiscordRPC` now validates the complete selected artifact list before launching any process, rejects invalid selections, and runs only selected products. CI passes its selected project list. The legacy no-selector local invocation still skips unbuilt projects.
- **Fixed: source suites were omitted from aggregate builds.** NowPlayingTile and RealTimeNotesDeskband source guards now run in the aggregate Windows build. TaskSchedulerMigration already invokes its local suite inside its own build script.
- **Fixed: temporary compiler directory creation could retry forever.** The repository helper now bounds directory creation retries and removes only its generated executable and empty directory during cleanup.
- **Fixed: dependency ownership assumed every subdirectory was a product.** `dependencies/content_sources` is now a shared namespace; DesktopStub declares it as a build dependency. Windows CI also runs the shared read-only SMTC provider harness.
- **Fixed: the bug-report selector omitted ADBController and WindhawkMods.** Both are now selectable.
- **Remaining: source guards are not behavioral coverage.** Many tests look for literal source text or regex patterns. They detect accidental removals but can pass broken code and fail correct refactors. Real INI, concurrency, image rendering and process-lifecycle tests are stronger evidence. New integration/plugin work needs runtime coverage in addition to these guards.
- **Remaining: noninteractive tests cannot certify the Windows Start screen.** Portable geometry tests can cover bounds, routing, line counts and scheduling logic. Windows bitmap tests exercise GDI+ drawing. Actual Windows 8/8.1/10 shell presentation, native Live Tile cycling, Explorer restart and hardware-specific behavior still need the corresponding OS/host/device.
- **Remaining: common configuration surfaces are not universal.** CLI utilities, COM deskbands, services and Windhawk mods have different hosts. They do not all yet expose equivalent INI/CLI/tray/startup controls. Relocating their source did not complete this requirement.
- **Historical conflicting policies found:** root README prescribed sidecar-only storage, an exact UTF-8 BOM/quoted INI format, no ACL changes, and mandatory maintainer approval for storage/format/migration/security/OS integration changes. Product docs also prescribed mandatory protected-directory trust and DPAPI-only credentials. The user subsequently confirmed current instructions supersede conflicting old rules. Root/shared audit owns the documentation and runtime follow-up; these historical statements are not adopted as new requirements here.

## Validation record

Initial host inspection found Visual Studio 2019 Build Tools and .NET Framework `csc.exe`. `wsl --list --quiet` reported no distributions. No WSL installation is needed for portable logic tests: CMake with a native C++17 compiler runs on Windows, Linux or macOS. CMake itself was not initially available on this host.

Completed baseline before current fixes:

- Project map: passed, 14 projects.
- Workflow project selector: passed, 14 projects plus All.
- README consistency scan: no warnings; this is only a keyword-based advisory scan, not evidence that documentation matches the code.
- Shared C++ baseline: runtime tests passed; 297 source checks passed.
- Registry notification service: 52 source checks and 19 runtime checks passed; help/version/invalid-command binary checks passed without installing either service.
- Managed legacy utility runtime/source suite: passed, including INI transactions/concurrency, Startup transactions, logger concurrency, inert DNS behavior and image collage generation.
- ADBController: source/compile checks passed (42 checks); build and 8 binary smoke scenarios passed.
- AllowContentAboveLock, YourPhoneHideBanner, asusblink, capsblink, DiscordRPC, DNSAutoUpdate, NowPlayingTile and PhotoCollage compiled. DiscordRPC's 176 source checks passed. NowPlayingTile reported warnings in Windows SDK 19041 GDI+ headers.
- The initial aggregate build stopped at DesktopStub's old fixed-layout regex guard while its renderer was being refactored. This was recorded rather than treating the partial build as a full pass.

Further validation results will be appended here as the concurrent changes settle.

Follow-up validation completed 2026-09-08:

- All 13 non-DesktopStub products compiled. The deskband passed MSVC `/W4 /WX` compile checks, 65 source checks, and linked its DLL. SecureDesktopLauncher source/protected-path tests and both binaries passed. CharmTray compiled and linked. TaskSchedulerMigration's 17 local checks passed.
- All three Windhawk mods passed syntax checks under the installed Windhawk Clang SDK for both x86 and x64.
- Selected Windows smoke passed for all 13 non-DesktopStub products. Behavioral scenarios include help/version side-effect checks, typed INI/CLI validation, inert DNS tray resident/reload/exit, and DiscordRPC no-tray resident reload/exit. Products without process-level smoke scenarios only received their existing artifact checks; this is not equivalent coverage for each product.
- All nine portable C++ executables compiled and ran with the installed Windhawk Clang 20 compiler: eight source guards plus 236 tile-layout runtime checks. This is a second compiler on Windows; Linux/macOS execution remains assigned to CI. Windhawk's default dynamic libraries use custom `.whl` import names, so the local standalone test executables were linked with `-static`. The first dynamic attempts failed before `main`; they were environment failures, not passing tests.
- Portable source results: shared baseline 297, DesktopStub 1716, DiscordRPC 176, ADBController 42, CharmTray 35, NowPlayingTile 67, RealTimeNotesDeskband 65, and SecureDesktopLauncher source suite all passed. Logs are under ignored `build/portable-clang/`.
- CMake/CTest configuration itself has not yet run locally because this host has no CMake installation. The underlying compile/run cases were executed directly as above. Final DesktopStub build/render/content-engine checks are owned by the corresponding implementation audits and will be recorded there.
- Final project-map and workflow-selector checks passed after the shared provider namespace fix. The missing-artifact negative smoke case returned exit code 1 before launching any application. Changed infrastructure passed `git diff --check` (Git only reported expected LF-to-CRLF conversion notices).
- The new `tools/ContentEngineTests.cpp` exercises the actual `dependencies/content_engine.h` policy with in-memory configuration readers. It passed MSVC `/W4 /WX` and Clang: **282,068 assertions including 100,000 deterministic randomized schedule iterations**. Coverage includes invalid bounds/types/providers, transactional failed reload, disabled entries, INI/CLI normalizer parity, pause/resume, long stalls without catch-up bursts, unsigned clock rollover, UTF-16 clipping, layered text fields, and registration/native timing warnings. No model defects were found by these cases. These tests do not claim to validate the host's provider threads or package lifecycle.
- The portable preset XML generator suite also passed Clang: **494 checks, zero failures**. CMake now contains 11 C++ test executables plus the optional PowerShell layout specification check.
- `tools/TestContentEngine.cmd` is the standalone native runner and is called by `DesktopStub/TestDesktopStubSource.cmd`, preserving the existing layout/template/rendering commands. One test-only MSVC constant-condition warning was fixed using C++17 `if constexpr`; the final warning-as-error build passed.
- The root-owned actual host/menu/snapshot harness (`tools/TestContentRuntime.cmd`) is now invoked after successful rendering checks in `DesktopStub/TestDesktopStubSource.cmd`. Windows CI already gates the DesktopStub build on that suite, so the harness runs once there without a duplicate CI invocation. Root reported its 23 runtime checks passing before integration; this wiring change was inspected without rerunning the full suite while source assertions were being updated.
- The portable model suite uses explicit throwing `Require` checks rather than C/C++ `assert`, so Release builds and `NDEBUG` cannot silently omit its assertions.

## Repeatable portable checks

From the repository root, with CMake and a C++17 compiler installed:

```text
cmake -S tools -B build/portable -DCMAKE_BUILD_TYPE=Release
cmake --build build/portable --config Release --parallel 2
ctest --test-dir build/portable -C Release --output-on-failure
```

The CMake project builds tests only. Windows product builds continue to use their existing build scripts. If PowerShell is available, CTest also runs the declarative TileText specification check. A missing PowerShell executable does not skip the compiled tile-layout runtime test.

For Windows process/configuration smoke, build the requested products first, then run:

```text
.github\scripts\Smoke-WindowsBuild.cmd --projects All
.github\scripts\Smoke-WindowsBuild.cmd --projects DesktopStub,DiscordRPC
```

`--projects` rejects missing/empty selected artifacts before starting tests. These smoke tests use temporary profiles. DNS profiles stay disabled, Discord uses dry-run, and service installers are not exercised by this entry point. DesktopStub now uses offline render/configuration entry points; package registration and normal DesktopStub/Discord resident integration require explicit `--allow-package-integration`. That opt-in was not run during the smoke-isolation pass. See [the updated smoke isolation audit](audit-smoke-safety.md). Native Start-screen appearance still requires visual inspection on the target OS.
