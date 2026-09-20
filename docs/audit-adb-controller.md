# ADBController tray and Startup consolidation

Updated 2026-09-09. This work follows the user's current rules. Older generated documentation and existing dirty-tree code were editable. The foreground TV controller remains intact; shared tray and per-user Startup support are now implemented.

## Implemented

- `dependencies/ADBController/adb_controller_app.inc` consumes shared native tray and Startup helpers. The project-local source remains an include overlay.
- One validator serves CLI, loaded INI, and prospective setting mutations. Canonical keys: `RunAtStartup=0`, `ShowTrayIcon=1`, `ShowMenuAsDropdown=1`, `LoggingEnabled=1`, existing `Theme=Auto`, and `AdbPath=adb.exe`. CLI aliases and generic `--set` update the same keys.
- Foreground and tray menus share command handlers, dropdown/flat layout, checkmarks, appearance, device configuration, executable picker, open/reload configuration, and exit. Disabling the icon leaves foreground configuration available.
- Shared tooltip normalization supplies product/state/selection text. Version-4 hover is enabled. Keyboard activation and Explorer-restart restoration are wired. Closing the window retains the existing exit behavior.
- Startup defaults off and uses only the current user's `shell:startup` shortcut, with `--ini <effective profile>` and no captured TV action. Shared INI-coupled transactions order enable/disable correctly and roll back ordinary failures. Manual INI changes reconcile on launch/reload; failures appear in activity/logging and tooltip state without preventing use of the foreground controller.
- Saved CLI changes notify an existing profile. An active command defers and acknowledges configuration reload until completion.
- README/example INI describe actual behavior. The default build and `build-only` compile without invoking the product. `test-inert` runs the isolated harness. The old product binary smoke requires explicit `binary-smoke`.

## Verification

All completed with MSVC `/W4 /WX`:

- `TestADBControllerRuntime.cmd`: **20 checks passed**. Actual CLI parsing, typed INI/defaults, invalid input, both menu layouts/checkmarks, busy-state handling, deferred reload, keyboard mapping, tooltip bounds, Startup profile arguments, fake backend enable/disable ordering, failure rollback, and cleanup.
- `BuildADBController.cmd check`: **46 source checks passed** plus syntax validation.
- `BuildADBController.cmd build-only`: **executable build passed**. The resulting binary was never launched.

The runtime harness includes the product under a renamed, uncalled entry point and injects a fake Startup transaction backend. It creates an isolated temporary INI and invisible popup-menu objects. Cleanup passed. No product entry point, ADB executable, TV/device, registered tray icon, real Startup entry, registry startup location, or scheduled task was exercised. No resident process from this task remains running.

Usage was checked before validation: 59% five-hour / 74% weekly before the runtime suite; 66% five-hour / 92% weekly before syntax/source and build-only validation. The final change making the default build inert was inspected rather than rerunning unchanged compilation.

## Remaining limitations and findings

- Live visual tray behavior and Explorer restart were not exercised; the suite checks menu objects, input routing, and tooltip data without publishing a real icon.
- ADB hardware behavior is preserved but untested here. Existing close/exit behavior waits for an active command to finish (its current timeout is 30 seconds).
- Most mutation menu items disable while busy; handlers also guard against mutation. The dropdown-layout header remains visible and its handler ignores changes during a command.
- Shared ShellLink path-length restrictions and Windows Startup policy can still prevent a shortcut from being applied; diagnostics report this instead of silently claiming success.
- The optional legacy `ADBControllerBinarySmoke.ps1` still contains `Start-Process -Wait` without a watchdog. It is no longer run by the default build or inert harness. Replacing that optional runner is separate outstanding test-infrastructure work.
- No new credential protection, path security policy, or device trust policy was introduced. Setup convenience and functional consistency remained the priority.
